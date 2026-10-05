/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "UI/MT2UserWidget.h"
#include "MT2FriendAddDialogWidget.generated.h"

class UButton;
class UEditableTextBox;
class UTextBlock;

// The old MessengerWindow opens uiCommon.InputDialog for Add Friend. Keeping it as a separate
// widget preserves the compact messenger board and makes the prompt easy to restyle in Blueprint.
UCLASS()
class METIN2_API UMT2FriendAddDialogWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	void OpenDialog();

protected:
	virtual void NativeConstruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	UFUNCTION() void HandleConfirmClicked();
	UFUNCTION() void HandleCancelClicked();
	UFUNCTION() void HandleNameCommitted(const FText& Text, ETextCommit::Type CommitMethod);
	void Submit();

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> TitleText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UEditableTextBox> NameBox;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> ConfirmButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> CancelButton;
};
