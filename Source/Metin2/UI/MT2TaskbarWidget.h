/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "UI/MT2UserWidget.h"
#include "CoreMinimal.h"
#include "MT2TaskbarWidget.generated.h"

class UButton;
class UImage;
class UMT2ExperienceGaugeWidget;
class UMT2QuickSlotBarWidget;
class UMT2ResourceGaugeWidget;
class AMT2PlayerState;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMT2TaskbarButtonSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMT2QuickSlotSignature, int32, SlotIndex);

UCLASS()
class METIN2_API UMT2TaskbarWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Taskbar") FMT2TaskbarButtonSignature OnInventoryClicked;
	UPROPERTY(BlueprintAssignable, Category = "Taskbar") FMT2TaskbarButtonSignature OnCharacterClicked;
	UPROPERTY(BlueprintAssignable, Category = "Taskbar") FMT2TaskbarButtonSignature OnMessengerClicked;
	UPROPERTY(BlueprintAssignable, Category = "Taskbar") FMT2TaskbarButtonSignature OnSystemClicked;
	UPROPERTY(BlueprintAssignable, Category = "Taskbar") FMT2TaskbarButtonSignature OnChatClicked;
	UPROPERTY(BlueprintAssignable, Category = "Taskbar") FMT2TaskbarButtonSignature OnMoneyClicked;
	UPROPERTY(BlueprintAssignable, Category = "Taskbar") FMT2QuickSlotSignature OnQuickSlotActivated;
	UFUNCTION(BlueprintCallable, Category = "Taskbar") void ActivateQuickSlot(int32 SlotIndex);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UFUNCTION() void HandleInventoryClicked();
	UFUNCTION() void HandleCharacterClicked();
	UFUNCTION() void HandleMessengerClicked();
	UFUNCTION() void HandleSystemClicked();
	UFUNCTION() void HandleChatClicked();
	UFUNCTION() void HandleMoneyClicked();
	UFUNCTION() void HandleQuickSlotActivated(int32 SlotIndex);
	UFUNCTION() void HandleExperienceChanged(int64 OldExperience, int64 NewExperience);
	UFUNCTION() void HandleLevelChanged(int32 OldLevel, int32 NewLevel);
	void BindPlayerState();
	void UnbindPlayerState();
	void RefreshResourceBars();
	void RefreshExperienceBar();
	TWeakObjectPtr<AMT2PlayerState> BoundPlayerState;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> TaskbarBase;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2ResourceGaugeWidget> ResourceGaugeWidget;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2ExperienceGaugeWidget> ExperienceGaugeWidget;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2QuickSlotBarWidget> QuickSlotBarWidget;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> LeftMouseButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> RightMouseButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> MoneyButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> CharacterButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> InventoryButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> MessengerButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> SystemButton;
};
