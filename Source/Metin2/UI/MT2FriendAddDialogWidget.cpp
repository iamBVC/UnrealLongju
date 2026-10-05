/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2FriendAddDialogWidget.h"

#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Messenger/MT2MessengerComponent.h"
#include "Player/MT2PlayerState.h"

void UMT2FriendAddDialogWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
	ConfirmButton->OnClicked.AddUniqueDynamic(this, &UMT2FriendAddDialogWidget::HandleConfirmClicked);
	CancelButton->OnClicked.AddUniqueDynamic(this, &UMT2FriendAddDialogWidget::HandleCancelClicked);
	NameBox->OnTextCommitted.AddUniqueDynamic(this, &UMT2FriendAddDialogWidget::HandleNameCommitted);
	SetVisibility(ESlateVisibility::Collapsed);
}

void UMT2FriendAddDialogWidget::OpenDialog()
{
	TitleText->SetText(NSLOCTEXT("MT2Messenger", "AddFriend", "Add Friend"));
	NameBox->SetText(FText::GetEmpty());
	SetVisibility(ESlateVisibility::Visible);
	NameBox->SetKeyboardFocus();
}

FReply UMT2FriendAddDialogWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		HandleCancelClicked();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UMT2FriendAddDialogWidget::HandleConfirmClicked() { Submit(); }
void UMT2FriendAddDialogWidget::HandleCancelClicked() { SetVisibility(ESlateVisibility::Collapsed); }

void UMT2FriendAddDialogWidget::HandleNameCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		Submit();
	}
}

void UMT2FriendAddDialogWidget::Submit()
{
	const FString TargetName = NameBox->GetText().ToString().TrimStartAndEnd();
	if (TargetName.IsEmpty())
	{
		NameBox->SetKeyboardFocus();
		return;
	}

	const APlayerController* Controller = GetOwningPlayer();
	AMT2PlayerState* PlayerState = Controller ? Controller->GetPlayerState<AMT2PlayerState>() : nullptr;
	if (UMT2MessengerComponent* Messenger = PlayerState ? PlayerState->GetMessengerComponent() : nullptr)
	{
		Messenger->RequestAddFriend(TargetName);
		SetVisibility(ESlateVisibility::Collapsed);
	}
}
