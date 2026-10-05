/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2PartyInviteDialogWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Player/MT2PlayerController.h"

void UMT2PartyInviteDialogWidget::NativeConstruct()
{
	Super::NativeConstruct();
	AcceptButton->OnClicked.AddUniqueDynamic(
		this, &UMT2PartyInviteDialogWidget::HandleAcceptClicked);
	DeclineButton->OnClicked.AddUniqueDynamic(
		this, &UMT2PartyInviteDialogWidget::HandleDeclineClicked);
}

void UMT2PartyInviteDialogWidget::OpenInvite(int32 InviteId, const FString& InviterName)
{
	PendingInviteId = InviteId;
	MessageText->SetText(FText::Format(
		NSLOCTEXT("MT2Party", "InviteQuestion", "{0} invited you to a party."),
		FText::FromString(InviterName)));
	SetVisibility(ESlateVisibility::Visible);
}

void UMT2PartyInviteDialogWidget::HandleAcceptClicked()
{
	Respond(true);
}

void UMT2PartyInviteDialogWidget::HandleDeclineClicked()
{
	Respond(false);
}

void UMT2PartyInviteDialogWidget::Respond(bool bAccept)
{
	if (AMT2PlayerController* Controller = Cast<AMT2PlayerController>(GetOwningPlayer()))
	{
		Controller->RespondToPartyInvite(PendingInviteId, bAccept);
	}
	PendingInviteId = 0;
	// This dialog is a persistent BindWidget child of MT2GameHUD. Removing it detaches the only
	// instance permanently, so all later invitations are delivered to a widget no longer on screen.
	SetVisibility(ESlateVisibility::Collapsed);
}

