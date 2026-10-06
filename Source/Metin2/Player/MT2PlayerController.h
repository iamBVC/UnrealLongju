/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/UpdateLevelVisibilityLevelInfo.h"
#include "Server/MT2ServerRuntimeTypes.h"
#include "World/MT2MapPresentationActor.h"
#include "MT2PlayerController.generated.h"

class UMT2ChatWidget;
class UMT2InputConfig;
class AMT2Party;
class AMT2PlayerCharacter;
class AMT2PlayerState;
class UMT2QuestNode_Warp;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FMT2GatewayOperationSignature, bool, bSucceeded, const FString&, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FMT2CharacterListSignature, const TArray<FMT2CharacterSummary>&, Characters);

UCLASS(Blueprintable)
class METIN2_API AMT2PlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	friend class UMT2QuestNode_Warp;

#if WITH_EDITOR
	friend class FMT2PIEVisibilityQueueTest;
	virtual void PreClientTravel(const FString& PendingURL, ETravelType TravelType, bool bIsSeamlessTravel) override;
	virtual void ProcessEvent(UFunction* Function, void* Parameters) override;
	virtual void PostSeamlessTravel() override;
#endif

	UFUNCTION(BlueprintCallable, Category = "Network")
	bool ConnectToServer(const FString& Address);

	UFUNCTION(BlueprintCallable, Category = "Gateway")
	void RegisterAccount(const FString& Username, const FString& Password);

	UFUNCTION(BlueprintCallable, Category = "Gateway")
	void Login(const FString& Username, const FString& Password);

	UFUNCTION(BlueprintCallable, Category = "Gateway")
	void CreateCharacter(
		const FString& CharacterName, const FMT2CharacterAppearance& Appearance, EMT2Empire Empire);

	UFUNCTION(BlueprintCallable, Category = "Gateway")
	void SelectCharacter(const FString& CharacterId, int32 PreferredChannel = 0);

	UFUNCTION(BlueprintCallable, Category = "Network")
	void RequestMapTransfer(const FString& DestinationMapId, int32 PreferredChannel = 0);

	// Server-authoritative entry point used by administrative and gameplay travel systems. The
	// coordinator still resolves the destination process and issues the admission ticket.
	bool RequestMapTransferFromServer(
		const FString& DestinationMapId, int32 PreferredChannel = 0,
		bool bForceTownSpawn = false, bool bClearPendingSpawnOnFailure = false);

	UPROPERTY(BlueprintAssignable, Category = "Gateway")
	FMT2GatewayOperationSignature OnRegistrationCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Gateway")
	FMT2GatewayOperationSignature OnLoginCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Gateway")
	FMT2GatewayOperationSignature OnCharacterCreationCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Gateway")
	FMT2GatewayOperationSignature OnTravelFailed;

	UPROPERTY(BlueprintAssignable, Category = "Gateway")
	FMT2CharacterListSignature OnCharacterListUpdated;

	UMT2InputConfig* GetInputConfig();

	// True when the mouse is over a hit-testable UMG widget. Gameplay mouse actions use this to avoid
	// leaking UI clicks into click-to-move or camera rotation.
	bool IsPointerOverGameUI() const;

	// Old chat.AppendChat(CHAT_TYPE_INFO, ...): a system feedback line in the chat log.
	UFUNCTION(BlueprintCallable, Category = "UI")
	void AddInfoChatLine(const FString& Message);

	void RequestPartyInvite(AMT2PlayerCharacter* TargetPlayer);
	void RequestTrade(AMT2PlayerCharacter* TargetPlayer);
	UFUNCTION(BlueprintCallable, Category="Combat|Duel")
	void RequestDuel(AMT2PlayerCharacter* TargetPlayer);
	UFUNCTION(BlueprintCallable, Category="Combat|Duel")
	void CancelDuel(AMT2PlayerCharacter* TargetPlayer);
	UFUNCTION(Server, Reliable) void ServerRequestDuel(AMT2PlayerCharacter* TargetPlayer);
	UFUNCTION(Server, Reliable) void ServerCancelDuel(AMT2PlayerCharacter* TargetPlayer);
	void RespondToPartyInvite(int32 InviteId, bool bAccept);
	void RequestDisbandParty();
	void RequestLeaveParty();
	void RequestKickPartyMember(AMT2PlayerState* Member);
	void RequestKickPartyMemberById(const FString& CharacterId);

	// Server maintenance path. The first call warns the player while saving continues; the second
	// closes the connection only after the server reports its persistence flush complete.
	void PrepareForServerMaintenance(const FString& Message);
	void DisconnectForServerMaintenance(const FString& Message);
	void SendSystemChatMessage(const FString& Message);
	void SendFullMapNpcSnapshot();
	const TArray<FMT2FullMapNpcMarker>& GetFullMapNpcMarkers() const { return FullMapNpcMarkers; }
	FSimpleMulticastDelegate OnFullMapNpcMarkersChanged;

	UFUNCTION(Client, Reliable)
	void ClientSetNetworkProfilerEnabled(bool bEnabled);

	UFUNCTION(Client, Reliable)
	void ClientSetFullMapNpcMarkers(const TArray<FMT2FullMapNpcMarker>& Markers);

	UFUNCTION(Client, Reliable)
	void ClientReceivePartyInvite(int32 InviteId, const FString& InviterName);

	UFUNCTION(Client, Reliable)
	void ClientNotifyYangReceived(int64 Amount);

	UFUNCTION(Client, Reliable)
	void ClientNotifyExperienceReceived(int64 Amount);

	UFUNCTION(Client, Reliable)
	void ClientSpawnExperienceOrbs(FVector SourceLocation, int64 ExperienceAmount);

	UFUNCTION(Client, Reliable)
	void ClientNotifyItemReceived(int32 Vnum, int32 Count, int32 SkillVnum);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;

	UFUNCTION(Server, Reliable)
	void ServerRegisterAccount(const FString& Username, const FString& Password);

	UFUNCTION(Server, Reliable)
	void ServerLogin(const FString& Username, const FString& Password);

	UFUNCTION(Server, Reliable)
	void ServerCreateCharacter(
		const FString& CharacterName, const FMT2CharacterAppearance& Appearance, EMT2Empire Empire);

	UFUNCTION(Server, Reliable)
	void ServerSelectCharacter(const FString& CharacterId, int32 PreferredChannel);

	UFUNCTION(Server, Reliable)
	void ServerRequestMapTransfer(const FString& DestinationMapId, int32 PreferredChannel);

	UFUNCTION(Client, Reliable)
	void ClientRegistrationResult(bool bSucceeded, const FString& Error);

	UFUNCTION(Client, Reliable)
	void ClientLoginResult(
		bool bSucceeded, const FString& Error, const TArray<FMT2CharacterSummary>& Characters);

	UFUNCTION(Client, Reliable)
	void ClientCharacterCreated(
		bool bSucceeded, const FString& Error, const FMT2CharacterSummary& CharacterSummary);

	UFUNCTION(Client, Reliable)
	void ClientBeginMapTravel(const FString& Endpoint, const FString& Ticket);

	UFUNCTION(Client, Reliable)
	void ClientTravelFailed(const FString& Error);

	UFUNCTION(Client, Reliable)
	void ClientWorldReady();

	UFUNCTION(Client, Reliable)
	void ClientSessionRevoked(const FString& Reason);

	UFUNCTION(Client, Reliable)
	void ClientServerMaintenanceMessage(const FString& Message);

	UFUNCTION(Client, Reliable)
	void ClientSystemChatMessage(const FString& Message);

	void HandleAccountSessionRevoked(const FString& SessionOrAccountId, const FString& Reason);

	UFUNCTION(Server, Reliable)
	void ServerSubmitGlobalChat(const FString& Message);

	UFUNCTION(Client, Reliable)
	void ClientReceiveGlobalChat(
		EMT2Empire Empire, const FString& SenderName, const FString& Message, bool bWorldBroadcast);

	UFUNCTION(Server, Reliable)
	void ServerRequestPartyInvite(AMT2PlayerState* TargetPlayerState);

	UFUNCTION(Server, Reliable)
	void ServerRequestTrade(AMT2PlayerCharacter* TargetPlayer);

	UFUNCTION(Server, Reliable)
	void ServerRespondToPartyInvite(int32 InviteId, bool bAccept);

	UFUNCTION(Server, Reliable)
	void ServerDisbandParty();

	UFUNCTION(Server, Reliable)
	void ServerLeaveParty();

	UFUNCTION(Server, Reliable)
	void ServerKickPartyMember(AMT2PlayerState* Member);

	UFUNCTION(Server, Reliable)
	void ServerKickPartyMemberById(const FString& CharacterId);

private:
	friend class AMT2GameModeBase;
	TArray<FMT2FullMapNpcMarker> FullMapNpcMarkers;
	bool BeginMapTransfer(const FString& DestinationMapId, int32 PreferredChannel,
		bool bForceTownSpawn = false, bool bClearPendingSpawnOnFailure = false);
	void ClearMigrationSaveDelegate();
	void AbortMapTransfer(const FString& Error);

	void HandleToggleCharacter();
	void HandleToggleSystemMenu();
	void HandleToggleSkill();
	void HandleToggleBelt();
	void HandleToggleMessenger();
	void HandleToggleMinimap();
	void HandleToggleInventory();
	void HandleToggleNotifications();
	void HandleToggleSpecialMount();
	void HandleToggleHorse();
	void HandlePickupItems();
	void HandleToggleFirstPerson();
	// Push-to-talk or press-to-toggle, selected in Audio settings.
	void HandleVoiceTalkPressed();
	void HandleVoiceTalkReleased();
	void HandleQuickSlotIndex(int32 SlotIndex);
	// Rebuilds every rebindable key binding from UMT2KeyBindingSubsystem (runs again on rebind).
	void ApplyKeyBindings();
	void HandleToggleChat();
	void FocusChatInput();
	void RestoreGameFocusAfterChat();
	bool EnsureChatWidget();
	void HandleChatReceived(
		EMT2Empire Empire, const FString& SenderName, const FString& Message, bool bWorldBroadcast);
	void AddRewardChatLine(const FString& Message);

	UFUNCTION()
	void HandleChatClosed();

	UFUNCTION()
	void HandleChatCommandSubmitted(const FString& Command);

	UFUNCTION()
	void HandleChatMessageSubmitted(const FString& Message);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2InputConfig> InputConfig;

	UPROPERTY(Transient)
	TObjectPtr<UMT2InputConfig> NativeInputConfig;

	UPROPERTY(Transient)
	TObjectPtr<UMT2ChatWidget> ChatWidget;

	FString AuthenticatedAccountId;
#if WITH_EDITOR
	TArray<FUpdateLevelVisibilityLevelInfo> PendingPIELevelVisibility;
#endif
	FString SessionToken;
	bool bGatewayRequestPending = false;
	bool bMigrationPending = false;
	FString PendingMigrationMapId;
	int32 PendingMigrationChannel = 0;
	bool bPendingMigrationForceTownSpawn = false;
	bool bClearPendingSpawnOnMigrationFailure = false;
	FDelegateHandle MigrationSaveHandle;
	TArray<FMT2CharacterSummary> CachedCharacters;
	double LastGlobalChatSubmitTime = -1000.0;
	// Server-side login throttle (per connection; the gateway runtime adds per-IP/per-account).
	double LastServerLoginAttemptTime = -1000.0;

	TWeakObjectPtr<AMT2PlayerState> PendingPartyInviter;
	int32 PendingPartyInviteId = 0;
	int32 NextPartyInviteId = 1;
	double PendingPartyInviteExpiry = 0.0;
};
