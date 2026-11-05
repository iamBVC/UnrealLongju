#include "MT2ItemUseAbilityUpTemplate.h"

#include "Characters/MT2PlayerCharacter.h"
#include "Components/MT2HealthComponent.h"
#include "Components/MT2StatusEffectComponent.h"
#include "Items/MT2InventoryComponent.h"
#include "Serialization/CustomVersion.h"

namespace
{
	const FGuid AbilityFieldsVersion(0xCA820F61, 0x448A4F2D, 0xB3435A88, 0xBF112031);
	FCustomVersionRegistration RegisterAbilityFields(AbilityFieldsVersion, 1, TEXT("MT2AbilityItemFields"));
}

UMT2ItemUseAbilityUpTemplate::UMT2ItemUseAbilityUpTemplate()
{
	UseSubType = EMT2UseSubType::AbilityUp;
}

void UMT2ItemUseAbilityUpTemplate::Serialize(FStructuredArchive::FRecord Record)
{
	FArchive& Ar = Record.GetUnderlyingArchive();
	Ar.UsingCustomVersion(AbilityFieldsVersion);
	if (Ar.IsLoading() && Ar.IsPersistent()) bMigrateAbilityFields = Ar.CustomVer(AbilityFieldsVersion) < 1;
	Super::Serialize(Record);
}

void UMT2ItemUseAbilityUpTemplate::PostLoad()
{
	Super::PostLoad();
	if (bMigrateAbilityFields)
	{
		BonusType = GrantedBonus; BonusAmount = GrantedBonusValue; DurationSeconds = BuffDurationSeconds;
	}
	bMigrateAbilityFields = false;
}

bool UMT2ItemUseAbilityUpTemplate::BuildEffect(FMT2StatusEffect& Effect) const
{
	EMT2BuiltinAffect Identity;
	switch (BonusType)
	{
	case EMT2ItemBonusType::MovementSpeed:
		Identity = EMT2BuiltinAffect::MoveSpeed; Effect.Kind = EMT2StatusEffectKind::MovementSpeed;
		Effect.Flags = 1u << static_cast<uint8>(EMT2EffectFlag::MoveSpeedPotion); break;
	case EMT2ItemBonusType::AttackSpeed:
		Identity = EMT2BuiltinAffect::AttackSpeedLegacy; Effect.Kind = EMT2StatusEffectKind::AttackSpeed;
		Effect.Flags = 1u << static_cast<uint8>(EMT2EffectFlag::AttackSpeedPotion); break;
	case EMT2ItemBonusType::Strength: Identity = EMT2BuiltinAffect::Strength; break;
	case EMT2ItemBonusType::Dexterity: Identity = EMT2BuiltinAffect::Dexterity; break;
	case EMT2ItemBonusType::Constitution: Identity = EMT2BuiltinAffect::Constitution; break;
	case EMT2ItemBonusType::Intelligence: Identity = EMT2BuiltinAffect::Intelligence; break;
	case EMT2ItemBonusType::CastSpeed: Identity = EMT2BuiltinAffect::CastSpeed; break;
	case EMT2ItemBonusType::AttackValue: Identity = EMT2BuiltinAffect::AttackGrade; break;
	case EMT2ItemBonusType::DefenseBonus: Identity = EMT2BuiltinAffect::DefenseGrade; break;
	default: return false;
	}
	Effect.Type = FMT2AffectIdentifier::FromBuiltin(Identity);
	Effect.bRemoveOnDeath = true;
	return true;
}

void UMT2ItemUseAbilityUpTemplate::NormalizeRestoredEffect(FMT2StatusEffect& Effect) const
{
	if (Effect.SourceItemVnum <= 0 || Effect.ApplyType != BonusType) return;
	FMT2StatusEffect Expected, Standard;
	if (!BuildEffect(Expected)) return;
	const bool bStandard = UMT2ItemUseAbilityUpTemplate::BuildEffect(Standard);
	const bool bOldSpeed =
		(BonusType == EMT2ItemBonusType::AttackSpeed && Effect.Type == FMT2AffectIdentifier::FromBuiltin(EMT2BuiltinAffect::AttackSpeed)) ||
		(BonusType == EMT2ItemBonusType::MovementSpeed && Effect.Type == FMT2AffectIdentifier::FromBuiltin(EMT2BuiltinAffect::MovementSpeed));
	if (!(Effect.Type == Expected.Type) && !(bStandard && Effect.Type == Standard.Type) && !bOldSpeed) return;
	// Normalize recognized item-origin identities only. Old speed records used synthetic IDs;
	// old dew records used ordinary ability identities and the ordinary death policy.
	Effect.Type = Expected.Type; Effect.Kind = Expected.Kind;
	Effect.Flags = Expected.Flags; Effect.bRemoveOnDeath = Expected.bRemoveOnDeath;
}

EMT2ItemUseExecution UMT2ItemUseAbilityUpTemplate::ExecuteUse(
	AMT2PlayerCharacter& Character, UMT2InventoryComponent& Inventory, int32 InventorySlot) const
{
	auto* Effects = Character.GetStatusEffectComponent();
	if (!Character.HasAuthority() || Inventory.GetOwner() != &Character || !Effects ||
		!Character.GetHealthComponent() || Character.GetHealthComponent()->IsDead() || DurationSeconds <= 0)
		return EMT2ItemUseExecution::Rejected;
	FMT2StatusEffect Effect;
	if (!BuildEffect(Effect)) return EMT2ItemUseExecution::Rejected;
	if (!RefreshesExistingEffect() && Effects->GetEffects().ContainsByPredicate(
		[&](const FMT2StatusEffect& Existing) { return Existing.Type == Effect.Type && Existing.ApplyType == BonusType; }))
		return EMT2ItemUseExecution::Rejected;
	Effect.SourceItemVnum = Vnum; Effect.ApplyType = BonusType; Effect.ApplyValue = BonusAmount;
	Effect.RemainingSeconds = DurationSeconds; Effect.Icon = Icon;
	return Effects->AddEffect(Effect, RefreshesExistingEffect()) ? EMT2ItemUseExecution::Consume : EMT2ItemUseExecution::Rejected;
}

