/*
- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.
*/

#include "Items/MT2InventoryComponent.h"
#include "Characters/MT2PlayerCharacter.h"
#include "Components/MT2StatusEffectComponent.h"
#include "Config/MT2GameplaySettings.h"
#include "Engine/World.h"
#include "TimerManager.h"

bool UMT2InventoryComponent::ToggleAutoRecoveryItem(
	int32 InventorySlot, const UMT2ItemAutoRecoveryTemplate& Template)
{
	if (bProcessingAutoRecovery || !GetOwner() || !GetOwner()->HasAuthority() ||
		!Slots.IsValidIndex(InventorySlot) || Slots[InventorySlot].IsEmpty() ||
		ResolveTemplate(Slots[InventorySlot].Vnum) != &Template) return false;
	const auto& State = Slots[InventorySlot].GetAutoRecoveryState();
	// Existing state is authoritative; activation never initializes or refills capacity.
	if (State.MaximumAmount <= 0 || State.RemainingAmount <= 0 || State.RemainingAmount > State.MaximumAmount) return false;
	int32 FreeSlot = INDEX_NONE;
	if (Slots[InventorySlot].Count > 1)
	{
		if (State.bActive) return false;
		for (int32 Candidate = 0; Candidate < Slots.Num(); ++Candidate)
			if (CanPlaceAt(Candidate, GetItemSize(Template.Vnum))) { FreeSlot = Candidate; break; }
		if (FreeSlot == INDEX_NONE) return false;
	}
	if (FreeSlot != INDEX_NONE)
	{
		FMT2ItemSlot Single = Slots[InventorySlot]; Single.Count = 1;
		--Slots[InventorySlot].Count; Slots[FreeSlot] = MoveTemp(Single); InventorySlot = FreeSlot;
	}
	const FMT2ItemSlot Before = Slots[InventorySlot];
	if (Before.GetAutoRecoveryState().bActive)
	{
		Slots[InventorySlot].EditAutoRecoveryState().bActive = false;
		StopAutoRecoveryEffectForSlot(Before);
		StartAutoRecoveryTimerIfNeeded(); OnInventoryChanged.Broadcast(); return true;
	}
	// Commit selection before notifying listeners; one reservoir per resource remains active.
	for (auto& Other : Slots)
	{
		if (Other.IsEmpty() || !Other.GetAutoRecoveryState().bActive) continue;
		const auto* OtherTemplate = Cast<UMT2ItemAutoRecoveryTemplate>(ResolveTemplate(Other.Vnum));
		if (OtherTemplate && OtherTemplate->Resource == Template.Resource) Other.EditAutoRecoveryState().bActive = false;
	}
	Slots[InventorySlot].EditAutoRecoveryState().bActive = true;
	RefreshAutoRecoveryEffect(Template, Before.GetAutoRecoveryState().RemainingAmount);
	StartAutoRecoveryTimerIfNeeded(); ProcessAutoRecoveryItems(); OnInventoryChanged.Broadcast();
	return true;
}

void UMT2InventoryComponent::RefreshAutoRecoveryEffect(
	const UMT2ItemAutoRecoveryTemplate& Template, int32 RemainingAmount)
{
	auto* Character = Cast<AMT2PlayerCharacter>(GetOwner());
	auto* Effects = Character ? Character->GetStatusEffectComponent() : nullptr;
	if (!Effects || RemainingAmount <= 0) return;
	const bool bHealth = Template.Resource == EMT2AutoRecoveryResource::Health;
	FMT2StatusEffect Effect;
	Effect.Kind = bHealth ? EMT2StatusEffectKind::AutoHealthRecovery : EMT2StatusEffectKind::AutoManaRecovery;
	Effect.Type = FMT2AffectIdentifier::FromBuiltin(bHealth ? EMT2BuiltinAffect::AutoHealthRecovery : EMT2BuiltinAffect::AutoManaRecovery);
	Effect.SourceItemVnum = Template.Vnum; Effect.ApplyValue = RemainingAmount;
	Effect.RemainingSeconds = -1; Effect.Icon = Template.Icon;
	const auto* Existing = Effects->GetEffects().FindByPredicate([&](const FMT2StatusEffect& Value) { return Value.Type == Effect.Type; });
	if (Existing && Existing->Kind == Effect.Kind && Existing->SourceItemVnum == Effect.SourceItemVnum &&
		Existing->ApplyValue == Effect.ApplyValue && Existing->RemainingSeconds == -1 && Existing->Icon == Effect.Icon) return;
	// Replace complete metadata so switching also updates source and icon.
	Effects->AddEffect(Effect, true);
}

void UMT2InventoryComponent::StopAutoRecoveryEffectForSlot(const FMT2ItemSlot& Slot)
{
	if (!Slot.GetAutoRecoveryState().bActive) return;
	const auto* Template = Cast<UMT2ItemAutoRecoveryTemplate>(ResolveTemplate(Slot.Vnum));
	auto* Character = Cast<AMT2PlayerCharacter>(GetOwner());
	auto* Effects = Character ? Character->GetStatusEffectComponent() : nullptr;
	if (Template && Effects)
		Effects->RemoveEffectsByType(FMT2AffectIdentifier::FromBuiltin(Template->Resource == EMT2AutoRecoveryResource::Health
			? EMT2BuiltinAffect::AutoHealthRecovery : EMT2BuiltinAffect::AutoManaRecovery));
}

void UMT2InventoryComponent::StartAutoRecoveryTimerIfNeeded()
{
	if (!GetWorld() || !GetOwner() || !GetOwner()->HasAuthority()) return;
	const bool bActive = Slots.ContainsByPredicate([](const FMT2ItemSlot& Item)
		{ return !Item.IsEmpty() && Item.GetAutoRecoveryState().bActive; });
	if (bActive && !GetWorld()->GetTimerManager().IsTimerActive(AutoRecoveryTimer))
	{
		const float Authored = UMT2GameplaySettings::Get().AutoRecoveryProcessIntervalSeconds;
		const float Interval = FMath::IsFinite(Authored) ? FMath::Max(0.1f, Authored) : 1.0f;
		GetWorld()->GetTimerManager().SetTimer(AutoRecoveryTimer, this,
			&UMT2InventoryComponent::ProcessAutoRecoveryItems, Interval, true);
	}
	else if (!bActive) GetWorld()->GetTimerManager().ClearTimer(AutoRecoveryTimer);
}

void UMT2InventoryComponent::ProcessAutoRecoveryItems()
{
	auto* Character = Cast<AMT2PlayerCharacter>(GetOwner());
	auto* Effects = Character ? Character->GetStatusEffectComponent() : nullptr;
	if (!Character || !Character->HasAuthority() || !Effects || bProcessingAutoRecovery) return;
	TGuardValue<bool> Guard(bProcessingAutoRecovery, true);
	bool bChanged = false;
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		if (Slots[Index].IsEmpty() || !Slots[Index].GetAutoRecoveryState().bActive) continue;
		const FMT2ItemSlot Before = Slots[Index];
		const auto* Template = Cast<UMT2ItemAutoRecoveryTemplate>(ResolveTemplate(Before.Vnum));
		const auto& State = Before.GetAutoRecoveryState();
		if (!Template || Before.Count != 1 || State.MaximumAmount <= 0 || State.RemainingAmount <= 0 ||
			State.RemainingAmount > State.MaximumAmount)
		{
			Slots[Index].EditAutoRecoveryState().bActive = false;
			StopAutoRecoveryEffectForSlot(Before); bChanged = true; continue;
		}
		FMT2StatusEffect Pool;
		const int32 Transfer = Template->PrepareRecoveryTransfer(*Character, State.RemainingAmount, Pool);
		if (Transfer > 0)
		{
			const int32 Remaining = State.RemainingAmount - Transfer;
			// Reserve before publishing; listeners may remove/move the original slot.
			Slots[Index].EditAutoRecoveryState().RemainingAmount = Remaining;
			if (Remaining == 0) Slots[Index] = FMT2ItemSlot();
			if (!Effects->AddEffect(Pool, true))
			{
				// Failed AddEffect has not published callbacks.
				Slots[Index] = Before;
				continue;
			}
			bChanged = true;
			if (Remaining == 0) { StopAutoRecoveryEffectForSlot(Before); continue; }
		}
		if (Slots.IsValidIndex(Index) && Slots[Index].Vnum == Before.Vnum &&
			Slots[Index].GetAutoRecoveryState().bActive)
			RefreshAutoRecoveryEffect(*Template, Slots[Index].GetAutoRecoveryState().RemainingAmount);
	}
	StartAutoRecoveryTimerIfNeeded();
	if (bChanged) OnInventoryChanged.Broadcast();
}
