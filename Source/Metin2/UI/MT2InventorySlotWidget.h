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
#include "MT2InventorySlotWidget.generated.h"

class UButton;
class UImage;
class UTextBlock;
class UTexture2D;
class UMT2AtlasImage;
class UMT2SlotEffectWidget;

UENUM(BlueprintType)
enum class EMT2InventorySlotKind : uint8
{
	Item,
	Equipment,
	Quick
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMT2InventorySlotClickedSignature, int32, SlotIndex, EMT2InventorySlotKind, SlotKind);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FMT2InventorySlotDropSignature, int32, SourceSlotIndex, EMT2InventorySlotKind, SourceKind, int32, TargetSlotIndex, EMT2InventorySlotKind, TargetKind);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMT2InventorySlotDragCancelledSignature, int32, SlotIndex, EMT2InventorySlotKind, SlotKind, FVector2D, ScreenPosition);

UCLASS()
class METIN2_API UMT2InventorySlotWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Inventory Slot")
	FMT2InventorySlotClickedSignature OnSlotClicked;

	UPROPERTY(BlueprintAssignable, Category = "Inventory Slot")
	FMT2InventorySlotClickedSignature OnSlotRightClicked;

	UPROPERTY(BlueprintAssignable, Category = "Inventory Slot")
	FMT2InventorySlotClickedSignature OnSlotDragStarted;

	UPROPERTY(BlueprintAssignable, Category = "Inventory Slot")
	FMT2InventorySlotDropSignature OnSlotDropped;

	UPROPERTY(BlueprintAssignable, Category = "Inventory Slot")
	FMT2InventorySlotDragCancelledSignature OnSlotDragCancelled;

	UFUNCTION(BlueprintCallable, Category = "Inventory Slot")
	void InitializeSlot(int32 InSlotIndex, EMT2InventorySlotKind InSlotKind, const FText& InLabel);

	UFUNCTION(BlueprintCallable, Category = "Inventory Slot")
	void SetItemVisual(UTexture2D* InIcon, int32 InCount, FName InItemId);

	// Resolves the item template for Vnum via the VNUM registry, loads its icon, and displays it (or
	// clears the slot when Vnum <= 0). This is the bridge from replicated inventory data to the visuals.
	UFUNCTION(BlueprintCallable, Category = "Inventory Slot")
	void SetItemByVnum(int32 Vnum, int32 Count, const TArray<FMT2ItemBonus>& Bonuses,
		const TArray<FMT2MetinSocket>& MetinSockets, int32 SkillVnum = 0,
		int32 AutoRecoveryRemainingAmount = -1, bool bAutoRecoveryActive = false,
		bool bNewlyAcquired = false);
	void SetItemByVnum(int32 Vnum, int32 Count) { SetItemByVnum(Vnum, Count, {}, {}); }

	// Marks this cell as hidden under a taller item occupying the cell(s) above it, so the item's icon
	// (which overflows down from its top cell) shows through cleanly.
	UFUNCTION(BlueprintCallable, Category = "Inventory Slot")
	void SetCovered(bool bInCovered);

	UFUNCTION(BlueprintCallable, Category = "Inventory Slot")
	void SetCoveredByItem(int32 OwningSlotIndex, int32 Vnum, int32 Count,
		const TArray<FMT2ItemBonus>& Bonuses, const TArray<FMT2MetinSocket>& MetinSockets,
		int32 SkillVnum = 0, int32 AutoRecoveryRemainingAmount = -1,
		bool bAutoRecoveryActive = false, bool bNewlyAcquired = false);

	UFUNCTION(BlueprintPure, Category = "Inventory Slot")
	FName GetItemId() const { return ItemId; }

	UFUNCTION(BlueprintPure, Category = "Inventory Slot")
	int32 GetSlotIndex() const { return InteractionSlotIndex; }

	UFUNCTION(BlueprintPure, Category = "Inventory Slot")
	EMT2InventorySlotKind GetSlotKind() const { return SlotKind; }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnPreviewMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation) override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual void NativeOnDragCancelled(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;

private:
	void RefreshVisuals();
	void EnsureSlotEffects();

	UPROPERTY(EditAnywhere, Category = "Inventory Slot")
	int32 SlotIndex = INDEX_NONE;

	UPROPERTY(Transient)
	int32 InteractionSlotIndex = INDEX_NONE;

	UPROPERTY(EditAnywhere, Category = "Inventory Slot")
	EMT2InventorySlotKind SlotKind = EMT2InventorySlotKind::Item;

	UPROPERTY(EditAnywhere, Category = "Inventory Slot")
	FText Label;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UMT2AtlasImage> SlotBackground;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> IconImage;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> LabelText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> CountText;

	// Transparent full-cell input surface. Required in the Widget Blueprint so interaction never
	// depends on the opaque pixels of an icon or a small text child.
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> HitButton;

	UPROPERTY(Transient)
	FName ItemId = NAME_None;
	UPROPERTY(Transient) TObjectPtr<UMT2SlotEffectWidget> ActiveEffect;
	UPROPERTY(Transient) TObjectPtr<UMT2SlotEffectWidget> NewItemEffect;
	bool bCurrentItemNew = false;
};
