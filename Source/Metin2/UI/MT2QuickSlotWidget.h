/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "UI/MT2UserWidget.h"
#include "CoreMinimal.h"
#include "MT2QuickSlotWidget.generated.h"

class UImage;
class UMT2SkillDefinition;
class UMT2SkillSlotWidget;
class UTextBlock;
class UTexture2D;
class UMT2SlotEffectWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMT2QuickSlotActivatedSignature, int32, SlotIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMT2QuickSlotDropSignature, int32, SourceSlotIndex, int32, TargetSlotIndex);

UCLASS()
class METIN2_API UMT2QuickSlotWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Quick Slot")
	FMT2QuickSlotActivatedSignature OnActivated;

	UPROPERTY(BlueprintAssignable, Category = "Quick Slot")
	FMT2QuickSlotDropSignature OnSlotDropped;

	// Right click clears the binding, like dragging it off the bar in the old client.
	UPROPERTY(BlueprintAssignable, Category = "Quick Slot")
	FMT2QuickSlotActivatedSignature OnRightClicked;

	void InitializeQuickSlot(int32 InSlotIndex, const FText& InHotkeyLabel, UTexture2D* InSlotTexture);

	UFUNCTION(BlueprintCallable, Category = "Quick Slot")
	void Activate();

	UFUNCTION(BlueprintCallable, Category = "Quick Slot")
	void SetIcon(UTexture2D* InIcon);

	// Arbitrary brush (skill icons live in atlas sub-rects, not standalone textures).
	void SetIconBrush(const FSlateBrush& Brush);
	void ClearIcon();

	// Skill bindings render through the same cell the skill window uses, so the bar shows the
	// identical icon, level text, cooldown drain and tooltip. Pass null to clear (item bindings and
	// empty cells).
	void SetSkillBinding(const UMT2SkillDefinition* Definition, int32 SkillLevel);

	UFUNCTION(BlueprintCallable, Category = "Quick Slot")
	void SetStackCount(int32 InCount);
	void SetItemActive(bool bActive);

	UFUNCTION(BlueprintPure, Category = "Quick Slot")
	int32 GetSlotIndex() const { return SlotIndex; }

protected:
	virtual void NativeConstruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation) override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;

private:
	int32 SlotIndex = INDEX_NONE;
	FText HotkeyLabel;
	TObjectPtr<UTexture2D> SlotTexture;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UBorder> SlotBackground;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> IconImage;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> HotkeyText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> CountText;

	void EnsureSkillSlot();

	UPROPERTY(Transient) TObjectPtr<UMT2SkillSlotWidget> SkillSlot;
	UPROPERTY(Transient) TObjectPtr<UMT2SlotEffectWidget> ItemActiveEffect;
	int32 SkillBindingVnum = 0;
};
