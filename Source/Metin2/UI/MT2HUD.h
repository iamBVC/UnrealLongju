/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "MT2HUD.generated.h"

class UMT2GameHUDWidget;
class UMT2LoadingScreenWidget;
class UMT2NotificationsWidget;
class UMT2SystemMenuWidget;
class UMT2StatusEffectBarWidget;
class UMT2PartyPanelWidget;
class UMT2PartyInviteDialogWidget;
class UMT2GuildWidget;
class UMT2GuildInviteDialogWidget;

UCLASS()
class METIN2_API AMT2HUD : public AHUD
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "HUD")
	void ToggleInventory();

	// ESC key / taskbar bottom-right system button: the old client's SystemDialog.
	void ToggleSystemMenu();

	UFUNCTION(BlueprintCallable, Category = "HUD")
	void ActivateQuickSlot(int32 SlotIndex);

	UMT2GameHUDWidget* GetGameHUDWidget() const { return GameHUDWidget; }

	void ToggleCharacterWindow();
	void ToggleSkillWindow();
	void ToggleBeltWindow();
	void ToggleMessengerWindow();
	void OpenFriendAddDialog();
	// Opens the whisper dialog on a player in the world - the target widget's "Message" button.
	void OpenMessengerConversation(AActor* TargetPlayer);
	// Shows the whisper dialog for whatever conversation the messenger component has open.
	void OpenWhisperWindow();
	// The standing notification strip (active quests, unread private messages).
	UMT2NotificationsWidget* GetNotificationsWidget() const { return NotificationsWidget; }
	// Character window opened straight on its Quests page (the N hotkey).
	void ToggleQuestWindow();
	void ToggleMinimapWindow();
	void ShowPartyInvite(int32 InviteId, const FString& InviterName);
	void ShowFriendRequest(const FString& RequesterId, const FString& RequesterName);
	void ToggleGuildWindow();
	void ShowGuildInvite(const FString& InviterName, const FString& GuildName);

protected:
	virtual void BeginPlay() override;

private:
	TSubclassOf<UMT2GameHUDWidget> ResolveGameHUDClass() const;
	bool EnsureSystemMenuWidget();
	bool EnsureMessengerWidget();
	bool EnsureFriendAddDialogWidget();
	bool EnsureWhisperWidget();
	bool EnsureNotificationsWidget();

	// World-entry cover: the map is fully streamed and the local pawn's appearance assets are
	// applied before anything is revealed, so players never see the T-posed placeholder mesh.
	void StartWorldRevealCover();
	void HandleWorldRevealCheck();
	void RevealWorld();

	UPROPERTY(Transient)
	TObjectPtr<UMT2GameHUDWidget> GameHUDWidget;

	UPROPERTY(Transient)
	TObjectPtr<UMT2SystemMenuWidget> SystemMenuWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UMT2MessengerWidget> MessengerWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UMT2FriendAddDialogWidget> FriendAddDialogWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UMT2WhisperWidget> WhisperWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UMT2NotificationsWidget> NotificationsWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UMT2FriendRequestDialogWidget> FriendRequestDialogWidget;

	UPROPERTY(Transient)
	TObjectPtr<UMT2StatusEffectBarWidget> StatusEffectBarWidget;

	UPROPERTY(Transient)
	TObjectPtr<UMT2PartyPanelWidget> PartyPanelWidget;

	UPROPERTY(Transient)
	TObjectPtr<UMT2PartyInviteDialogWidget> PartyInviteDialogWidget;

	UPROPERTY(Transient)
	TObjectPtr<UMT2GuildWidget> GuildWidget;

	UPROPERTY(Transient)
	TObjectPtr<UMT2GuildInviteDialogWidget> GuildInviteDialogWidget;

	UPROPERTY(Transient)
	TObjectPtr<UMT2LoadingScreenWidget> WorldRevealWidget;

	FTimerHandle WorldRevealTimer;
	double WorldRevealStartTime = 0.0;
};
