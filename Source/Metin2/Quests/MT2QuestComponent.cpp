/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Quests/MT2QuestComponent.h"

#include "Characters/MT2PlayerCharacter.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "UI/MT2QuestDialogWidget.h"
#include "Player/MT2PlayerState.h"
#include "Quests/MT2QuestManagerComponent.h"

UMT2QuestComponent::UMT2QuestComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UMT2QuestComponent::ShowDialog(const FMT2DialogPayload& Payload, TFunction<void(int32)> OnAnswer)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	// Replacing a live conversation dismisses the old one (its continuation gets choice 0).
	if (PendingAnswerCallback)
	{
		TFunction<void(int32)> Dismissed = MoveTemp(PendingAnswerCallback);
		PendingAnswerCallback = nullptr;
		Dismissed(0);
	}
	if (PendingTextCallback)
	{
		TFunction<void(const FString&)> Dismissed = MoveTemp(PendingTextCallback);
		PendingTextCallback = nullptr;
		Dismissed(FString());
	}
	PendingAnswerCallback = MoveTemp(OnAnswer);
	ClientShowDialog(Payload);
}

void UMT2QuestComponent::ShowInputDialog(
	const FMT2DialogPayload& Payload, TFunction<void(const FString&)> OnAnswer)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	if (PendingAnswerCallback)
	{
		TFunction<void(int32)> Dismissed = MoveTemp(PendingAnswerCallback);
		PendingAnswerCallback = nullptr;
		Dismissed(0);
	}
	if (PendingTextCallback)
	{
		TFunction<void(const FString&)> Dismissed = MoveTemp(PendingTextCallback);
		PendingTextCallback = nullptr;
		Dismissed(FString());
	}
	PendingTextCallback = MoveTemp(OnAnswer);
	ClientShowDialog(Payload);
}

void UMT2QuestComponent::CloseDialog()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	PendingAnswerCallback = nullptr;
	PendingTextCallback = nullptr;
	ClientCloseDialog();
}

void UMT2QuestComponent::ServerAnswerDialog_Implementation(int32 ChoiceIndex)
{
	if (!PendingAnswerCallback)
	{
		if (ChoiceIndex == 0 && PendingTextCallback)
		{
			PendingTextCallback = nullptr;
			if (const AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner()))
			{
				if (UMT2QuestManagerComponent* Manager = State->GetQuestManagerComponent()) { Manager->CancelConversation(); }
			}
			CloseDialog();
		}
		return;
	}
	// The callback may immediately open the next dialog page (setting a new pending callback), so
	// detach before invoking - the select() resume.
	TFunction<void(int32)> Callback = MoveTemp(PendingAnswerCallback);
	PendingAnswerCallback = nullptr;
	Callback(FMath::Max(ChoiceIndex, 0));

	// Conversation ended (no further page opened): close the window and release the movement lock.
	if (!PendingAnswerCallback && !PendingTextCallback)
	{
		CloseDialog();
	}
}

void UMT2QuestComponent::ServerAnswerDialogText_Implementation(const FString& Text)
{
	if (!PendingTextCallback)
	{
		return;
	}
	TFunction<void(const FString&)> Callback = MoveTemp(PendingTextCallback);
	PendingTextCallback = nullptr;
	Callback(Text.Left(64));
	if (!PendingAnswerCallback && !PendingTextCallback)
	{
		CloseDialog();
	}
}

UMT2QuestDialogWidget* UMT2QuestComponent::GetOrCreateDialogWidget()
{
	if (DialogWidget)
	{
		return DialogWidget;
	}
	const APlayerState* State = Cast<APlayerState>(GetOwner());
	APlayerController* Controller = State
		? Cast<APlayerController>(State->GetOwningController()) : nullptr;
	if (!Controller || !Controller->IsLocalController())
	{
		return nullptr;
	}
	DialogWidget = CreateWidget<UMT2QuestDialogWidget>(Controller, UMT2QuestDialogWidget::StaticClass());
	if (DialogWidget)
	{
		DialogWidget->OnOptionSelected.AddUniqueDynamic(this, &UMT2QuestComponent::HandleDialogOptionSelected);
		DialogWidget->OnTextSubmitted.AddUniqueDynamic(this, &UMT2QuestComponent::HandleDialogTextSubmitted);
		DialogWidget->AddToViewport(50);
	}
	return DialogWidget;
}

void UMT2QuestComponent::ClientShowDialog_Implementation(FMT2DialogPayload Payload)
{
	if (UMT2QuestDialogWidget* Widget = GetOrCreateDialogWidget())
	{
		Widget->ShowDialog(Payload);
		if (const APlayerState* State = Cast<APlayerState>(GetOwner()))
		{
			if (AMT2PlayerCharacter* Player = Cast<AMT2PlayerCharacter>(State->GetPawn()))
			{
				// Freeze the character while a conversation is on screen (old client behavior).
				if (!bMovementLockedForDialog)
				{
					bMovementLockedForDialog = true;
					Player->SetInteractionUIOpen(true);
				}
			}
		}
	}
}

void UMT2QuestComponent::ClientCloseDialog_Implementation()
{
	if (DialogWidget)
	{
		DialogWidget->CloseDialog();
	}
	if (bMovementLockedForDialog)
	{
		bMovementLockedForDialog = false;
		if (const APlayerState* State = Cast<APlayerState>(GetOwner()))
		{
			if (AMT2PlayerCharacter* Player = Cast<AMT2PlayerCharacter>(State->GetPawn()))
			{
				Player->SetInteractionUIOpen(false);
			}
		}
	}
}

void UMT2QuestComponent::HandleDialogOptionSelected(int32 ChoiceIndex)
{
	ServerAnswerDialog(ChoiceIndex);
}

void UMT2QuestComponent::HandleDialogTextSubmitted(const FString& Text)
{
	ServerAnswerDialogText(Text);
}
