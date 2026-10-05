/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "UI/MT2UserWidget.h"
#include "CoreMinimal.h"
#include "Items/MT2ItemTypes.h"
#include "UI/MT2InventorySlotWidget.h"
#include "MT2InventoryGridWidget.generated.h"

UCLASS()
class METIN2_API UMT2InventoryGridWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Inventory Grid") FMT2InventorySlotClickedSignature OnSlotClicked;
	UPROPERTY(BlueprintAssignable, Category = "Inventory Grid") FMT2InventorySlotClickedSignature OnSlotRightClicked;
	UPROPERTY(BlueprintAssignable, Category = "Inventory Grid") FMT2InventorySlotClickedSignature OnSlotDragStarted;
	UPROPERTY(BlueprintAssignable, Category = "Inventory Grid") FMT2InventorySlotDropSignature OnSlotDropped;
	UPROPERTY(BlueprintAssignable, Category = "Inventory Grid") FMT2InventorySlotDragCancelledSignature OnSlotDragCancelled;
	void InitializeGrid(int32 InColumns, int32 InRows, int32 InStartIndex);
	void SetStartIndex(int32 InStartIndex);

	// Fills the 45 bound slot widgets from Slots[PageStart .. PageStart+44], clearing any beyond range.
	void RefreshSlots(const TArray<FMT2ItemSlot>& InSlots, int32 PageStart);

protected:
	virtual void NativeConstruct() override;

private:
	UFUNCTION() void HandleSlotClicked(int32 SlotIndex, EMT2InventorySlotKind SlotKind);
	UFUNCTION() void HandleSlotRightClicked(int32 SlotIndex, EMT2InventorySlotKind SlotKind);
	UFUNCTION() void HandleSlotDragStarted(int32 SlotIndex, EMT2InventorySlotKind SlotKind);
	UFUNCTION() void HandleSlotDropped(int32 SourceSlotIndex, EMT2InventorySlotKind SourceKind, int32 TargetSlotIndex, EMT2InventorySlotKind TargetKind);
	UFUNCTION() void HandleSlotDragCancelled(int32 SlotIndex, EMT2InventorySlotKind SlotKind, FVector2D ScreenPosition);
	int32 ResolveItemSize(int32 Vnum) const;
	void BindSlots();
	int32 StartIndex = 0;
	UPROPERTY(Transient) TArray<TObjectPtr<UMT2InventorySlotWidget>> Slots;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot00;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot01;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot02;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot03;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot04;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot05;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot06;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot07;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot08;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot09;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot10;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot11;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot12;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot13;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot14;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot15;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot16;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot17;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot18;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot19;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot20;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot21;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot22;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot23;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot24;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot25;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot26;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot27;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot28;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot29;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot30;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot31;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot32;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot33;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot34;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot35;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot36;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot37;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot38;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot39;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot40;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot41;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot42;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot43;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> Slot44;
};
