/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "UI/MT2DraggableWindowWidget.h"
#include "UI/MT2InventorySlotWidget.h"
#include "MT2InventoryWidget.generated.h"

class UButton;
class USoundBase;
class UMT2BoardWidget;
class UMT2CurrencyPanelWidget;
class UMT2EquipmentPanelWidget;
class UMT2InventoryComponent;
class UMT2InventoryGridWidget;
class UMT2ItemDropDialogWidget;
class UMT2TitleBarWidget;
class UMT2ItemTemplate;
class AMT2PlayerState;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMT2InventoryPageChangedSignature, int32, PageIndex);

UCLASS()
class METIN2_API UMT2InventoryWidget : public UMT2DraggableWindowWidget
{
	GENERATED_BODY()

public:
	UMT2InventoryWidget(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(BlueprintAssignable, Category = "Inventory") FMT2InventoryPageChangedSignature OnInventoryPageChanged;
	UPROPERTY(BlueprintAssignable, Category = "Inventory") FMT2InventorySlotClickedSignature OnSlotClicked;
	UPROPERTY(BlueprintAssignable, Category = "Inventory") FMT2InventorySlotDropSignature OnSlotDropped;
	UFUNCTION(BlueprintCallable, Category = "Inventory") void ToggleInventory();
	// Shows the inventory (no-op if already open); used when opening a shop.
	UFUNCTION(BlueprintCallable, Category = "Inventory") void OpenInventory();
	UFUNCTION(BlueprintCallable, Category = "Inventory") void SetInventoryPage(int32 PageIndex);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	UFUNCTION() void HandleCloseClicked();
	UFUNCTION() void HandlePageOneClicked();
	UFUNCTION() void HandlePageTwoClicked();
	UFUNCTION() void HandlePageThreeClicked();
	UFUNCTION() void HandlePageFourClicked();
	UFUNCTION() void HandleSlotClicked(int32 SlotIndex, EMT2InventorySlotKind SlotKind);
	UFUNCTION() void HandleGridSlotRightClicked(int32 SlotIndex, EMT2InventorySlotKind SlotKind);
	UFUNCTION() void HandleEquipSlotRightClicked(int32 SlotIndex, EMT2InventorySlotKind SlotKind);
	UFUNCTION() void HandleSlotDragStarted(int32 SlotIndex, EMT2InventorySlotKind SlotKind);
public:
	// Opens the drop-count dialog for an inventory slot; called when a carried item is clicked
	// onto the ground (the carry system replaced the old hold-drag drop).
	void OpenDropDialogForSlot(int32 SlotIndex);
	void OpenMetinAttachDialog(
		int32 StoneSlot, int32 TargetSlot, bool bTargetEquipped);

private:
	UFUNCTION() void HandleSlotDropped(int32 SourceSlotIndex, EMT2InventorySlotKind SourceKind, int32 TargetSlotIndex, EMT2InventorySlotKind TargetKind);
	void RefreshPageSlots();
	void RefreshEquipment();
	void RefreshCurrency();

	// Resolves and caches the local player's inventory component, binding OnInventoryChanged /
	// OnEquipmentChanged the first time it's available so the UI refreshes on any change (incl. replication).
	UMT2InventoryComponent* ResolveInventoryComponent();
	class AMT2PlayerCharacter* GetOwningMT2Character() const;
	const UMT2ItemTemplate* ResolveItemTemplate(int32 SlotIndex, EMT2InventorySlotKind SlotKind);
	const UMT2ItemTemplate* ResolveItemTemplateByVnum(int32 Vnum) const;
	void PlayItemSound(const UMT2ItemTemplate* Template, bool bUseSound) const;
	void PlaySoundAsset(const TSoftObjectPtr<USoundBase>& Sound) const;
	UFUNCTION() void HandleInventoryActionConfirmed(int32 Vnum, bool bUseSound);

	UFUNCTION()
	void HandleInventoryChanged();

	UFUNCTION()
	void HandleEquipmentChanged();

	UFUNCTION()
	void HandleYangChanged(int64 OldYang, int64 NewYang);

	int32 ActivePage = 0;
	TWeakObjectPtr<UMT2InventoryComponent> BoundInventory;
	TWeakObjectPtr<AMT2PlayerState> BoundPlayerState;

	UPROPERTY(EditDefaultsOnly, Category = "Inventory|Sounds")
	TSoftObjectPtr<USoundBase> PickupItemSound;
	UPROPERTY(EditDefaultsOnly, Category = "Inventory|Sounds")
	TSoftObjectPtr<USoundBase> DefaultItemSound;
	UPROPERTY(EditDefaultsOnly, Category = "Inventory|Sounds")
	TSoftObjectPtr<USoundBase> ArmorItemSound;
	UPROPERTY(EditDefaultsOnly, Category = "Inventory|Sounds")
	TSoftObjectPtr<USoundBase> WeaponItemSound;
	UPROPERTY(EditDefaultsOnly, Category = "Inventory|Sounds")
	TSoftObjectPtr<USoundBase> BowItemSound;
	UPROPERTY(EditDefaultsOnly, Category = "Inventory|Sounds")
	TSoftObjectPtr<USoundBase> AccessoryItemSound;
	UPROPERTY(EditDefaultsOnly, Category = "Inventory|Sounds")
	TSoftObjectPtr<USoundBase> PotionItemSound;
	UPROPERTY(EditDefaultsOnly, Category = "Inventory|Sounds")
	TSoftObjectPtr<USoundBase> PortalItemSound;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> PageButtons;
	UPROPERTY(Transient) TObjectPtr<UMT2ItemDropDialogWidget> DropDialogWidget;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2BoardWidget> BoardWidget;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2TitleBarWidget> TitleBarWidget;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2EquipmentPanelWidget> EquipmentPanelWidget;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventoryGridWidget> InventoryGridWidget;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2CurrencyPanelWidget> CurrencyPanelWidget;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> InventoryPageOneButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> InventoryPageTwoButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> InventoryPageThreeButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> InventoryPageFourButton;
};
