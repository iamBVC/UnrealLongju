/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2EquipmentPanelWidget.h"

#include "Components/Button.h"

void UMT2EquipmentPanelWidget::NativeConstruct()
{
	Super::NativeConstruct();
	Slots = {ArmorSlot, HeadSlot, BootsSlot, WristSlot, WeaponSlot, EarringSlot, NecklaceSlot,
		BraceletSlot, ShoesSlot, ArrowSlot, ShieldSlot, BeltSlot, ExtraSlot, GloveSlot};
	static const int32 SlotIds[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 27, 28};
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		Slots[Index]->InitializeSlot(SlotIds[Index], EMT2InventorySlotKind::Equipment, FText::GetEmpty());
		Slots[Index]->OnSlotClicked.AddUniqueDynamic(this, &UMT2EquipmentPanelWidget::HandleSlotClicked);
		Slots[Index]->OnSlotRightClicked.AddUniqueDynamic(this, &UMT2EquipmentPanelWidget::HandleSlotRightClicked);
		Slots[Index]->OnSlotDragStarted.AddUniqueDynamic(this, &UMT2EquipmentPanelWidget::HandleSlotDragStarted);
		Slots[Index]->OnSlotDropped.AddUniqueDynamic(this, &UMT2EquipmentPanelWidget::HandleSlotDropped);
	}
	PageButtons = {EquipmentPageOneButton, EquipmentPageTwoButton};
	EquipmentPageOneButton->OnClicked.AddUniqueDynamic(this, &UMT2EquipmentPanelWidget::HandlePageOne);
	EquipmentPageTwoButton->OnClicked.AddUniqueDynamic(this, &UMT2EquipmentPanelWidget::HandlePageTwo);
	DragonSoulButton->OnClicked.AddUniqueDynamic(this, &UMT2EquipmentPanelWidget::HandleDragonSoul);
	MallButton->OnClicked.AddUniqueDynamic(this, &UMT2EquipmentPanelWidget::HandleMall);
	CostumeButton->OnClicked.AddUniqueDynamic(this, &UMT2EquipmentPanelWidget::HandleCostume);
	RefreshTabs();
}

void UMT2EquipmentPanelWidget::SetEquipmentPage(int32 PageIndex)
{
	ActivePage = FMath::Clamp(PageIndex, 0, 1);
	RefreshTabs();
	OnPageChanged.Broadcast(ActivePage);
}

void UMT2EquipmentPanelWidget::RefreshTabs()
{
	for (int32 Index = 0; Index < PageButtons.Num(); ++Index)
	{
		PageButtons[Index]->SetBackgroundColor(Index == ActivePage ? FLinearColor(1.0f, 0.86f, 0.55f, 1.0f) : FLinearColor::White);
	}
}

void UMT2EquipmentPanelWidget::RefreshEquipment(const TArray<FMT2ItemSlot>& Equipment)
{
	for (UMT2InventorySlotWidget* EquipSlot : Slots)
	{
		const int32 WearPosition = EquipSlot->GetSlotIndex();
		if (Equipment.IsValidIndex(WearPosition) && !Equipment[WearPosition].IsEmpty())
		{
			EquipSlot->SetItemByVnum(Equipment[WearPosition].Vnum,
				Equipment[WearPosition].Count, Equipment[WearPosition].Bonuses,
				Equipment[WearPosition].MetinSockets, Equipment[WearPosition].SkillVnum);
		}
		else
		{
			EquipSlot->SetItemByVnum(0, 0);
		}
	}
}

void UMT2EquipmentPanelWidget::HandleSlotClicked(int32 SlotIndex, EMT2InventorySlotKind SlotKind) { OnSlotClicked.Broadcast(SlotIndex, SlotKind); }
void UMT2EquipmentPanelWidget::HandleSlotRightClicked(int32 SlotIndex, EMT2InventorySlotKind SlotKind) { OnSlotRightClicked.Broadcast(SlotIndex, SlotKind); }
void UMT2EquipmentPanelWidget::HandleSlotDragStarted(int32 SlotIndex, EMT2InventorySlotKind SlotKind) { OnSlotDragStarted.Broadcast(SlotIndex, SlotKind); }
void UMT2EquipmentPanelWidget::HandleSlotDropped(int32 SourceSlotIndex, EMT2InventorySlotKind SourceKind, int32 TargetSlotIndex, EMT2InventorySlotKind TargetKind) { OnSlotDropped.Broadcast(SourceSlotIndex, SourceKind, TargetSlotIndex, TargetKind); }
void UMT2EquipmentPanelWidget::HandlePageOne() { SetEquipmentPage(0); }
void UMT2EquipmentPanelWidget::HandlePageTwo() { SetEquipmentPage(1); }
void UMT2EquipmentPanelWidget::HandleDragonSoul() { OnDragonSoulClicked.Broadcast(); }
void UMT2EquipmentPanelWidget::HandleMall() { OnMallClicked.Broadcast(); }
void UMT2EquipmentPanelWidget::HandleCostume() { OnCostumeClicked.Broadcast(); }
