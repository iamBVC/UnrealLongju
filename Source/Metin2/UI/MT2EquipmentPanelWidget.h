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
#include "MT2EquipmentPanelWidget.generated.h"

class UButton;
class UImage;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMT2EquipmentPageChangedSignature, int32, PageIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMT2EquipmentActionSignature);

UCLASS()
class METIN2_API UMT2EquipmentPanelWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Equipment") FMT2InventorySlotClickedSignature OnSlotClicked;
	UPROPERTY(BlueprintAssignable, Category = "Equipment") FMT2InventorySlotClickedSignature OnSlotRightClicked;
	UPROPERTY(BlueprintAssignable, Category = "Equipment") FMT2InventorySlotClickedSignature OnSlotDragStarted;
	UPROPERTY(BlueprintAssignable, Category = "Equipment") FMT2InventorySlotDropSignature OnSlotDropped;
	// Fills each equipment slot from Equipment[slot's wear position].
	void RefreshEquipment(const TArray<FMT2ItemSlot>& Equipment);
	UPROPERTY(BlueprintAssignable, Category = "Equipment") FMT2EquipmentPageChangedSignature OnPageChanged;
	UPROPERTY(BlueprintAssignable, Category = "Equipment") FMT2EquipmentActionSignature OnDragonSoulClicked;
	UPROPERTY(BlueprintAssignable, Category = "Equipment") FMT2EquipmentActionSignature OnMallClicked;
	UPROPERTY(BlueprintAssignable, Category = "Equipment") FMT2EquipmentActionSignature OnCostumeClicked;
	UFUNCTION(BlueprintCallable, Category = "Equipment") void SetEquipmentPage(int32 PageIndex);

protected:
	virtual void NativeConstruct() override;

private:
	UFUNCTION() void HandleSlotClicked(int32 SlotIndex, EMT2InventorySlotKind SlotKind);
	UFUNCTION() void HandleSlotRightClicked(int32 SlotIndex, EMT2InventorySlotKind SlotKind);
	UFUNCTION() void HandleSlotDragStarted(int32 SlotIndex, EMT2InventorySlotKind SlotKind);
	UFUNCTION() void HandleSlotDropped(int32 SourceSlotIndex, EMT2InventorySlotKind SourceKind, int32 TargetSlotIndex, EMT2InventorySlotKind TargetKind);
	UFUNCTION() void HandlePageOne();
	UFUNCTION() void HandlePageTwo();
	UFUNCTION() void HandleDragonSoul();
	UFUNCTION() void HandleMall();
	UFUNCTION() void HandleCostume();
	void RefreshTabs();
	int32 ActivePage = 0;
	UPROPERTY(Transient) TArray<TObjectPtr<UMT2InventorySlotWidget>> Slots;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> PageButtons;

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> EquipmentBackground;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> ArmorSlot;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> HeadSlot;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> BootsSlot;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> WristSlot;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> WeaponSlot;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> EarringSlot;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> NecklaceSlot;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> BraceletSlot;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> ShoesSlot;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> ArrowSlot;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> ShieldSlot;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> BeltSlot;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> ExtraSlot;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventorySlotWidget> GloveSlot;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> DragonSoulButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> MallButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> CostumeButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> EquipmentPageOneButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> EquipmentPageTwoButton;
};
