/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "UI/MT2UserWidget.h"
#include "CoreMinimal.h"
#include "MT2ItemDropDialogWidget.generated.h"

class UButton;
class UMT2BoardWidget;
class UTextBlock;

UCLASS()
class METIN2_API UMT2ItemDropDialogWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	void OpenDropDialog(int32 InventorySlot, int32 Vnum, int32 Count);
	void OpenMetinAttachDialog(
		int32 StoneSlot, int32 TargetSlot, bool bTargetEquipped,
		const FText& StoneName, const FText& TargetName);

protected:
	virtual void NativeConstruct() override;

private:
	UFUNCTION() void HandleConfirmClicked();
	UFUNCTION() void HandleCancelClicked();
	void CloseDialog();

	int32 PendingInventorySlot = INDEX_NONE;
	int32 PendingVnum = 0;
	int32 PendingCount = 0;
	int32 PendingTargetSlot = INDEX_NONE;
	bool bPendingMetinAttach = false;
	bool bPendingTargetEquipped = false;

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2BoardWidget> BoardWidget;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> MessageText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> ConfirmButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> CancelButton;
};
