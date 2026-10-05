/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Items/MT2InventoryComponent.h"

#include "Characters/MT2PlayerCharacter.h"
#include "Components/MT2HealthComponent.h"
#include "Components/MT2ManaComponent.h"
#include "Components/MT2StatusEffectComponent.h"
#include "Components/MT2StatusEffectDefinition.h"
#include "Engine/World.h"
#include "Items/MT2ItemTemplate.h"
#include "TimerManager.h"

bool UMT2InventoryComponent::ToggleAutoRecoveryItem(
	int32 InventorySlot, const UMT2ItemAutoRecoveryTemplate& Template)
{
	if (!Slots.IsValidIndex(InventorySlot) || Slots[InventorySlot].IsEmpty() ||
		Template.RecoveryCapacity <= 0)
	{
		return false;
	}

	if (Slots[InventorySlot].Count > 1)
	{
		int32 FreeSlot = INDEX_NONE;
		for (int32 Candidate = 0; Candidate < Slots.Num(); ++Candidate)
		{
			if (CanPlaceAt(Candidate, 1))
			{
				FreeSlot = Candidate;
				break;
			}
		}
		if (FreeSlot == INDEX_NONE)
		{
			return false;
		}
		FMT2ItemSlot Single = Slots[InventorySlot];
		Single.Count = 1;
		Slots[InventorySlot].Count--;
		Slots[FreeSlot] = MoveTemp(Single);
		InventorySlot = FreeSlot;
	}

	FMT2ItemSlot& Selected = Slots[InventorySlot];
	Template.InitializeGeneratedInstance(Selected);
	if (Selected.AutoRecoveryRemainingAmount <= 0)
	{
		return false;
	}
	if (Selected.bAutoRecoveryActive)
	{
		StopAutoRecoveryEffectForSlot(Selected);
		Selected.bAutoRecoveryActive = false;
		OnInventoryChanged.Broadcast();
		StartAutoRecoveryTimerIfNeeded();
		return true;
	}

	for (FMT2ItemSlot& Item : Slots)
	{
		if (Item.IsEmpty() || !Item.bAutoRecoveryActive)
		{
			continue;
		}
		const UMT2ItemAutoRecoveryTemplate* Other =
			Cast<UMT2ItemAutoRecoveryTemplate>(ResolveTemplate(Item.Vnum));
		if (Other && Other->Resource == Template.Resource)
		{
			StopAutoRecoveryEffectForSlot(Item);
			Item.bAutoRecoveryActive = false;
		}
	}
	Selected.bAutoRecoveryActive = true;
	RefreshAutoRecoveryEffect(Template, Selected.AutoRecoveryRemainingAmount);
	StartAutoRecoveryTimerIfNeeded();
	ProcessAutoRecoveryItems();
	OnInventoryChanged.Broadcast();
	return true;
}

void UMT2InventoryComponent::RefreshAutoRecoveryEffect(
	const UMT2ItemAutoRecoveryTemplate& Template, int32 RemainingAmount)
{
	AMT2PlayerCharacter* Character = Cast<AMT2PlayerCharacter>(GetOwner());
	UMT2StatusEffectComponent* Effects = Character ? Character->GetStatusEffectComponent() : nullptr;
	if (!Effects)
	{
		return;
	}
	const bool bHealth = Template.Resource == EMT2AutoRecoveryResource::Health;
	const int32 AffectType = bHealth
		? MT2AffectId::AutoHealthRecovery : MT2AffectId::AutoManaRecovery;
	if (!Effects->UpdateEffectValueByType(AffectType, RemainingAmount))
	{
		FMT2StatusEffect Effect;
		Effect.Kind = bHealth ? EMT2StatusEffectKind::AutoHealthRecovery
			: EMT2StatusEffectKind::AutoManaRecovery;
		Effect.Type = AffectType;
		Effect.SourceItemVnum = Template.Vnum;
		Effect.ApplyValue = RemainingAmount;
		Effect.RemainingSeconds = -1;
		Effect.Icon = Template.Icon;
		Effects->AddEffect(Effect, true);
	}
}

void UMT2InventoryComponent::StopAutoRecoveryEffectForSlot(const FMT2ItemSlot& Slot)
{
	if (!Slot.bAutoRecoveryActive)
	{
		return;
	}
	const UMT2ItemAutoRecoveryTemplate* Template =
		Cast<UMT2ItemAutoRecoveryTemplate>(ResolveTemplate(Slot.Vnum));
	AMT2PlayerCharacter* Character = Cast<AMT2PlayerCharacter>(GetOwner());
	UMT2StatusEffectComponent* Effects = Character ? Character->GetStatusEffectComponent() : nullptr;
	if (Template && Effects)
	{
		Effects->RemoveEffectsByType(Template->Resource == EMT2AutoRecoveryResource::Health
			? MT2AffectId::AutoHealthRecovery : MT2AffectId::AutoManaRecovery);
	}
}

void UMT2InventoryComponent::StartAutoRecoveryTimerIfNeeded()
{
	if (!GetWorld() || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	const bool bHasActive = Slots.ContainsByPredicate([](const FMT2ItemSlot& Item)
	{
		return !Item.IsEmpty() && Item.bAutoRecoveryActive;
	});
	if (bHasActive && !GetWorld()->GetTimerManager().IsTimerActive(AutoRecoveryTimer))
	{
		GetWorld()->GetTimerManager().SetTimer(
			AutoRecoveryTimer, this, &UMT2InventoryComponent::ProcessAutoRecoveryItems, 1.0f, true);
	}
	else if (!bHasActive)
	{
		GetWorld()->GetTimerManager().ClearTimer(AutoRecoveryTimer);
	}
}

void UMT2InventoryComponent::ProcessAutoRecoveryItems()
{
	AMT2PlayerCharacter* Character = Cast<AMT2PlayerCharacter>(GetOwner());
	if (!Character || !Character->HasAuthority())
	{
		return;
	}
	bool bChanged = false;
	for (FMT2ItemSlot& Item : Slots)
	{
		if (Item.IsEmpty() || !Item.bAutoRecoveryActive)
		{
			continue;
		}
		const UMT2ItemAutoRecoveryTemplate* Template =
			Cast<UMT2ItemAutoRecoveryTemplate>(ResolveTemplate(Item.Vnum));
		if (!Template)
		{
			Item.bAutoRecoveryActive = false;
			bChanged = true;
			continue;
		}
		Template->InitializeGeneratedInstance(Item);
		int32 Restored = 0;
		if (Template->Resource == EMT2AutoRecoveryResource::Health)
		{
			if (UMT2HealthComponent* Health = Character->GetHealthComponent();
				Health && Health->GetHealth() > 0.0f)
			{
				const int32 Missing = FMath::Max(
					0, FMath::FloorToInt(Health->GetMaxHealth() - Health->GetHealth()));
				Restored = FMath::Min3(Item.AutoRecoveryRemainingAmount, Missing,
					FMath::Max(1, FMath::FloorToInt(Health->GetMaxHealth() * 0.03f)));
				if (Restored > 0) Health->SetCurrentValue(Health->GetHealth() + Restored);
			}
		}
		else if (UMT2ManaComponent* Mana = Character->GetManaComponent())
		{
			const int32 Missing = FMath::Max(
				0, FMath::FloorToInt(Mana->GetMaxMana() - Mana->GetMana()));
			Restored = FMath::Min3(Item.AutoRecoveryRemainingAmount, Missing,
				FMath::Max(1, FMath::FloorToInt(Mana->GetMaxMana() * 0.03f)));
			if (Restored > 0) Mana->SetCurrentValue(Mana->GetMana() + Restored);
		}
		if (Restored > 0)
		{
			Item.AutoRecoveryRemainingAmount -= Restored;
			bChanged = true;
		}
		if (Item.AutoRecoveryRemainingAmount <= 0)
		{
			StopAutoRecoveryEffectForSlot(Item);
			Item = FMT2ItemSlot();
			bChanged = true;
		}
		else
		{
			RefreshAutoRecoveryEffect(*Template, Item.AutoRecoveryRemainingAmount);
		}
	}
	if (bChanged)
	{
		OnInventoryChanged.Broadcast();
	}
	StartAutoRecoveryTimerIfNeeded();
}
