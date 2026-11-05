#include "MT2ItemUseAffectTemplate.h"

#include "Characters/MT2PlayerCharacter.h"
#include "Components/MT2HealthComponent.h"
#include "Components/MT2StatusEffectComponent.h"
#include "Items/MT2InventoryComponent.h"
#include "Serialization/CustomVersion.h"

namespace
{
	const FGuid AffectFieldsVersion(0x37E704B6, 0xFF484217, 0xA9E3D1C6, 0x172EC50B);
	FCustomVersionRegistration RegisterAffectFields(AffectFieldsVersion, 1, TEXT("MT2AffectItemFields"));
}

UMT2ItemUseAffectTemplate::UMT2ItemUseAffectTemplate()
{
	UseSubType = EMT2UseSubType::Affect;
}

void UMT2ItemUseAffectTemplate::Serialize(FStructuredArchive::FRecord Record)
{
	FArchive& Ar = Record.GetUnderlyingArchive();
	Ar.UsingCustomVersion(AffectFieldsVersion);
	if (Ar.IsLoading() && Ar.IsPersistent()) bMigrateAffectFields = Ar.CustomVer(AffectFieldsVersion) < 1;
	Super::Serialize(Record);
}

void UMT2ItemUseAffectTemplate::PostLoad()
{
	Super::PostLoad();
	if (bMigrateAffectFields)
	{
		EffectIdentity = AffectType; BonusType = GrantedBonus;
		BonusAmount = GrantedBonusValue; DurationSeconds = BuffDurationSeconds;
	}
	bMigrateAffectFields = false;
}

EMT2ItemUseExecution UMT2ItemUseAffectTemplate::ExecuteUse(
	AMT2PlayerCharacter& Character, UMT2InventoryComponent& Inventory, int32 InventorySlot) const
{
	auto* Effects = Character.GetStatusEffectComponent();
	if (!Character.HasAuthority() || Inventory.GetOwner() != &Character || !Effects ||
		!Character.GetHealthComponent() || Character.GetHealthComponent()->IsDead() ||
		EffectIdentity.ToLegacyId() <= 0 || BonusType == EMT2ItemBonusType::None ||
		!StaticEnum<EMT2ItemBonusType>()->IsValidEnumValue(static_cast<int64>(BonusType)) || DurationSeconds <= 0)
		return EMT2ItemUseExecution::Rejected;
	// TODO(SYS-04): these require an authoritative PC-bang eligibility service. Reject rather than
	// granting a restricted affect to an unverified caller.
	if (Limits.ContainsByPredicate([](const FMT2ItemLimit& Limit) { return Limit.Type == EMT2ItemLimitType::PCBang; }))
	{
		UE_LOG(LogTemp, Warning, TEXT("Affect item %d requires the unimplemented PC-bang eligibility service."), Vnum);
		return EMT2ItemUseExecution::Rejected;
	}
	if (Effects->GetEffects().ContainsByPredicate([&](const FMT2StatusEffect& Existing)
		{ return Existing.Type == EffectIdentity && Existing.ApplyType == BonusType; }))
		return EMT2ItemUseExecution::Rejected;
	FMT2StatusEffect Effect;
	Effect.Type = EffectIdentity; Effect.ApplyType = BonusType; Effect.ApplyValue = BonusAmount;
	Effect.SourceItemVnum = Vnum; Effect.RemainingSeconds = DurationSeconds; Effect.Icon = Icon;
	// Legacy ClearAffect preserves block-chat and all 500-series affects at death.
	const int32 Identity = EffectIdentity.ToLegacyId();
	Effect.bRemoveOnDeath = !(EffectIdentity == FMT2AffectIdentifier::FromBuiltin(EMT2BuiltinAffect::BlockChat)) &&
		(Identity < 500 || Identity >= 600);
	return Effects->AddEffect(Effect, false) ? EMT2ItemUseExecution::Consume : EMT2ItemUseExecution::Rejected;
}

