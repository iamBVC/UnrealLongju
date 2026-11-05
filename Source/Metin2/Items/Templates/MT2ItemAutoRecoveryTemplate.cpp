/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Items/Templates/MT2ItemAutoRecoveryTemplate.h"
#include "Characters/MT2PlayerCharacter.h"
#include "Components/MT2HealthComponent.h"
#include "Components/MT2ManaComponent.h"
#include "Components/MT2StatusEffectComponent.h"
#include "Items/MT2InventoryComponent.h"
#include "Player/MT2PlayerState.h"
#include "Trade/MT2TradeComponent.h"

EMT2ItemUseExecution UMT2ItemAutoRecoveryTemplate::ExecuteUse(
	AMT2PlayerCharacter& Character, UMT2InventoryComponent& Inventory, int32 InventorySlot) const
{
	const auto* Health = Character.GetHealthComponent();
	const auto* State = Character.GetPlayerState<AMT2PlayerState>();
	if (!Character.HasAuthority() || Inventory.GetOwner() != &Character || !Health || Health->IsDead() ||
		!Character.GetStatusEffectComponent() || RecoveryCapacity <= 0 ||
		(Resource != EMT2AutoRecoveryResource::Health && Resource != EMT2AutoRecoveryResource::Mana) ||
		(State && State->GetTradeComponent() && State->GetTradeComponent()->IsTrading()))
		return EMT2ItemUseExecution::Rejected;
	return Inventory.ToggleAutoRecoveryItem(InventorySlot, *this) ? EMT2ItemUseExecution::Accepted : EMT2ItemUseExecution::Rejected;
}

int32 UMT2ItemAutoRecoveryTemplate::PrepareRecoveryTransfer(
	AMT2PlayerCharacter& Character, int32 Available, FMT2StatusEffect& OutPool) const
{
	const auto* Health = Character.GetHealthComponent();
	const auto* Effects = Character.GetStatusEffectComponent();
	if (!Character.HasAuthority() || !Health || Health->IsDead() || !Effects || Available <= 0 ||
		Effects->HasEffectFlag(EMT2EffectFlag::Stun) ||
		Effects->HasEffectType(FMT2AffectIdentifier::FromBuiltin(EMT2BuiltinAffect::Stun))) return 0;
	if (Resource != EMT2AutoRecoveryResource::Health && Resource != EMT2AutoRecoveryResource::Mana) return 0;
	const bool bHealth = Resource == EMT2AutoRecoveryResource::Health;
	const UMT2ResourceComponent* Target = bHealth ? static_cast<const UMT2ResourceComponent*>(Health) : Character.GetManaComponent();
	if (!Target || !Target->IsInitialized() || !FMath::IsFinite(Target->GetMaxValue()) ||
		!FMath::IsFinite(Target->GetCurrentValue())) return 0;
	const auto Type = FMT2AffectIdentifier::FromBuiltin(bHealth ? EMT2BuiltinAffect::HealthRecovery : EMT2BuiltinAffect::ManaRecovery);
	const int64 Maximum = static_cast<int64>(FMath::Clamp<double>(Target->GetMaxValue(), 0.0, MAX_int32));
	const int64 Current = static_cast<int64>(FMath::Clamp<double>(Target->GetCurrentValue(), 0.0, MAX_int32));
	const int32 Pending = FMath::Max(0, Effects->GetEffectValueByType(Type));
	const int32 Amount = static_cast<int32>(FMath::Min<int64>(Available, FMath::Max<int64>(0, Maximum - Current - Pending)));
	if (Amount <= 0) return 0;
	OutPool = FMT2StatusEffect();
	OutPool.Kind = bHealth ? EMT2StatusEffectKind::HealthRecovery : EMT2StatusEffectKind::ManaRecovery;
	OutPool.Type = Type; OutPool.ApplyValue = Pending + Amount; OutPool.RemainingSeconds = -1;
	OutPool.SourceItemVnum = Vnum; OutPool.Icon = Icon; OutPool.bRemoveOnDeath = true;
	return Amount;
}

void UMT2ItemAutoRecoveryTemplate::InitializeGeneratedInstance(
	FMT2ItemInstanceData& InOutData) const
{
	if (InOutData.GetAutoRecoveryState().MaximumAmount <= 0)
	{
		InOutData.EditAutoRecoveryState().MaximumAmount = FMath::Max(RecoveryCapacity, 0);
		InOutData.EditAutoRecoveryState().RemainingAmount = InOutData.GetAutoRecoveryState().MaximumAmount;
	}
	InOutData.EditAutoRecoveryState().RemainingAmount = FMath::Clamp(
		InOutData.GetAutoRecoveryState().RemainingAmount, 0, InOutData.GetAutoRecoveryState().MaximumAmount);
}

void UMT2ItemAutoRecoveryTemplate::InitializeDefaultFamilyState(FMT2ItemInstanceData& Data) const
{
	Data.FamilyState.InitializeAs<FMT2AutoRecoveryItemState>(DefaultRecoveryState);
}

void UMT2ItemAutoRecoveryTemplate::CaptureDefaultFamilyState(const FMT2ItemInstanceData& Data)
{
	DefaultRecoveryState = Data.GetAutoRecoveryState();
}
