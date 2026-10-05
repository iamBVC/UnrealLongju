/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Items/MT2InventoryComponent.h"

#include "Characters/MT2PlayerCharacter.h"
#include "Items/MT2ItemTemplate.h"
#include "Player/MT2PlayerState.h"

int32 UMT2InventoryComponent::CountItemByVnum(int32 Vnum) const
{
	int32 Total = 0;
	for (const FMT2ItemSlot& Slot : Slots)
	{
		if (!Slot.IsEmpty() && Slot.Vnum == Vnum)
		{
			Total += Slot.Count;
		}
	}
	return Total;
}

bool UMT2InventoryComponent::CanRefineItem(int32 TargetSlot) const
{
	if (!Slots.IsValidIndex(TargetSlot) || Slots[TargetSlot].IsEmpty())
	{
		return false;
	}
	const UMT2ItemTemplate* CurrentTemplate = ResolveTemplate(Slots[TargetSlot].Vnum);
	return CurrentTemplate && CurrentTemplate->RefinedVnum > 0 &&
		CurrentTemplate->RefinementRecipe.IsValid() &&
		ResolveTemplate(CurrentTemplate->RefinedVnum) != nullptr;
}

EMT2RefinementResult UMT2InventoryComponent::RefineItem(int32 TargetSlot, int32 ScrollSlot)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Slots.IsValidIndex(TargetSlot) ||
		Slots[TargetSlot].IsEmpty() || TargetSlot == ScrollSlot || !CanRefineItem(TargetSlot))
	{
		return EMT2RefinementResult::Invalid;
	}

	const UMT2ItemTemplate* CurrentTemplate = ResolveTemplate(Slots[TargetSlot].Vnum);
	const UMT2ItemTemplate* FutureTemplate =
		CurrentTemplate ? ResolveTemplate(CurrentTemplate->RefinedVnum) : nullptr;
	if (!CurrentTemplate || !FutureTemplate)
	{
		return EMT2RefinementResult::Invalid;
	}

	const UMT2ItemRefinementScrollTemplate* ScrollTemplate = nullptr;
	if (ScrollSlot != INDEX_NONE)
	{
		if (!Slots.IsValidIndex(ScrollSlot) || Slots[ScrollSlot].IsEmpty())
		{
			return EMT2RefinementResult::Invalid;
		}
		ScrollTemplate =
			Cast<UMT2ItemRefinementScrollTemplate>(ResolveTemplate(Slots[ScrollSlot].Vnum));
		if (!ScrollTemplate)
		{
			return EMT2RefinementResult::Invalid;
		}
	}

	const int32 ConsumedScrollSlot = ScrollTemplate &&
		Slots[ScrollSlot].Count == 1 ? ScrollSlot : INDEX_NONE;
	if (!CanPlaceAt(TargetSlot, GetItemSize(FutureTemplate->Vnum), TargetSlot, ConsumedScrollSlot))
	{
		return EMT2RefinementResult::NoInventorySpace;
	}

	const FMT2RefinementRecipe& Recipe = CurrentTemplate->RefinementRecipe;
	const auto CountMaterial = [this, TargetSlot, ScrollSlot](int32 Vnum)
	{
		int32 Total = 0;
		for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
		{
			if (SlotIndex != TargetSlot && SlotIndex != ScrollSlot &&
				!Slots[SlotIndex].IsEmpty() && Slots[SlotIndex].Vnum == Vnum)
			{
				Total += Slots[SlotIndex].Count;
			}
		}
		return Total;
	};
	for (const FMT2RefinementMaterial& Material : Recipe.Materials)
	{
		if (CountMaterial(Material.ItemVnum) < Material.Count)
		{
			return EMT2RefinementResult::MissingMaterials;
		}
	}

	AMT2PlayerCharacter* Character = Cast<AMT2PlayerCharacter>(GetOwner());
	AMT2PlayerState* PlayerState = Character ? Character->GetPlayerState<AMT2PlayerState>() : nullptr;
	if (!PlayerState || PlayerState->GetYang() < Recipe.YangCost)
	{
		return EMT2RefinementResult::NotEnoughYang;
	}

	const auto RemoveMaterial = [this, TargetSlot, ScrollSlot](int32 Vnum, int32 Count)
	{
		for (int32 SlotIndex = 0; SlotIndex < Slots.Num() && Count > 0; ++SlotIndex)
		{
			FMT2ItemSlot& Slot = Slots[SlotIndex];
			if (SlotIndex == TargetSlot || SlotIndex == ScrollSlot ||
				Slot.IsEmpty() || Slot.Vnum != Vnum)
			{
				continue;
			}
			const int32 Removed = FMath::Min(Slot.Count, Count);
			Slot.Count -= Removed;
			Count -= Removed;
			if (Slot.Count <= 0)
			{
				Slot = FMT2ItemSlot();
			}
		}
	};
	for (const FMT2RefinementMaterial& Material : Recipe.Materials)
	{
		RemoveMaterial(Material.ItemVnum, Material.Count);
	}
	if (ScrollTemplate)
	{
		--Slots[ScrollSlot].Count;
		if (Slots[ScrollSlot].Count <= 0)
		{
			Slots[ScrollSlot] = FMT2ItemSlot();
		}
	}
	PlayerState->AddYang(-Recipe.YangCost);

	const int32 SuccessChance = FMath::Clamp(
		Recipe.SuccessPercent + (ScrollTemplate ? ScrollTemplate->AdditionalSuccessPercent : 0),
		0, 100);
	EMT2RefinementResult Result = EMT2RefinementResult::Success;
	if (FMath::RandRange(1, 100) <= SuccessChance)
	{
		Slots[TargetSlot].Vnum = FutureTemplate->Vnum;
	}
	else if (!ScrollTemplate)
	{
		Slots[TargetSlot] = FMT2ItemSlot();
		Result = EMT2RefinementResult::FailedDestroyed;
	}
	else if (CurrentTemplate->RefinementLevel > 0 && CurrentTemplate->PreviousRefinedVnum > 0)
	{
		Slots[TargetSlot].Vnum = CurrentTemplate->PreviousRefinedVnum;
		Result = EMT2RefinementResult::FailedDowngraded;
	}
	else
	{
		Result = EMT2RefinementResult::FailedAtMinimum;
	}

	OnInventoryChanged.Broadcast();
	return Result;
}
