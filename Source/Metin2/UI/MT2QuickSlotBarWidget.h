/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "UI/MT2UserWidget.h"
#include "CoreMinimal.h"
#include "Items/MT2QuickSlotTypes.h"
#include "MT2QuickSlotBarWidget.generated.h"

class UButton;
class UImage;
class UTextBlock;
class UMT2InventoryComponent;
class UMT2QuickSlotWidget;
class UMT2SkillComponent;
class AMT2PlayerState;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMT2QuickBarSlotSignature, int32, SlotIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMT2QuickBarPageSignature, int32, PageIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMT2QuickBarChatSignature);

// The taskbar's 8-key quick bar (1-4, F1-F4, 4 pages). Slots hold item or skill bindings from the
// PlayerState: clicking with a carried icon binds it, clicking (or pressing the hotkey) uses the
// binding, right-clicking clears it.
UCLASS()
class METIN2_API UMT2QuickSlotBarWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Quick Bar") FMT2QuickBarSlotSignature OnSlotActivated;
	UPROPERTY(BlueprintAssignable, Category = "Quick Bar") FMT2QuickBarPageSignature OnPageChanged;
	UPROPERTY(BlueprintAssignable, Category = "Quick Bar") FMT2QuickBarChatSignature OnChatClicked;
	void ActivateSlot(int32 SlotIndex);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UFUNCTION() void HandleSlotActivated(int32 SlotIndex);
	UFUNCTION() void HandleSlotRightClicked(int32 SlotIndex);
	UFUNCTION() void HandleQuickSlotsChanged();
	UFUNCTION() void HandleInventoryChanged();
	UFUNCTION() void HandleSkillLevelsChanged();
	UFUNCTION() void HandlePreviousPage();
	UFUNCTION() void HandleNextPage();
	UFUNCTION() void HandleChat();
	void SetPage(int32 PageIndex);
	void BindPlayerState();
	void RefreshQuickSlots();
	// Executes the binding (skill cast / item use) - the hotkey path.
	void UseSlotBinding(int32 SlotIndex);
	bool MakeAssignmentBrush(const FMT2QuickSlotAssignment& Assignment, FSlateBrush& OutBrush) const;
	int32 ToGlobalSlotIndex(int32 SlotIndex) const;
	AMT2PlayerState* GetOwningMT2PlayerState() const;
	UMT2InventoryComponent* GetInventoryComponent() const;

	int32 ActivePage = 0;
	TWeakObjectPtr<AMT2PlayerState> BoundPlayerState;
	TWeakObjectPtr<UMT2InventoryComponent> BoundInventory;
	TWeakObjectPtr<UMT2SkillComponent> BoundSkillComponent;
	UPROPERTY(Transient) TArray<TObjectPtr<UMT2QuickSlotWidget>> Slots;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2QuickSlotWidget> QuickSlot1;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2QuickSlotWidget> QuickSlot2;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2QuickSlotWidget> QuickSlot3;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2QuickSlotWidget> QuickSlot4;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2QuickSlotWidget> QuickSlot5;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2QuickSlotWidget> QuickSlot6;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2QuickSlotWidget> QuickSlot7;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2QuickSlotWidget> QuickSlot8;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> ChatButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> PreviousPageButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> NextPageButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> PageBackground;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> PageText;
};
