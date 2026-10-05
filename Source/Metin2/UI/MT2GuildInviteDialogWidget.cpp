/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2GuildInviteDialogWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Guild/MT2GuildComponent.h"
#include "Player/MT2PlayerState.h"

void UMT2GuildInviteDialogWidget::NativeConstruct()
{
	Super::NativeConstruct();
	AcceptButton->OnClicked.AddUniqueDynamic(this, &UMT2GuildInviteDialogWidget::HandleAccept);
	DeclineButton->OnClicked.AddUniqueDynamic(this, &UMT2GuildInviteDialogWidget::HandleDecline);
	SetVisibility(ESlateVisibility::Collapsed);
}

UMT2GuildComponent* UMT2GuildInviteDialogWidget::ResolveGuild() const
{
	const AMT2PlayerState* State = GetOwningPlayerState<AMT2PlayerState>();
	return State ? State->GetGuildComponent() : nullptr;
}

void UMT2GuildInviteDialogWidget::OpenInvite(const FString& InviterName, const FString& GuildName)
{
	MessageText->SetText(FText::Format(
		NSLOCTEXT("MT2Guild", "Invite", "{0} invited you to join {1}."),
		FText::FromString(InviterName), FText::FromString(GuildName)));
	SetVisibility(ESlateVisibility::Visible);
}

void UMT2GuildInviteDialogWidget::HandleAccept() { Respond(true); }
void UMT2GuildInviteDialogWidget::HandleDecline() { Respond(false); }
void UMT2GuildInviteDialogWidget::Respond(bool bAccept)
{
	if (UMT2GuildComponent* Guild = ResolveGuild()) Guild->AnswerInvite(bAccept);
	SetVisibility(ESlateVisibility::Collapsed);
}
