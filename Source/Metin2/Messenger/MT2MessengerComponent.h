/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Messenger/MT2MessengerTypes.h"
#include "MT2MessengerComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMT2MessengerChangedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FMT2MessengerMessageSignature, const FMT2PrivateMessage&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FMT2MessengerResultSignature, FName, Action, EMT2MessengerResult, Result);

// The player's messenger: friend list, pending friend requests, and private conversations.
//
// Lives on the PlayerState. Everything here is per-player and private, so the replicated state is
// COND_OwnerOnly. The component itself holds no authority over the data - the coordinator does, since
// a friend may be on a different map server - so each action is an RPC up to the server, which
// forwards it to the coordinator and waits for the resulting push.
UCLASS(ClassGroup = "MT2", meta = (BlueprintSpawnableComponent))
class METIN2_API UMT2MessengerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMT2MessengerComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ---- reads (client) ----
	UFUNCTION(BlueprintPure, Category = "Messenger")
	const TArray<FMT2FriendEntry>& GetFriends() const { return Friends; }

	UFUNCTION(BlueprintPure, Category = "Messenger")
	const TArray<FMT2FriendRequest>& GetPendingRequests() const { return PendingRequests; }

	// The conversation currently open, oldest first. Filled by OpenConversation.
	UFUNCTION(BlueprintPure, Category = "Messenger")
	const TArray<FMT2PrivateMessage>& GetConversation() const { return Conversation; }

	UFUNCTION(BlueprintPure, Category = "Messenger")
	FString GetOpenConversationId() const { return OpenConversationId; }

	UFUNCTION(BlueprintPure, Category = "Messenger")
	int32 GetTotalUnreadCount() const;

	// ---- actions (called on the owning client) ----
	// Asks TargetName to become a friend. They get a request they must accept, like the old
	// messenger_auth prompt - a name alone never puts someone on your list.
	UFUNCTION(BlueprintCallable, Category = "Messenger")
	void RequestAddFriend(const FString& TargetName);

	// Asks a player in the world to be friends - the target board's "Friend" button. Like whispering,
	// the character id is server-only, so the actor is resolved there.
	UFUNCTION(BlueprintCallable, Category = "Messenger")
	void RequestAddFriendPlayer(AActor* TargetPlayer);

	UFUNCTION(BlueprintCallable, Category = "Messenger")
	void AnswerFriendRequest(const FString& RequesterId, bool bAccept);

	UFUNCTION(BlueprintCallable, Category = "Messenger")
	void RemoveFriend(const FString& CompanionId);

	UFUNCTION(BlueprintCallable, Category = "Messenger")
	void SendPrivateMessage(const FString& RecipientId, const FString& Body);

	// Loads the stored history with this friend and clears their unread badge.
	UFUNCTION(BlueprintCallable, Category = "Messenger")
	void OpenConversation(const FString& CompanionId);

	// Opens a conversation with a player in the world - the target widget's "Message" button. The
	// client cannot know a character id (it is server-only), so the target actor is resolved on the
	// server and handed back with the name.
	UFUNCTION(BlueprintCallable, Category = "Messenger")
	void OpenConversationWithPlayer(AActor* TargetPlayer);

	UFUNCTION(BlueprintCallable, Category = "Messenger")
	void CloseConversation();

	// Display name of the open conversation. Held separately because a whisper target need not be a
	// friend, so the name cannot always be looked up in the list.
	UFUNCTION(BlueprintPure, Category = "Messenger")
	FString GetOpenConversationName() const { return OpenConversationName; }

	// Whether the companion of the open conversation is a game master (the old dialog's
	// gamemastermark, shown for the person you are talking to - not for yourself).
	UFUNCTION(BlueprintPure, Category = "Messenger")
	bool IsOpenConversationGameMaster() const { return bOpenConversationIsGameMaster; }

	// Unread whispers per companion, kept on the client. This is separate from the friend list because
	// a whisper can come from anyone, friend or not, and the notification has to appear either way.
	const TMap<FString, int32>& GetUnreadCounts() const { return UnreadByCompanion; }
	// The sender's display name for an unread conversation, for callers with no friend entry to read.
	FString GetUnreadSenderName(const FString& CompanionId) const;
	// Clears the unread notification for one companion; opening their conversation is what does it.
	void MarkConversationRead(const FString& CompanionId);

	// Fires whenever the friend list, requests or open conversation change.
	UPROPERTY(BlueprintAssignable, Category = "Messenger")
	FMT2MessengerChangedSignature OnMessengerChanged;

	// A newly arrived message, for the chat line and the notification sound.
	UPROPERTY(BlueprintAssignable, Category = "Messenger")
	FMT2MessengerMessageSignature OnPrivateMessageReceived;

	UPROPERTY(BlueprintAssignable, Category = "Messenger")
	FMT2MessengerResultSignature OnMessengerActionResult;

	// ---- server-side entry points, called by the runtime subsystem when the coordinator pushes ----
	void ApplyFriendSnapshot(TArray<FMT2FriendEntry> NewFriends);
	void ApplyPresenceUpdate(
		const FString& CompanionId, const FString& CompanionName, bool bOnline,
		const FString& MapId, int32 Channel, int32 Level);
	void ApplyIncomingRequest(const FString& RequesterId, const FString& RequesterName);
	void ApplyIncomingMessage(const FMT2PrivateMessage& Message);
	void ApplyConversation(const FString& CompanionId, TArray<FMT2PrivateMessage> Messages);
	void ApplyActionResult(FName Action, EMT2MessengerResult Result);

private:
	UFUNCTION(Server, Reliable) void ServerRequestAddFriend(const FString& TargetName);
	UFUNCTION(Server, Reliable) void ServerAnswerFriendRequest(const FString& RequesterId, bool bAccept);
	UFUNCTION(Server, Reliable) void ServerRemoveFriend(const FString& CompanionId);
	UFUNCTION(Server, Reliable) void ServerSendPrivateMessage(const FString& RecipientId, const FString& Body);
	UFUNCTION(Server, Reliable) void ServerOpenConversation(const FString& CompanionId);
	UFUNCTION(Server, Reliable) void ServerOpenConversationWithPlayer(AActor* TargetPlayer);
	UFUNCTION(Server, Reliable) void ServerRequestAddFriendPlayer(AActor* TargetPlayer);
	// Shows the request prompt on the asked player's screen.
	UFUNCTION(Client, Reliable) void ClientShowFriendRequest(
		const FString& RequesterId, const FString& RequesterName);
	// Adds a companion without a coordinator: the friendship lives in this session only.
	void AddLocalFriend(const FString& CompanionId, const FString& CompanionName, bool bOnline);
	UFUNCTION(Client, Reliable) void ClientBeginConversation(
		const FString& CompanionId, const FString& CompanionName, bool bCompanionIsGameMaster);
	// Resolves a companion's game-master flag from their PlayerState on this server, when they are on it.
	bool IsCompanionGameMaster(const FString& CompanionId) const;

	UFUNCTION(Client, Reliable) void ClientReceiveMessage(const FMT2PrivateMessage& Message);
	UFUNCTION(Client, Reliable) void ClientReceiveConversation(
		const FString& CompanionId, const TArray<FMT2PrivateMessage>& Messages);
	UFUNCTION(Client, Reliable) void ClientReceiveActionResult(FName Action, EMT2MessengerResult Result);

	UFUNCTION() void OnRep_Messenger();

	// This character's messenger identity. Normally the persistent character id; see the .cpp for the
	// fallback used by sessions that never went through map admission (PIE, standalone).
	FString GetOwnerCharacterId() const;
	static FString GetMessengerIdFor(const class AMT2PlayerState* State);
	// The messenger of a player on this same server, by messenger id. Used to deliver locally when
	// there is no coordinator to route through.
	UMT2MessengerComponent* FindLocalMessenger(const FString& MessengerId) const;
	bool HasCoordinator() const;
	FString GetOwnerCharacterName() const;
	class UMT2ServerRuntimeSubsystem* GetRuntime() const;

	UPROPERTY(ReplicatedUsing = OnRep_Messenger)
	TArray<FMT2FriendEntry> Friends;

	UPROPERTY(ReplicatedUsing = OnRep_Messenger)
	TArray<FMT2FriendRequest> PendingRequests;

	// Not replicated: the history is fetched on demand and only ever concerns the owning client, so it
	// travels as an RPC payload rather than sitting in the actor's replicated state.
	UPROPERTY()
	TArray<FMT2PrivateMessage> Conversation;

	UPROPERTY()
	FString OpenConversationId;

	UPROPERTY()
	FString OpenConversationName;

	UPROPERTY()
	bool bOpenConversationIsGameMaster = false;

	// Client-side unread bookkeeping: companion id -> count, and the name to show for it.
	TMap<FString, int32> UnreadByCompanion;
	TMap<FString, FString> UnreadSenderNames;

	// Every message this client has seen, per companion. Without it a conversation opened after the
	// fact would be blank: the coordinator only has what it stored, and a server running without one
	// stores nothing at all.
	TMap<FString, TArray<FMT2PrivateMessage>> ConversationHistory;

	// The server's messenger id for this character, mirrored down so the client can tell its own
	// messages apart. It cannot work this out for itself: the persistent id lives only on the server.
	UPROPERTY(Replicated)
	FString ReplicatedMessengerId;

	// The other end of a message, whichever direction it went.
	FString GetCompanionOf(const FMT2PrivateMessage& Message) const;
	void AppendToHistory(const FMT2PrivateMessage& Message);

	// Announcing presence needs the restored character id, which arrives after this component does, so
	// the announce polls briefly rather than racing the load (same shape as the quest manager).
	void TryAnnounceLogin();
	FTimerHandle LoginAnnounceTimer;
};
