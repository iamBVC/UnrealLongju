/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2InventoryGridWidget.h"

#include "Components/CanvasPanelSlot.h"
#include "Items/MT2ItemUtils.h"

void UMT2InventoryGridWidget::InitializeGrid(int32 InColumns, int32 InRows, int32 InStartIndex)
{
	ensureMsgf(InColumns == 5 && InRows == 9, TEXT("MT2 inventory BP grid is fixed at 5x9."));
	SetStartIndex(InStartIndex);
}

void UMT2InventoryGridWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BindSlots();
	SetStartIndex(StartIndex);
}

void UMT2InventoryGridWidget::BindSlots()
{
	Slots = {Slot00, Slot01, Slot02, Slot03, Slot04, Slot05, Slot06, Slot07, Slot08, Slot09, Slot10, Slot11, Slot12, Slot13, Slot14, Slot15, Slot16, Slot17, Slot18, Slot19, Slot20, Slot21, Slot22, Slot23, Slot24, Slot25, Slot26, Slot27, Slot28, Slot29, Slot30, Slot31, Slot32, Slot33, Slot34, Slot35, Slot36, Slot37, Slot38, Slot39, Slot40, Slot41, Slot42, Slot43, Slot44};
	for (UMT2InventorySlotWidget* InventorySlot : Slots)
	{
		InventorySlot->OnSlotClicked.AddUniqueDynamic(this, &UMT2InventoryGridWidget::HandleSlotClicked);
		InventorySlot->OnSlotRightClicked.AddUniqueDynamic(this, &UMT2InventoryGridWidget::HandleSlotRightClicked);
		InventorySlot->OnSlotDragStarted.AddUniqueDynamic(this, &UMT2InventoryGridWidget::HandleSlotDragStarted);
		InventorySlot->OnSlotDropped.AddUniqueDynamic(this, &UMT2InventoryGridWidget::HandleSlotDropped);
		InventorySlot->OnSlotDragCancelled.AddUniqueDynamic(this, &UMT2InventoryGridWidget::HandleSlotDragCancelled);
	}
}

int32 UMT2InventoryGridWidget::ResolveItemSize(int32 Vnum) const
{
	return MT2ItemUtils::GetInventorySize(this, Vnum);
}

void UMT2InventoryGridWidget::SetStartIndex(int32 InStartIndex)
{
	StartIndex = FMath::Max(0, InStartIndex);
	for (int32 LocalIndex = 0; LocalIndex < Slots.Num(); ++LocalIndex)
	{
		Slots[LocalIndex]->InitializeSlot(StartIndex + LocalIndex, EMT2InventorySlotKind::Item, FText::GetEmpty());
	}
}

void UMT2InventoryGridWidget::RefreshSlots(const TArray<FMT2ItemSlot>& InSlots, int32 PageStart)
{
	constexpr int32 NumColumns = 5;
	if (Slots.IsEmpty())
	{
		BindSlots();
	}

	// A multi-cell item is stored only at its top cell; mark the cells it covers below so they render
	// transparently and its overflowing icon shows through.
	TArray<int32> CoveringItem;
	CoveringItem.Init(INDEX_NONE, Slots.Num());
	for (int32 LocalIndex = 0; LocalIndex < Slots.Num(); ++LocalIndex)
	{
		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slots[LocalIndex]->Slot))
		{
			CanvasSlot->SetZOrder(0);
		}
		const int32 DataIndex = PageStart + LocalIndex;
		if (InSlots.IsValidIndex(DataIndex) && !InSlots[DataIndex].IsEmpty())
		{
			const int32 Size = ResolveItemSize(InSlots[DataIndex].Vnum);
			if (Size > 1)
			{
				if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slots[LocalIndex]->Slot))
				{
					CanvasSlot->SetZOrder(1);
				}
			}
			for (int32 Cell = 1; Cell < Size; ++Cell)
			{
				const int32 Below = LocalIndex + Cell * NumColumns;
				if (Slots.IsValidIndex(Below))
				{
					CoveringItem[Below] = LocalIndex;
				}
			}
		}
	}

	for (int32 LocalIndex = 0; LocalIndex < Slots.Num(); ++LocalIndex)
	{
		if (CoveringItem[LocalIndex] != INDEX_NONE)
		{
			const int32 OwnerLocalIndex = CoveringItem[LocalIndex];
			const int32 OwnerDataIndex = PageStart + OwnerLocalIndex;
			const FMT2ItemSlot& OwnerItem = InSlots[OwnerDataIndex];
			Slots[LocalIndex]->SetCoveredByItem(
				StartIndex + OwnerLocalIndex, OwnerItem.Vnum, OwnerItem.Count,
				OwnerItem.Bonuses, OwnerItem.MetinSockets, OwnerItem.SkillVnum,
				OwnerItem.AutoRecoveryRemainingAmount, OwnerItem.bAutoRecoveryActive,
				OwnerItem.bNewlyAcquired);
			continue;
		}
		const int32 DataIndex = PageStart + LocalIndex;
		if (InSlots.IsValidIndex(DataIndex) && !InSlots[DataIndex].IsEmpty())
		{
			Slots[LocalIndex]->SetItemByVnum(
				InSlots[DataIndex].Vnum, InSlots[DataIndex].Count,
				InSlots[DataIndex].Bonuses, InSlots[DataIndex].MetinSockets,
				InSlots[DataIndex].SkillVnum,
				InSlots[DataIndex].AutoRecoveryRemainingAmount,
				InSlots[DataIndex].bAutoRecoveryActive,
				InSlots[DataIndex].bNewlyAcquired);
		}
		else
		{
			Slots[LocalIndex]->SetItemByVnum(0, 0);
		}
	}
}

void UMT2InventoryGridWidget::HandleSlotClicked(int32 SlotIndex, EMT2InventorySlotKind SlotKind)
{
	OnSlotClicked.Broadcast(SlotIndex, SlotKind);
}

void UMT2InventoryGridWidget::HandleSlotRightClicked(int32 SlotIndex, EMT2InventorySlotKind SlotKind)
{
	OnSlotRightClicked.Broadcast(SlotIndex, SlotKind);
}

void UMT2InventoryGridWidget::HandleSlotDragStarted(int32 SlotIndex, EMT2InventorySlotKind SlotKind)
{
	OnSlotDragStarted.Broadcast(SlotIndex, SlotKind);
}

void UMT2InventoryGridWidget::HandleSlotDragCancelled(
	int32 SlotIndex, EMT2InventorySlotKind SlotKind, FVector2D ScreenPosition)
{
	OnSlotDragCancelled.Broadcast(SlotIndex, SlotKind, ScreenPosition);
}

void UMT2InventoryGridWidget::HandleSlotDropped(int32 SourceSlotIndex, EMT2InventorySlotKind SourceKind, int32 TargetSlotIndex, EMT2InventorySlotKind TargetKind)
{
	OnSlotDropped.Broadcast(SourceSlotIndex, SourceKind, TargetSlotIndex, TargetKind);
}
