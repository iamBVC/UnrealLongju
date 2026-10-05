/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Items/MT2ItemTypes.h"
#include "UI/MT2UserWidget.h"
#include "MT2RefinementDialogWidget.generated.h"

class AMT2Npc;
class UButton;
class UHorizontalBox;
class UTextBlock;
class UVerticalBox;

UCLASS()
class METIN2_API UMT2RefinementDialogWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	void OpenRefinement(int32 TargetSlot, int32 ScrollSlot, AMT2Npc* Blacksmith);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnPreviewKeyDown(
		const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	UFUNCTION()
	void HandleConfirmClicked();

	UFUNCTION()
	void HandleCancelClicked();

	void CloseDialog();
	void RebuildPreview();

	int32 PendingTargetSlot = INDEX_NONE;
	int32 PendingScrollSlot = INDEX_NONE;
	TWeakObjectPtr<AMT2Npc> PendingBlacksmith;
	bool bInteractionLocked = false;

	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> ComparisonBox;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> RequirementsBox;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ChanceText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FailureText;

	UPROPERTY(Transient)
	TObjectPtr<UButton> ConfirmButton;
};
