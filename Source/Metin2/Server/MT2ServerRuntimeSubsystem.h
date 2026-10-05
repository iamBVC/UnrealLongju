/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Persistence/MT2PersistenceTypes.h"
#include "Party/MT2PartyTypes.h"
#include "Guild/MT2GuildTypes.h"
#include "Server/MT2ServerRuntimeTypes.h"
#include "MT2ServerRuntimeSubsystem.generated.h"

class FJsonObject;
class FSocket;

DECLARE_MULTICAST_DELEGATE_FourParams(
	FMT2ChatReceivedSignature, EMT2Empire, const FString&, const FString&, bool);
DECLARE_MULTICAST_DELEGATE_TwoParams(
	FMT2AccountSessionRevokedSignature, const FString& /*SessionOrAccountId*/, const FString& /*Reason*/);

UCLASS()
class METIN2_API UMT2ServerRuntimeSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual bool IsTickableWhenPaused() const override { return true; }

	UFUNCTION(BlueprintPure, Category = "Server Runtime")
	EMT2ServerRuntimeMode GetRuntimeMode() const { return Config.Mode; }

	UFUNCTION(BlueprintPure, Category = "Server Runtime")
	bool IsCoordinator() const { return Config.IsCoordinator(); }

	UFUNCTION(BlueprintPure, Category = "Server Runtime")
	bool IsGateway() const { return Config.IsGateway(); }

	UFUNCTION(BlueprintPure, Category = "Server Runtime")
	bool IsMapServer() const { return Config.IsMapServer(); }

	UFUNCTION(BlueprintPure, Category = "Server Runtime")
	bool IsCoordinatorConnected() const { return bCoordinatorAuthenticated; }

	UFUNCTION(BlueprintPure, Category = "Server Runtime")
	FString GetMapId() const { return Config.MapId; }

	UFUNCTION(BlueprintPure, Category = "Server Runtime")
	int32 GetChannel() const { return Config.Channel; }

	UFUNCTION(BlueprintPure, Category = "Server Runtime")
	EMT2MapServerState GetMapServerState() const;

	const FMT2ServerRuntimeConfig& GetConfig() const { return Config; }

	void RequestPersistenceLoad(
		FName EntityType, const FString& EntityId,
		TFunction<void(FMT2PersistenceLoadResult&&)> Completion);
	void RequestPersistenceSave(
		const FMT2PersistentRecord& Record,
		TFunction<void(FMT2PersistenceSaveResult&&)> Completion);
	void RequestMapRoute(
		const FString& MapId, int32 PreferredChannel,
		TFunction<void(FMT2MapRouteResult&&)> Completion);
	void RequestTransferTicket(
		const FString& CharacterId, const FString& AccountId, const FString& DestinationMapId,
		int32 PreferredChannel, bool bForceTownSpawn,
		TFunction<void(FMT2TransferTicketResult&&)> Completion);
	void ClaimTransferTicket(
		const FString& Ticket, TFunction<void(FMT2TransferClaimResult&&)> Completion);
	void CompleteTransferTicket(
		const FString& Ticket, bool bSucceeded,
		TFunction<void(FMT2TransferCompleteResult&&)> Completion);
	void RequestAccountRegistration(
		const FString& Username, const FString& Password,
		TFunction<void(FMT2AccountRegistrationResult&&)> Completion);
	void RequestAccountLogin(
		const FString& Username, const FString& Password, const FString& ExistingSessionToken,
		TFunction<void(FMT2AccountLoginResult&&)> Completion);
	void RequestCharacterCreation(
		const FString& SessionToken, const FString& CharacterName,
		const FMT2CharacterAppearance& Appearance, EMT2Empire Empire,
		TFunction<void(FMT2CharacterCreateResult&&)> Completion);
	void RequestCharacterAdmission(
		const FString& SessionToken, const FString& CharacterId, int32 PreferredChannel,
		TFunction<void(FMT2CharacterAdmissionResult&&)> Completion);

	// Gateway-side brute-force protection: at most one login attempt per IP and per account name
	// inside the cooldown window. Returns true when the attempt must be rejected; a permitted
	// attempt stamps both keys. Throttled attempts do NOT refresh the stamps, so a spammer cannot
	// lock a victim's account name out forever.
	static constexpr double LoginAttemptCooldownSeconds = 5.0;
	bool ShouldThrottleLoginAttempt(const FString& RemoteIp, const FString& Username);

	void PublishChat(EMT2Empire Empire, const FString& SenderName, const FString& Message,
		bool bBroadcastToAllServers);
	void NotifyCharacterLogout(const FString& CharacterId, const FString& AccountId);
	// Party state is session data owned by the coordinator. It is intentionally not persisted in
	// SQLite, but survives map-server travel and can span machines.
	void PublishPartySnapshot(const FString& PartyId, const TArray<FMT2PartyMemberData>& Members);
	void PublishPartyDisband(const FString& PartyId);
	void RequestClusterPersist(TFunction<void(bool, const FString&)> Completion);
	void RequestClusterShutdown(bool bRestart, TFunction<void(bool, const FString&)> Completion);
	void RequestClusterServerList(TFunction<void(bool, const FString&)> Completion);
	// Admin "/tpall": summon every player on every map server to WorldLocation on THIS server's map
	// and channel. Callable only from a map server; the coordinator fans a "summon" broadcast out to
	// all map peers, which teleport local players and map-transfer remote ones to the destination.
	void RequestSummonAllPlayers(const FVector& WorldLocation, TFunction<void(bool, const FString&)> Completion);
	// ---- messenger (cross-server) ----
	// The coordinator owns presence and routing; friendships and messages live in SQLite next to it,
	// so a friend on another map server shows the right lamp and a message to an offline player waits
	// in the store until they log in anywhere in the cluster.
	void PublishMessengerLogin(
		const FString& CharacterId, const FString& CharacterName, int32 Level);
	void PublishMessengerRequestAdd(const FString& CharacterId, const FString& TargetName);
	void PublishMessengerAnswerRequest(
		const FString& CharacterId, const FString& RequesterId, bool bAccept);
	void PublishMessengerRemoveFriend(const FString& CharacterId, const FString& CompanionId);
	void PublishMessengerSendMessage(
		const FString& CharacterId, const FString& SenderName,
		const FString& RecipientId, const FString& Body);
	void PublishMessengerOpenConversation(const FString& CharacterId, const FString& CompanionId);
	void PublishGuildLogin(const FString& CharacterId);
	void PublishGuildCreate(const FString& CharacterId, const FString& GuildName);
	void PublishGuildInvite(const FString& CharacterId, const FString& TargetCharacterId);
	void PublishGuildAnswer(const FString& CharacterId, const FString& InviterId, bool bAccept);
	void PublishGuildAction(
		const FString& CharacterId, FName Action, const FString& TargetId,
		int32 Rank = 0, const FString& Text = FString(), int32 Flags = 0);
	void PublishGuildChat(const FString& CharacterId, const FString& SenderName, const FString& Message);
	void PublishGuildMarkUpload(const FString& CharacterId, const TArray<uint8>& PngData);
	void PublishGuildMarkRequest(const FString& CharacterId, int32 GuildId, int64 KnownRevision);

	FMT2ChatReceivedSignature OnChatReceived;
	FMT2AccountSessionRevokedSignature OnAccountSessionRevoked;

private:
	struct FCoordinatorPeer
	{
		FSocket* Socket = nullptr;
		TArray<uint8> ReceiveBuffer;
		TArray<uint8> SendBuffer;
		int32 SendOffset = 0;
		bool bAuthenticated = false;
		bool bCloseRequested = false;
		FString InstanceId;
		FString Role;
		FString PublicIp;
		FString BuildVersion;
		int32 GamePort = 0;
		int32 PlayerCount = 0;
		int32 MaxPlayers = 0;
		double LastHeartbeatSeconds = 0.0;
		double ConnectedAtSeconds = 0.0;
	};
	enum class EClusterControlAction : uint8
	{
		None,
		Persist,
		Shutdown,
		Reboot
	};
	struct FClusterControlOperation
	{
		EClusterControlAction Action = EClusterControlAction::None;
		FString OperationId;
		FString RequesterInstanceId;
		FString RequesterRequestId;
		TSet<FString> PendingInstances;
		FString Error;
		double DeadlineSeconds = 0.0;
		bool bFinalizing = false;

		bool IsActive() const { return Action != EClusterControlAction::None; }
		void Reset() { *this = FClusterControlOperation(); }
	};
	enum class ETransferState : uint8
	{
		Issued,
		Claimed,
		Completing
	};
	struct FTransferTicket
	{
		FString Ticket;
		FString CharacterId;
		FString AccountId;
		FString SourceInstanceId;
		FString DestinationInstanceId;
		FString DestinationMapId;
		int32 DestinationChannel = 1;
		bool bForceTownSpawn = false;
		FString SessionToken;
		FMT2CharacterSummary Character;
		ETransferState State = ETransferState::Issued;
		double ExpiresAtSeconds = 0.0;
	};
	struct FAccountSession
	{
		FString Token;
		FString AccountId;
		FString GatewayInstanceId;
		double ExpiresAtSeconds = 0.0;
	};
	struct FCharacterLease
	{
		FString CharacterId;
		FString AccountId;
		FString MapInstanceId;
		double ExpiresAtSeconds = 0.0;
	};
	// Who is online, where, right now. Rebuilt from map-server announcements rather than persisted:
	// presence is session state, and a coordinator restart re-learns it as servers reconnect.
	struct FMessengerPresence
	{
		FString CharacterId;
		FString CharacterName;
		FString MapInstanceId;
		FString MapId;
		int32 Channel = 1;
		int32 Level = 0;
	};

	struct FCoordinatorParty
	{
		FString PartyId;
		TArray<FMT2PartyMemberData> Members;
	};
	struct FPendingGuildInvite
	{
		FString InviterId;
		int32 GuildId = 0;
		double ExpiresAtSeconds = 0.0;
	};

	bool ParseCommandLine(FString& OutError);
	void HandlePostWorldInitialization(UWorld* World, const UWorld::InitializationValues InitializationValues);
	void TryTravelToConfiguredMap(UWorld* World);

	bool StartCoordinatorListener();
	void StopNetworking();
	void TickCoordinator(double NowSeconds);
	void TickMapClient(double NowSeconds);
	void AcceptCoordinatorPeers();
	void PollCoordinatorPeers(double NowSeconds);
	void PollMapCoordinatorConnection(double NowSeconds);
	void BeginCoordinatorConnection(double NowSeconds);
	void DisconnectMapCoordinator(const FString& Reason);

	bool PollSocket(
		FSocket* Socket, TArray<uint8>& ReceiveBuffer,
		TFunctionRef<void(const TSharedPtr<FJsonObject>&)> MessageHandler);
	bool FlushSocket(FSocket* Socket, TArray<uint8>& SendBuffer, int32& SendOffset);
	bool QueueJson(TArray<uint8>& SendBuffer, const TSharedRef<FJsonObject>& Message) const;
	void QueuePeerJson(FCoordinatorPeer& Peer, const TSharedRef<FJsonObject>& Message) const;
	void QueueMapJson(const TSharedRef<FJsonObject>& Message);

	void HandleCoordinatorPeerMessage(FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message);
	void HandleMapCoordinatorMessage(const TSharedPtr<FJsonObject>& Message);
	void RemoveCoordinatorPeer(int32 PeerIndex);
	FCoordinatorPeer* FindPeerByInstanceId(const FString& InstanceId) const;
	void SendToInstance(const FString& InstanceId, const TSharedRef<FJsonObject>& Message);
	void RegisterMapServer(FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message);
	void UpdateMapServerHeartbeat(FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message, double NowSeconds);
	bool ChooseMapServer(const FString& MapId, int32 PreferredChannel, FMT2MapServerDescriptor& OutServer) const;
	void HandlePersistenceLoadRequest(FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message);
	void HandlePersistenceSaveRequest(FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message);
	void HandleRouteRequest(FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message);
	void HandleTransferIssueRequest(FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message, double NowSeconds);
	void HandleTransferClaimRequest(FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message, double NowSeconds);
	void HandleTransferCompleteRequest(FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message);
	void HandleAccountRegistrationRequest(FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message);
	void HandleAccountLoginRequest(FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message);
	void HandleCharacterCreationRequest(FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message);
	void HandleCharacterAdmissionRequest(FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message, double NowSeconds);
	void HandleCharacterLogout(FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message);
	// ---- messenger, coordinator side ----
	void HandleMessengerLogin(FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message);
	void HandleMessengerRequestAdd(FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message);
	void HandleMessengerAnswerRequest(FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message);
	void HandleMessengerRemoveFriend(FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message);
	void HandleMessengerSendMessage(FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message);
	void HandleMessengerOpenConversation(FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message);
	void MessengerDropPresence(const FString& CharacterId);
	// ---- messenger, map-server side ----
	class UMT2MessengerComponent* FindLocalMessenger(const FString& CharacterId) const;
	// Returns true when the message was a messenger push (handled or harmlessly dropped).
	bool HandleMapMessengerMessage(const FString& Type, const TSharedPtr<FJsonObject>& Message);
	// Sends CharacterId its whole messenger state (friend list with live presence).
	void SendMessengerSnapshot(const FString& CharacterId);
	// Routes one message to whichever map server hosts CharacterId. Silently drops it when the
	// character is offline - the caller decides whether that needs storing instead.
	bool SendToCharacter(const FString& CharacterId, const TSharedRef<FJsonObject>& Message);
	// Tells everyone who has CharacterId on their friend list that its presence changed.
	void BroadcastMessengerPresence(const FString& CharacterId, bool bOnline);
	class FMT2PersistenceBackend* GetMessengerStore() const;
	void HandleGuildLogin(FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message);
	void HandleGuildCreate(FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message);
	void HandleGuildInvite(FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message, double NowSeconds);
	void HandleGuildAnswer(FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message, double NowSeconds);
	void HandleGuildAction(FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message);
	void HandleGuildChat(FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message);
	void HandleGuildMarkUpload(FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message);
	void HandleGuildMarkRequest(FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message);
	bool HandleMapGuildMessage(const FString& Type, const TSharedPtr<FJsonObject>& Message);
	void SendGuildSnapshot(const FString& CharacterId);
	void BroadcastGuildSnapshot(int32 GuildId);
	void SendGuildResult(const FString& CharacterId, const FString& Action, EMT2GuildResult Result);
	class UMT2GuildComponent* FindLocalGuild(const FString& CharacterId) const;
	static TSharedRef<FJsonObject> WriteGuildSnapshot(const FMT2GuildSnapshot& Guild);
	static bool ReadGuildSnapshot(const TSharedPtr<FJsonObject>& Object, FMT2GuildSnapshot& OutGuild);

	void HandlePartyUpdate(FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message);
	void HandlePartyDisband(FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message);
	void BroadcastPartySnapshot(const FCoordinatorParty& Party);
	void BroadcastPartyDisband(const FString& PartyId);
	void BroadcastPartyForCharacter(const FString& CharacterId);
	void ApplyPartySnapshotToWorld(
		const FString& PartyId, const TArray<FMT2PartyMemberData>& Members);
	void ApplyPartyDisbandToWorld(const FString& PartyId);
	void HandleAdminControlRequest(FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message);
	// Map-server side of "/tpall": teleport local players to Location (5m spread) if this is the
	// destination server, otherwise map-transfer them to DestMapId/DestChannel arriving at Location.
	void ExecuteLocalSummon(const FString& DestInstanceId, const FString& DestMapId, int32 DestChannel, const FVector& Location);
	void HandleControlPeerReady(FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message);
	void BeginCoordinatorControl(
		EClusterControlAction Action, const FString& RequesterInstanceId, const FString& RequestId);
	void CompleteCoordinatorControl();
	void SendAdminControlResult(
		const FString& InstanceId, const FString& RequestId, bool bSucceeded, const FString& Message);
	FString BuildServerList() const;
	void BeginLocalControl(EClusterControlAction Action, const FString& OperationId);
	void ForceSaveAllPlayers(TFunction<void(bool, const FString&)> Completion);
	void NotifyAllPlayersForMaintenance(const FString& Message, bool bDisconnect);
	void SendLocalControlReady(
		EClusterControlAction Action, const FString& OperationId, bool bSucceeded, const FString& Error);
	void ScheduleLocalExit(bool bRestart);
	bool RelaunchCurrentProcess() const;
	FAccountSession* FindValidSession(const FString& SessionToken, const FString& GatewayInstanceId);
	bool HasActiveCharacterReservation(const FString& CharacterId, double NowSeconds) const;
	void RevokeAccountSessions(const FString& AccountId, const FString& PreservedSessionToken);
	void ExpirePendingRequests(double NowSeconds);
	void TrackPendingRequest(const FString& RequestId);
	bool IsConfiguredWorldReady() const;

	FString NextRequestId();
	static FString GetStringField(const TSharedPtr<FJsonObject>& Message, const TCHAR* Name);
	static int32 GetIntField(const TSharedPtr<FJsonObject>& Message, const TCHAR* Name, int32 DefaultValue = 0);
	static int64 GetInt64Field(const TSharedPtr<FJsonObject>& Message, const TCHAR* Name, int64 DefaultValue = 0);
	static void WriteServerDescriptor(const FMT2MapServerDescriptor& Server, const TSharedRef<FJsonObject>& Message);
	static FMT2MapServerDescriptor ReadServerDescriptor(const TSharedPtr<FJsonObject>& Message);
	static TSharedRef<FJsonObject> WriteCharacterSummary(const FMT2CharacterSummary& Character);
	static bool ReadCharacterSummary(const TSharedPtr<FJsonObject>& Object, FMT2CharacterSummary& OutCharacter);
	static TSharedRef<FJsonObject> WritePartyMember(const FMT2PartyMemberData& Member);
	static bool ReadPartyMember(const TSharedPtr<FJsonObject>& Object, FMT2PartyMemberData& OutMember);

	FMT2ServerRuntimeConfig Config;
	FDelegateHandle PostWorldInitializationHandle;
	bool bMapTravelRequested = false;

	FSocket* ListenerSocket = nullptr;
	FSocket* CoordinatorSocket = nullptr;
	TArray<TUniquePtr<FCoordinatorPeer>> CoordinatorPeers;
	TMap<FString, FMT2MapServerDescriptor> RegisteredMapServers;
	TMap<FString, FTransferTicket> TransferTickets;
	TMap<FString, FAccountSession> AccountSessions;
	TMap<FString, FString> SessionByAccount;
	TMap<FString, FCharacterLease> CharacterLeases;
	TMap<FString, FCoordinatorParty> CoordinatorParties;
	// Coordinator only. Presence, plus the friend requests awaiting an answer (kept in memory like the
	// old m_set_requestToAdd - an unanswered request does not survive a restart).
	TMap<FString, FMessengerPresence> MessengerPresences;
	TMultiMap<FString, FString> MessengerPendingRequests; // recipient id -> requester id
	TMap<FString, FPendingGuildInvite> PendingGuildInvites;
	TMap<int32, double> GuildMarkUploadStamps;
	FClusterControlOperation ClusterControl;
	TArray<uint8> CoordinatorReceiveBuffer;
	TArray<uint8> CoordinatorSendBuffer;
	int32 CoordinatorSendOffset = 0;
	bool bCoordinatorConnectPending = false;
	bool bCoordinatorAuthenticated = false;
	bool bCoordinatorRegistered = false;
	double CoordinatorConnectStartedSeconds = 0.0;
	double NextCoordinatorConnectSeconds = 0.0;
	double NextHeartbeatSeconds = 0.0;
	uint64 RequestSequence = 0;

	TMap<FString, TFunction<void(FMT2PersistenceLoadResult&&)>> PendingLoads;
	TMap<FString, TFunction<void(FMT2PersistenceSaveResult&&)>> PendingSaves;
	TMap<FString, TFunction<void(FMT2MapRouteResult&&)>> PendingRoutes;
	TMap<FString, TFunction<void(FMT2TransferTicketResult&&)>> PendingTransferTickets;
	TMap<FString, TFunction<void(FMT2TransferClaimResult&&)>> PendingTransferClaims;
	TMap<FString, TFunction<void(FMT2TransferCompleteResult&&)>> PendingTransferCompletions;
	TMap<FString, TFunction<void(FMT2AccountRegistrationResult&&)>> PendingAccountRegistrations;
	TMap<FString, TFunction<void(FMT2AccountLoginResult&&)>> PendingAccountLogins;
	// "ip:<addr>" / "user:<name>" -> last permitted login attempt (FPlatformTime seconds).
	TMap<FString, double> LoginAttemptStamps;
	TMap<FString, TFunction<void(FMT2CharacterCreateResult&&)>> PendingCharacterCreations;
	TMap<FString, TFunction<void(FMT2CharacterAdmissionResult&&)>> PendingCharacterAdmissions;
	TMap<FString, TFunction<void(bool, const FString&)>> PendingAdminControls;
	TMap<FString, double> PendingRequestDeadlines;
	bool bLocalMaintenancePreparing = false;
	bool bLocalExitScheduled = false;
};
