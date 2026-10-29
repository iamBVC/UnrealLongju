#include "MT2ItemUsePotionContinueTemplate.h"

#include "Characters/MT2PlayerCharacter.h"
#include "Components/MT2HealthComponent.h"
#include "Components/MT2ManaComponent.h"
#include "Components/MT2StatusEffectComponent.h"
#include "Items/MT2InventoryComponent.h"
#include "Serialization/CustomVersion.h"

namespace
{
	const FGuid RecoveryDurationVersion(0x63D25B44, 0xC9CB4A3F, 0xA49CFF51, 0xF4C8FCE2);
	FCustomVersionRegistration RegisterRecoveryDurationVersion(RecoveryDurationVersion, 1, TEXT("MT2ContinuousRecoveryDuration"));
}

UMT2ItemUsePotionContinueTemplate::UMT2ItemUsePotionContinueTemplate()
{
	UseSubType = EMT2UseSubType::PotionContinue;
}

void UMT2ItemUsePotionContinueTemplate::Serialize(FStructuredArchive::FRecord Record)
{
	FArchive& Ar = Record.GetUnderlyingArchive();
	Ar.UsingCustomVersion(RecoveryDurationVersion);
	if (Ar.IsLoading() && Ar.IsPersistent())
		bMigrateRecoveryDuration = Ar.CustomVer(RecoveryDurationVersion) < 1;
	Super::Serialize(Record);
}

void UMT2ItemUsePotionContinueTemplate::PostLoad()
{
	Super::PostLoad();
	// Package version, not value, distinguishes old defaults from newly authored durations. This
	// preserves child overrides of zero/negative even if the parent has already migrated.
	if (bMigrateRecoveryDuration)
		RecoveryDurationSeconds = BuffDurationSeconds;
	// Retain the hidden legacy default: old child CDO delta archives omit inherited values and
	// still need this parent default during load. No gameplay/tooltip code reads it for potions.
	bMigrateRecoveryDuration = false;
}

EMT2ItemUseExecution UMT2ItemUsePotionContinueTemplate::ExecuteUse(
	AMT2PlayerCharacter& Character, UMT2InventoryComponent& Inventory, int32 InventorySlot) const
{
	UMT2HealthComponent* Health = Character.GetHealthComponent();
	UMT2StatusEffectComponent* Effects = Character.GetStatusEffectComponent();
	if (!Character.HasAuthority() || Inventory.GetOwner() != &Character || !Health || Health->IsDead() ||
		!Effects || RecoveryDurationSeconds <= 0 || HealthRecovery < 0 || ManaRecovery < 0 ||
		(HealthRecovery == 0 && ManaRecovery == 0))
		return EMT2ItemUseExecution::Rejected;

	// USE_POTION_CONTINUE chooses HP before MP and does not scale by Potion Effect Bonus.
	const bool bHealth = HealthRecovery > 0;
	const UMT2ResourceComponent* Resource = bHealth ? static_cast<UMT2ResourceComponent*>(Health) : Character.GetManaComponent();
	if (!Resource || !Resource->IsInitialized()) return EMT2ItemUseExecution::Rejected;
	FMT2StatusEffect Effect;
	Effect.Kind = bHealth ? EMT2StatusEffectKind::ContinuousHealthRecovery : EMT2StatusEffectKind::ContinuousManaRecovery;
	Effect.Type = FMT2AffectIdentifier::FromBuiltin(bHealth ? EMT2BuiltinAffect::HealthRecoveryContinue : EMT2BuiltinAffect::ManaRecoveryContinue);
	Effect.SourceItemVnum = Vnum;
	Effect.ApplyValue = bHealth ? HealthRecovery : ManaRecovery;
	Effect.RemainingSeconds = RecoveryDurationSeconds;
	Effect.bRemoveOnDeath = true;
	Effect.Icon = Icon;
	return Effects->AddEffect(Effect, true) ? EMT2ItemUseExecution::Consume : EMT2ItemUseExecution::Rejected;
}

