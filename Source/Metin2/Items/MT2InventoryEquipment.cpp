/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Items/MT2InventoryComponent.h"

#include "Characters/MT2PlayerCharacter.h"
#include "Items/MT2InventoryLayout.h"
#include "Items/MT2ItemTemplate.h"
#include "Player/MT2PlayerState.h"
#include "Stats/MT2PrimaryStatsComponent.h"

bool UMT2InventoryComponent::EquipItemFromSlot(int32 InventorySlot)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() ||
		!Slots.IsValidIndex(InventorySlot) || Slots[InventorySlot].IsEmpty())
	{
		return false;
	}
	const UMT2ItemTemplate* Template = ResolveTemplate(Slots[InventorySlot].Vnum);
	if (!CanEquipTemplate(Template))
	{
		return false;
	}
	const int32 WearPosition = GetWearPosition(Slots[InventorySlot].Vnum);
	if (!Equipment.IsValidIndex(WearPosition))
	{
		return false;
	}

	const FMT2ItemSlot PreviouslyEquipped = Equipment[WearPosition];
	if (!PreviouslyEquipped.IsEmpty() &&
		!CanPlaceAt(InventorySlot, GetItemSize(PreviouslyEquipped.Vnum), InventorySlot))
	{
		return false;
	}
	Equipment[WearPosition] = Slots[InventorySlot];
	Slots[InventorySlot] = PreviouslyEquipped;
	OnInventoryChanged.Broadcast();
	OnEquipmentChanged.Broadcast();
	StartAutoRecoveryTimerIfNeeded();
	return true;
}

bool UMT2InventoryComponent::CanEquipTemplate(const UMT2ItemTemplate* Template) const
{
	if (!Cast<UMT2ItemEquipmentTemplate>(Template))
	{
		return false;
	}
	const AMT2PlayerCharacter* Character = Cast<AMT2PlayerCharacter>(GetOwner());
	const AMT2PlayerState* State = Character ? Character->GetPlayerState<AMT2PlayerState>() : nullptr;
	if (!State)
	{
		return true;
	}

	const FMT2CharacterAppearance& Appearance = State->GetCharacterAppearance();
	constexpr int32 AntiFemale = 1 << 0;
	constexpr int32 AntiMale = 1 << 1;
	if ((Appearance.Sex == EMT2CharacterSex::Female && (Template->AntiFlags & AntiFemale) != 0) ||
		(Appearance.Sex == EMT2CharacterSex::Male && (Template->AntiFlags & AntiMale) != 0))
	{
		return false;
	}
	const int32 RaceAntiFlag = 1 << (2 + static_cast<int32>(Appearance.Race));
	if ((Template->AntiFlags & RaceAntiFlag) != 0)
	{
		return false;
	}

	const UMT2PrimaryStatsComponent* PrimaryStats = Character->GetPrimaryStatsComponent();
	const FMT2PrimaryStats Stats = PrimaryStats
		? PrimaryStats->GetCalculatedStats() : FMT2PrimaryStats();
	for (const FMT2ItemLimit& Limit : Template->Limits)
	{
		const bool bSatisfied =
			(Limit.Type == EMT2ItemLimitType::Level && State->GetCharacterLevel() >= Limit.Value) ||
			(Limit.Type == EMT2ItemLimitType::Strength && Stats.Strength >= Limit.Value) ||
			(Limit.Type == EMT2ItemLimitType::Dexterity && Stats.Dexterity >= Limit.Value) ||
			(Limit.Type == EMT2ItemLimitType::Intelligence && Stats.Intelligence >= Limit.Value) ||
			(Limit.Type == EMT2ItemLimitType::Constitution && Stats.Constitution >= Limit.Value) ||
			Limit.Type == EMT2ItemLimitType::None ||
			static_cast<uint8>(Limit.Type) > static_cast<uint8>(EMT2ItemLimitType::Constitution);
		if (!bSatisfied)
		{
			return false;
		}
	}
	return true;
}

bool UMT2InventoryComponent::UnequipItem(int32 WearPosition)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() ||
		!Equipment.IsValidIndex(WearPosition) || Equipment[WearPosition].IsEmpty())
	{
		return false;
	}
	const int32 Size = GetItemSize(Equipment[WearPosition].Vnum);
	for (int32 TopSlot = 0; TopSlot < Slots.Num(); ++TopSlot)
	{
		if (Slots[TopSlot].IsEmpty() && CanPlaceAt(TopSlot, Size))
		{
			Slots[TopSlot] = Equipment[WearPosition];
			Equipment[WearPosition] = FMT2ItemSlot();
			OnInventoryChanged.Broadcast();
			OnEquipmentChanged.Broadcast();
			return true;
		}
	}
	return false;
}

bool UMT2InventoryComponent::UnequipAllItems()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	TArray<FMT2ItemSlot> NewSlots = Slots;
	TArray<FMT2ItemSlot> NewEquipment = Equipment;
	auto IsOccupied = [this, &NewSlots](int32 Cell)
	{
		for (int32 Anchor = 0; Anchor < NewSlots.Num(); ++Anchor)
		{
			if (NewSlots[Anchor].IsEmpty()) continue;
			for (int32 Offset = 0; Offset < GetItemSize(NewSlots[Anchor].Vnum); ++Offset)
			{
				if (Anchor + Offset * MT2InventoryLayout::Columns == Cell) return true;
			}
		}
		return false;
	};
	auto CanPlace = [&NewSlots, &IsOccupied](int32 TopSlot, int32 Size)
	{
		if (!NewSlots.IsValidIndex(TopSlot) ||
			(TopSlot % PageSlotCount) / MT2InventoryLayout::Columns + Size > MT2InventoryLayout::Rows)
		{
			return false;
		}
		for (int32 Offset = 0; Offset < Size; ++Offset)
		{
			const int32 Cell = TopSlot + Offset * MT2InventoryLayout::Columns;
			if (!NewSlots.IsValidIndex(Cell) || IsOccupied(Cell)) return false;
		}
		return true;
	};

	bool bHadEquipment = false;
	for (FMT2ItemSlot& EquippedItem : NewEquipment)
	{
		if (EquippedItem.IsEmpty()) continue;
		bHadEquipment = true;
		const int32 Size = GetItemSize(EquippedItem.Vnum);
		int32 Destination = INDEX_NONE;
		for (int32 SlotIndex = 0; SlotIndex < NewSlots.Num(); ++SlotIndex)
		{
			if (NewSlots[SlotIndex].IsEmpty() && CanPlace(SlotIndex, Size))
			{
				Destination = SlotIndex;
				break;
			}
		}
		if (Destination == INDEX_NONE) return false;
		NewSlots[Destination] = EquippedItem;
		EquippedItem = FMT2ItemSlot();
	}

	if (bHadEquipment)
	{
		Slots = MoveTemp(NewSlots);
		Equipment = MoveTemp(NewEquipment);
		OnInventoryChanged.Broadcast();
		OnEquipmentChanged.Broadcast();
	}
	return true;
}

bool UMT2InventoryComponent::ExtractRandomEquippedItem(FMT2ItemSlot& OutItem)
{
	OutItem = FMT2ItemSlot();
	if (!GetOwner() || !GetOwner()->HasAuthority()) return false;
	TArray<int32> OccupiedSlots;
	for (int32 Index = 0; Index < Equipment.Num(); ++Index)
	{
		if (!Equipment[Index].IsEmpty()) OccupiedSlots.Add(Index);
	}
	if (OccupiedSlots.IsEmpty()) return false;
	const int32 WearPosition = OccupiedSlots[FMath::RandRange(0, OccupiedSlots.Num() - 1)];
	OutItem = Equipment[WearPosition];
	Equipment[WearPosition] = FMT2ItemSlot();
	OnEquipmentChanged.Broadcast();
	return true;
}
