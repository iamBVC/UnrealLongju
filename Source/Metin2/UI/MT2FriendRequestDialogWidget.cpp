/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2FriendRequestDialogWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Messenger/MT2MessengerComponent.h"
#include "Player/MT2PlayerState.h"

void UMT2FriendRequestDialogWidget::NativeConstruct()
{
	Super::NativeConstruct();
	AcceptButton->OnClicked.AddUniqueDynamic(this, &UMT2FriendRequestDialogWidget::HandleAcceptClicked);
	DenyButton->OnClicked.AddUniqueDynamic(this, &UMT2FriendRequestDialogWidget::HandleDenyClicked);
	SetVisibility(ESlateVisibility::Collapsed);
}

void UMT2FriendRequestDialogWidget::OpenRequest(
	const FString& RequesterId, const FString& RequesterName)
{
	PendingRequesterId = RequesterId;
	MessageText->SetText(FText::FromString(
		FString::Printf(TEXT("%s sent you a friend request"), *RequesterName)));
	SetVisibility(ESlateVisibility::Visible);
}

void UMT2FriendRequestDialogWidget::HandleAcceptClicked() { Respond(true); }
void UMT2FriendRequestDialogWidget::HandleDenyClicked() { Respond(false); }

void UMT2FriendRequestDialogWidget::Respond(bool bAccept)
{
	const APlayerController* Controller = GetOwningPlayer();
	AMT2PlayerState* State = Controller ? Controller->GetPlayerState<AMT2PlayerState>() : nullptr;
	if (UMT2MessengerComponent* Messenger = State ? State->GetMessengerComponent() : nullptr)
	{
		Messenger->AnswerFriendRequest(PendingRequesterId, bAccept);
	}
	PendingRequesterId.Reset();
	SetVisibility(ESlateVisibility::Collapsed);
}
