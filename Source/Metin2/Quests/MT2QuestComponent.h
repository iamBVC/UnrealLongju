/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Quests/MT2QuestTypes.h"
#include "MT2QuestComponent.generated.h"

class UMT2QuestDialogWidget;

// The per-player half of the old quest system, living on the PlayerState. This step carries the
// dialog session (the say/select suspend-resume of the old Lua coroutines): the server shows a
// dialog page with an answer callback, the owning client renders it and answers back. Quest
// states/flags join this component with the quest subsystem.
UCLASS(BlueprintType, ClassGroup = "MT2", meta = (BlueprintSpawnableComponent))
class METIN2_API UMT2QuestComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMT2QuestComponent();

	// Server: shows a dialog page on the owning client. OnAnswer runs (server-side) with the
	// 1-based chosen option, or 0 when the dialog is dismissed - the select() resume point.
	// Replaces any dialog already open (old scripts do the same on re-click).
	void ShowDialog(const FMT2DialogPayload& Payload, TFunction<void(int32)> OnAnswer);
	void ShowInputDialog(const FMT2DialogPayload& Payload, TFunction<void(const FString&)> OnAnswer);

	// Server: closes the client's dialog window (setskin(NOWINDOW) / end of conversation).
	void CloseDialog();
	bool HasPendingDialog() const { return !!PendingAnswerCallback || !!PendingTextCallback; }

	UFUNCTION(Server, Reliable)
	void ServerAnswerDialog(int32 ChoiceIndex);

	UFUNCTION(Server, Reliable)
	void ServerAnswerDialogText(const FString& Text);

private:
	UFUNCTION(Client, Reliable)
	void ClientShowDialog(FMT2DialogPayload Payload);

	UFUNCTION(Client, Reliable)
	void ClientCloseDialog();

	UFUNCTION()
	void HandleDialogOptionSelected(int32 ChoiceIndex);
	UFUNCTION()
	void HandleDialogTextSubmitted(const FString& Text);

	UMT2QuestDialogWidget* GetOrCreateDialogWidget();

	// Server-side continuation of the running conversation (the suspended coroutine).
	TFunction<void(int32)> PendingAnswerCallback;
	TFunction<void(const FString&)> PendingTextCallback;

	// Owning-client dialog window, created on demand.
	UPROPERTY(Transient)
	TObjectPtr<UMT2QuestDialogWidget> DialogWidget;

	// Client-side: whether the dialog currently holds the pawn's movement lock.
	bool bMovementLockedForDialog = false;
};
