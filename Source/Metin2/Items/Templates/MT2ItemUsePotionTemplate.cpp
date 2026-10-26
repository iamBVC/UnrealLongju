#include "MT2ItemUsePotionTemplate.h"

#include "Characters/MT2PlayerCharacter.h"
#include "Components/MT2HealthComponent.h"
#include "Components/MT2ManaComponent.h"
#include "Components/MT2StatusEffectComponent.h"
#include "Config/MT2GameplaySettings.h"
#include "Items/MT2InventoryComponent.h"

UMT2ItemUsePotionTemplate::UMT2ItemUsePotionTemplate()
{
	UseSubType = EMT2UseSubType::Potion;
}

EMT2ItemUseExecution UMT2ItemUsePotionTemplate::ExecuteUse(
	AMT2PlayerCharacter& Character, UMT2InventoryComponent& Inventory, int32 InventorySlot) const
{
	UMT2HealthComponent* Health = Character.GetHealthComponent();
	UMT2StatusEffectComponent* Effects = Character.GetStatusEffectComponent();
	if (!Character.HasAuthority() || Inventory.GetOwner() != &Character || !Health || Health->IsDead() ||
		!Effects || HealthRecovery < 0 || ManaRecovery < 0 || (HealthRecovery == 0 && ManaRecovery == 0))
		return EMT2ItemUseExecution::Rejected;

	const int64 Multiplier = FMath::Min<int64>(
		FMath::Max(1, UMT2GameplaySettings::Get().QueuedPotionMaximumEffectPercent),
		100LL + Character.GetItemApplyBonus(EMT2ItemBonusType::PotionBonus));
	if (Multiplier < 0) return EMT2ItemUseExecution::Rejected;
	TArray<FMT2StatusEffect, TInlineAllocator<2>> Pending;
	auto Prepare = [&](UMT2ResourceComponent* Resource, int32 Quantity, EMT2StatusEffectKind Kind, EMT2BuiltinAffect Type)
	{
		if (Quantity == 0) return true;
		if (!Resource || !Resource->IsInitialized()) return false;
		const FMT2AffectIdentifier Affect = FMT2AffectIdentifier::FromBuiltin(Type);
		const int32 Existing = Effects->GetEffectValueByType(Affect);
		if (static_cast<double>(Resource->GetCurrentValue()) + Existing >= Resource->GetMaxValue()) return false;
		FMT2StatusEffect Effect;
		Effect.Kind = Kind;
		Effect.Type = Affect;
		Effect.SourceItemVnum = Vnum;
		// Eligibility is checked before adding; legacy does not cap the new pool to the deficit.
		Effect.ApplyValue = static_cast<int32>(FMath::Clamp<int64>(Existing + static_cast<int64>(Quantity) * Multiplier / 100, 0, MAX_int32));
		Effect.RemainingSeconds = -1;
		Effect.bRemoveOnDeath = true;
		Effect.Icon = Icon;
		Pending.Add(Effect);
		return true;
	};
	// Preflight both resources to avoid granting a free MP pool if the later HP gate rejects.
	if (!Prepare(Character.GetManaComponent(), ManaRecovery, EMT2StatusEffectKind::ManaRecovery, EMT2BuiltinAffect::ManaRecovery) ||
		!Prepare(Health, HealthRecovery, EMT2StatusEffectKind::HealthRecovery, EMT2BuiltinAffect::HealthRecovery))
		return EMT2ItemUseExecution::Rejected;
	bool bUsed = false;
	for (const FMT2StatusEffect& Effect : Pending) bUsed |= Effects->AddEffect(Effect, true);
	return bUsed ? EMT2ItemUseExecution::Consume : EMT2ItemUseExecution::Rejected;
}

