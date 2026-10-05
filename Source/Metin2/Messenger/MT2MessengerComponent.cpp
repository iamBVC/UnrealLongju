/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Messenger/MT2MessengerComponent.h"

#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Persistence/MT2PersistenceComponent.h"
#include "Player/MT2PlayerState.h"
#include "UI/MT2HUD.h"
#include "Server/MT2ServerRuntimeSubsystem.h"

UMT2MessengerComponent::UMT2MessengerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UMT2MessengerComponent::BeginPlay()
{
	Super::BeginPlay();
	// Server only: presence, the friend list and the offline backlog all come from the coordinator.
	if (!GetOwner() || !GetOwner()->HasAuthority() || !GetWorld())
	{
		return;
	}
	GetWorld()->GetTimerManager().SetTimer(
		LoginAnnounceTimer, FTimerDelegate::CreateUObject(this, &UMT2MessengerComponent::TryAnnounceLogin),
		0.5f, true, 0.5f);
}

void UMT2MessengerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LoginAnnounceTimer);
	}
	Super::EndPlay(EndPlayReason);
}

void UMT2MessengerComponent::TryAnnounceLogin()
{
	const FString CharacterId = GetOwnerCharacterId();
	if (CharacterId.IsEmpty())
	{
		return; // name and character record not restored yet; the poll retries
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LoginAnnounceTimer);
	}
	// The client needs this to recognise its own messages, with or without a coordinator.
	ReplicatedMessengerId = CharacterId;
	// Nothing to announce without a coordinator: presence is its to track, and a lone server has
	// nobody to tell.
	if (!HasCoordinator())
	{
		return;
	}
	const AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner());
	GetRuntime()->PublishMessengerLogin(
		CharacterId, GetOwnerCharacterName(), State ? State->GetCharacterLevel() : 0);
}

void UMT2MessengerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// A friend list is private; nobody else needs to know who you talk to.
	DOREPLIFETIME_CONDITION(UMT2MessengerComponent, Friends, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UMT2MessengerComponent, PendingRequests, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UMT2MessengerComponent, ReplicatedMessengerId, COND_OwnerOnly);
}

FString UMT2MessengerComponent::GetMessengerIdFor(const AMT2PlayerState* State)
{
	const UMT2PersistenceComponent* Persistence = State ? State->GetPersistenceComponent() : nullptr;
	const FString EntityId = Persistence ? Persistence->GetEntityId() : FString();
	if (!EntityId.IsEmpty())
	{
		return EntityId;
	}
	// PIE and standalone never run map admission (AMT2GameModeBase gates it on IsMapServer), so no
	// persistent id is ever assigned. Falling back to the character name keeps the messenger usable
	// there: names are unique in the database, so the two identities never collide for a real session.
	return State ? State->GetCharacterName() : FString();
}

FString UMT2MessengerComponent::GetOwnerCharacterId() const
{
	// On a client the persistent id is not available locally, so use the one the server mirrored down;
	// computing it here would fall back to the name and disagree with the server's id in a real
	// cluster, which would make the client mistake its own messages for someone else's.
	if (!ReplicatedMessengerId.IsEmpty())
	{
		return ReplicatedMessengerId;
	}
	return GetMessengerIdFor(Cast<AMT2PlayerState>(GetOwner()));
}

FString UMT2MessengerComponent::GetCompanionOf(const FMT2PrivateMessage& Message) const
{
	const FString OwnId = GetOwnerCharacterId();
	return Message.SenderCharacterId == OwnId ? Message.RecipientCharacterId : Message.SenderCharacterId;
}

void UMT2MessengerComponent::AppendToHistory(const FMT2PrivateMessage& Message)
{
	const FString CompanionId = GetCompanionOf(Message);
	if (CompanionId.IsEmpty())
	{
		return;
	}
	TArray<FMT2PrivateMessage>& History = ConversationHistory.FindOrAdd(CompanionId);
	History.Add(Message);
	if (History.Num() > MT2Messenger::MaxConversationHistory)
	{
		History.RemoveAt(0, History.Num() - MT2Messenger::MaxConversationHistory);
	}
}

UMT2MessengerComponent* UMT2MessengerComponent::FindLocalMessenger(const FString& MessengerId) const
{
	UWorld* World = GetWorld();
	if (!World || MessengerId.IsEmpty())
	{
		return nullptr;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		AMT2PlayerState* State = It->Get() ? It->Get()->GetPlayerState<AMT2PlayerState>() : nullptr;
		if (State && GetMessengerIdFor(State) == MessengerId)
		{
			return State->FindComponentByClass<UMT2MessengerComponent>();
		}
	}
	return nullptr;
}

bool UMT2MessengerComponent::IsCompanionGameMaster(const FString& CompanionId) const
{
	UWorld* World = GetWorld();
	if (!World || CompanionId.IsEmpty())
	{
		return false;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const AMT2PlayerState* State = It->Get() ? It->Get()->GetPlayerState<AMT2PlayerState>() : nullptr;
		if (State && GetMessengerIdFor(State) == CompanionId)
		{
			return State->IsAdmin();
		}
	}
	// Off this server: presence does not carry the flag yet, so the mark stays hidden rather than
	// claiming something we cannot check.
	return false;
}

FString UMT2MessengerComponent::GetUnreadSenderName(const FString& CompanionId) const
{
	if (const FString* Name = UnreadSenderNames.Find(CompanionId))
	{
		return *Name;
	}
	return FString();
}

bool UMT2MessengerComponent::HasCoordinator() const
{
	const UMT2ServerRuntimeSubsystem* Runtime = GetRuntime();
	return Runtime && Runtime->IsMapServer() && Runtime->IsCoordinatorConnected();
}

FString UMT2MessengerComponent::GetOwnerCharacterName() const
{
	const AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner());
	return State ? State->GetCharacterName() : FString();
}

UMT2ServerRuntimeSubsystem* UMT2MessengerComponent::GetRuntime() const
{
	const AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner());
	UGameInstance* GameInstance = State ? State->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UMT2ServerRuntimeSubsystem>() : nullptr;
}

int32 UMT2MessengerComponent::GetTotalUnreadCount() const
{
	int32 Total = 0;
	for (const FMT2FriendEntry& Friend : Friends)
	{
		Total += Friend.UnreadCount;
	}
	return Total;
}

// -------------------------------------------------------------------------------------------------
// Client-side actions
// -------------------------------------------------------------------------------------------------

void UMT2MessengerComponent::RequestAddFriend(const FString& TargetName)
{
	const FString Clean = TargetName.TrimStartAndEnd();
	if (Clean.IsEmpty())
	{
		return;
	}
	if (Clean.Equals(GetOwnerCharacterName(), ESearchCase::IgnoreCase))
	{
		ApplyActionResult(TEXT("add"), EMT2MessengerResult::CannotAddSelf);
		return;
	}
	ServerRequestAddFriend(Clean);
}

void UMT2MessengerComponent::RequestAddFriendPlayer(AActor* TargetPlayer)
{
	if (TargetPlayer)
	{
		ServerRequestAddFriendPlayer(TargetPlayer);
	}
}

void UMT2MessengerComponent::AnswerFriendRequest(const FString& RequesterId, bool bAccept)
{
	if (RequesterId.IsEmpty())
	{
		return;
	}
	// Drop it locally straight away so the prompt cannot be answered twice while the RPC is in flight.
	PendingRequests.RemoveAll([&RequesterId](const FMT2FriendRequest& Request)
	{
		return Request.FromCharacterId == RequesterId;
	});
	OnMessengerChanged.Broadcast();
	ServerAnswerFriendRequest(RequesterId, bAccept);
}

void UMT2MessengerComponent::RemoveFriend(const FString& CompanionId)
{
	if (!CompanionId.IsEmpty())
	{
		ServerRemoveFriend(CompanionId);
	}
}

void UMT2MessengerComponent::SendPrivateMessage(const FString& RecipientId, const FString& Body)
{
	const FString Clean = Body.TrimStartAndEnd().Left(MT2Messenger::MaxMessageLength);
	if (RecipientId.IsEmpty() || Clean.IsEmpty())
	{
		return;
	}
	ServerSendPrivateMessage(RecipientId, Clean);
}

void UMT2MessengerComponent::OpenConversation(const FString& CompanionId)
{
	if (CompanionId.IsEmpty())
	{
		return;
	}
	OpenConversationId = CompanionId;
	// Seeded from this client's own record; a coordinator's stored history replaces it when it arrives.
	Conversation = ConversationHistory.FindOrAdd(CompanionId);
	if (const FMT2FriendEntry* Friend = Friends.FindByPredicate(
		[&CompanionId](const FMT2FriendEntry& Entry) { return Entry.CharacterId == CompanionId; }))
	{
		OpenConversationName = Friend->CharacterName;
	}
	else if (const FString UnreadName = GetUnreadSenderName(CompanionId); !UnreadName.IsEmpty())
	{
		// Opened from a notification: the sender need not be a friend, so the name comes from there.
		OpenConversationName = UnreadName;
	}
	MarkConversationRead(CompanionId);
	OnMessengerChanged.Broadcast();
	ServerOpenConversation(CompanionId);
}

void UMT2MessengerComponent::OpenConversationWithPlayer(AActor* TargetPlayer)
{
	if (TargetPlayer)
	{
		ServerOpenConversationWithPlayer(TargetPlayer);
	}
}

void UMT2MessengerComponent::CloseConversation()
{
	OpenConversationName.Reset();
	bOpenConversationIsGameMaster = false;
	OpenConversationId.Reset();
	Conversation.Reset();
	OnMessengerChanged.Broadcast();
}

// -------------------------------------------------------------------------------------------------
// Server RPCs - each one forwards to the coordinator, which owns the friend graph and the store
// -------------------------------------------------------------------------------------------------

void UMT2MessengerComponent::ServerRequestAddFriend_Implementation(const FString& TargetName)
{
	if (UMT2ServerRuntimeSubsystem* Runtime = GetRuntime())
	{
		Runtime->PublishMessengerRequestAdd(GetOwnerCharacterId(), TargetName.Left(24));
	}
	else
	{
		ClientReceiveActionResult(TEXT("add"), EMT2MessengerResult::Unavailable);
	}
}

void UMT2MessengerComponent::ServerRequestAddFriendPlayer_Implementation(AActor* TargetPlayer)
{
	const APawn* TargetPawn = Cast<APawn>(TargetPlayer);
	AMT2PlayerState* TargetState = TargetPawn
		? Cast<AMT2PlayerState>(TargetPawn->GetPlayerState()) : Cast<AMT2PlayerState>(TargetPlayer);
	const FString TargetId = GetMessengerIdFor(TargetState);
	if (TargetId.IsEmpty() || TargetId == GetOwnerCharacterId())
	{
		ClientReceiveActionResult(TEXT("add"), EMT2MessengerResult::CannotAddSelf);
		return;
	}
	if (Friends.ContainsByPredicate([&TargetId](const FMT2FriendEntry& Entry)
		{ return Entry.CharacterId == TargetId; }))
	{
		ClientReceiveActionResult(TEXT("add"), EMT2MessengerResult::AlreadyFriends);
		return;
	}
	if (Friends.Num() >= MT2Messenger::MaxFriends)
	{
		ClientReceiveActionResult(TEXT("add"), EMT2MessengerResult::ListFull);
		return;
	}

	if (HasCoordinator())
	{
		// The coordinator owns the friend graph, and knows the target by name wherever they are.
		GetRuntime()->PublishMessengerRequestAdd(
			GetOwnerCharacterId(), TargetState->GetCharacterName());
		ClientReceiveActionResult(TEXT("add"), EMT2MessengerResult::Success);
		return;
	}

	// No coordinator: ask the player directly, since they are on this server by definition - the
	// request came from clicking them in the world.
	UMT2MessengerComponent* TargetMessenger =
		TargetState ? TargetState->FindComponentByClass<UMT2MessengerComponent>() : nullptr;
	if (!TargetMessenger)
	{
		ClientReceiveActionResult(TEXT("add"), EMT2MessengerResult::UnknownCharacter);
		return;
	}
	TargetMessenger->PendingRequests.AddUnique(
		FMT2FriendRequest{GetOwnerCharacterId(), GetOwnerCharacterName()});
	TargetMessenger->ClientShowFriendRequest(GetOwnerCharacterId(), GetOwnerCharacterName());
	ClientReceiveActionResult(TEXT("add"), EMT2MessengerResult::Success);
}

void UMT2MessengerComponent::ClientShowFriendRequest_Implementation(
	const FString& RequesterId, const FString& RequesterName)
{
	ApplyIncomingRequest(RequesterId, RequesterName);
	const AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner());
	APlayerController* Controller = State ? Cast<APlayerController>(State->GetOwner()) : nullptr;
	if (AMT2HUD* HUD = Controller ? Controller->GetHUD<AMT2HUD>() : nullptr)
	{
		HUD->ShowFriendRequest(RequesterId, RequesterName);
	}
}

void UMT2MessengerComponent::AddLocalFriend(
	const FString& CompanionId, const FString& CompanionName, bool bOnline)
{
	if (CompanionId.IsEmpty() || Friends.ContainsByPredicate([&CompanionId](const FMT2FriendEntry& Entry)
		{ return Entry.CharacterId == CompanionId; }))
	{
		return;
	}
	FMT2FriendEntry& Entry = Friends.AddDefaulted_GetRef();
	Entry.CharacterId = CompanionId;
	Entry.CharacterName = CompanionName;
	Entry.bOnline = bOnline;
	OnMessengerChanged.Broadcast();
}

void UMT2MessengerComponent::ServerAnswerFriendRequest_Implementation(
	const FString& RequesterId, bool bAccept)
{
	if (HasCoordinator())
	{
		GetRuntime()->PublishMessengerAnswerRequest(GetOwnerCharacterId(), RequesterId, bAccept);
		return;
	}

	// Without a coordinator the friendship is session-only: there is no database to write it to, and
	// both players are on this server anyway.
	PendingRequests.RemoveAll([&RequesterId](const FMT2FriendRequest& Request)
	{
		return Request.FromCharacterId == RequesterId;
	});
	if (!bAccept)
	{
		OnMessengerChanged.Broadcast();
		return;
	}
	UMT2MessengerComponent* Requester = FindLocalMessenger(RequesterId);
	const FString RequesterName = Requester ? Requester->GetOwnerCharacterName() : RequesterId;
	AddLocalFriend(RequesterId, RequesterName, Requester != nullptr);
	if (Requester)
	{
		// Friendship is symmetric, so the other side gets the entry too.
		Requester->AddLocalFriend(GetOwnerCharacterId(), GetOwnerCharacterName(), true);
	}
}

void UMT2MessengerComponent::ServerRemoveFriend_Implementation(const FString& CompanionId)
{
	if (UMT2ServerRuntimeSubsystem* Runtime = GetRuntime())
	{
		Runtime->PublishMessengerRemoveFriend(GetOwnerCharacterId(), CompanionId);
	}
}

void UMT2MessengerComponent::ServerSendPrivateMessage_Implementation(
	const FString& RecipientId, const FString& Body)
{
	const FString Clean = Body.TrimStartAndEnd().Left(MT2Messenger::MaxMessageLength);
	if (Clean.IsEmpty())
	{
		return;
	}
	if (HasCoordinator())
	{
		GetRuntime()->PublishMessengerSendMessage(
			GetOwnerCharacterId(), GetOwnerCharacterName(), RecipientId, Clean);
		return;
	}

	// No coordinator (PIE, or a single map server running alone): deliver on this server. The message
	// is not stored, so it only reaches a recipient who is here and online - which is all this setup
	// can offer, and matches the old game's live-only whisper.
	UMT2MessengerComponent* Recipient = FindLocalMessenger(RecipientId);
	if (!Recipient)
	{
		ClientReceiveActionResult(TEXT("send"), EMT2MessengerResult::NotFriends);
		return;
	}

	FMT2PrivateMessage Delivered;
	Delivered.SenderCharacterId = GetOwnerCharacterId();
	Delivered.SenderName = GetOwnerCharacterName();
	Delivered.RecipientCharacterId = RecipientId;
	Delivered.Body = Clean;
	Delivered.SentUnixTime = FDateTime::UtcNow().ToUnixTimestamp();
	Recipient->ApplyIncomingMessage(Delivered);

	// The sender sees their own line too, so both halves of the conversation read the same.
	FMT2PrivateMessage Echo = Delivered;
	Echo.bRead = true;
	ClientReceiveMessage(Echo);
}

void UMT2MessengerComponent::ServerOpenConversationWithPlayer_Implementation(AActor* TargetPlayer)
{
	// The target arrives as a pawn or a PlayerState; either way the character id lives on the server
	// only, which is why this resolution cannot happen on the client.
	const APawn* TargetPawn = Cast<APawn>(TargetPlayer);
	const AMT2PlayerState* TargetState = TargetPawn
		? Cast<AMT2PlayerState>(TargetPawn->GetPlayerState()) : Cast<AMT2PlayerState>(TargetPlayer);
	const FString CompanionId = GetMessengerIdFor(TargetState);
	if (CompanionId.IsEmpty() || CompanionId == GetOwnerCharacterId())
	{
		// Not a player, or the player themselves. Answering keeps the button from looking broken.
		ClientReceiveActionResult(TEXT("whisper"), EMT2MessengerResult::UnknownCharacter);
		return;
	}
	ClientBeginConversation(CompanionId, TargetState->GetCharacterName(), TargetState->IsAdmin());
	if (HasCoordinator())
	{
		// Only the coordinator has the stored history; without one the window simply opens empty.
		GetRuntime()->PublishMessengerOpenConversation(GetOwnerCharacterId(), CompanionId);
	}
}

void UMT2MessengerComponent::ClientBeginConversation_Implementation(
	const FString& CompanionId, const FString& CompanionName, bool bCompanionIsGameMaster)
{
	// Opening from the friend list sends no name, since the list already has one; keep whatever the
	// client set rather than blanking the title.
	const bool bSameConversation = OpenConversationId == CompanionId;
	OpenConversationId = CompanionId;
	if (!CompanionName.IsEmpty() || !bSameConversation)
	{
		OpenConversationName = CompanionName.IsEmpty() ? OpenConversationName : CompanionName;
	}
	bOpenConversationIsGameMaster = bCompanionIsGameMaster;
	if (!bSameConversation)
	{
		Conversation = ConversationHistory.FindOrAdd(CompanionId);
	}
	// Reading a conversation clears its notification.
	MarkConversationRead(CompanionId);
	OnMessengerChanged.Broadcast();
}

void UMT2MessengerComponent::ServerOpenConversation_Implementation(const FString& CompanionId)
{
	if (UMT2ServerRuntimeSubsystem* Runtime = GetRuntime())
	{
		Runtime->PublishMessengerOpenConversation(GetOwnerCharacterId(), CompanionId);
	}
}

// -------------------------------------------------------------------------------------------------
// Coordinator pushes, applied on the server and forwarded to the owning client
// -------------------------------------------------------------------------------------------------

void UMT2MessengerComponent::ApplyFriendSnapshot(TArray<FMT2FriendEntry> NewFriends)
{
	Friends = MoveTemp(NewFriends);
	// Replication only reaches the owner; the listen-server host has no OnRep, so tell it directly.
	OnMessengerChanged.Broadcast();
}

void UMT2MessengerComponent::ApplyPresenceUpdate(
	const FString& CompanionId, const FString& CompanionName, bool bOnline,
	const FString& MapId, int32 Channel, int32 Level)
{
	FMT2FriendEntry* Friend = Friends.FindByPredicate([&CompanionId](const FMT2FriendEntry& Entry)
	{
		return Entry.CharacterId == CompanionId;
	});
	if (!Friend)
	{
		return;
	}
	Friend->bOnline = bOnline;
	Friend->MapId = bOnline ? MapId : FString();
	Friend->Channel = bOnline ? Channel : 0;
	if (!CompanionName.IsEmpty())
	{
		Friend->CharacterName = CompanionName;
	}
	if (Level > 0)
	{
		Friend->Level = Level;
	}
	OnMessengerChanged.Broadcast();
}

void UMT2MessengerComponent::ApplyIncomingRequest(
	const FString& RequesterId, const FString& RequesterName)
{
	if (RequesterId.IsEmpty())
	{
		return;
	}
	// Coming from the coordinator this runs on the server, so the prompt has to be pushed down; the
	// local path calls ClientShowFriendRequest itself and lands in the branch below on the client.
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		ClientShowFriendRequest(RequesterId, RequesterName);
	}
	FMT2FriendRequest Request;
	Request.FromCharacterId = RequesterId;
	Request.FromCharacterName = RequesterName;
	PendingRequests.AddUnique(Request);
	OnMessengerChanged.Broadcast();
}

void UMT2MessengerComponent::ApplyIncomingMessage(const FMT2PrivateMessage& Message)
{
	// The unread badge is the coordinator's count, but bumping it here keeps the list honest until the
	// next snapshot arrives.
	if (FMT2FriendEntry* Friend = Friends.FindByPredicate([&Message](const FMT2FriendEntry& Entry)
		{ return Entry.CharacterId == Message.SenderCharacterId; }))
	{
		if (OpenConversationId != Message.SenderCharacterId)
		{
			++Friend->UnreadCount;
		}
	}
	ClientReceiveMessage(Message);
}

void UMT2MessengerComponent::ApplyConversation(
	const FString& CompanionId, TArray<FMT2PrivateMessage> Messages)
{
	ClientReceiveConversation(CompanionId, Messages);
}

void UMT2MessengerComponent::ApplyActionResult(FName Action, EMT2MessengerResult Result)
{
	ClientReceiveActionResult(Action, Result);
}

void UMT2MessengerComponent::ClientReceiveMessage_Implementation(const FMT2PrivateMessage& Message)
{
	// A line belongs to the open conversation whichever way it went: the echo of the player's own
	// message names them as the sender and the companion as the recipient, so matching only on the
	// sender hid everything they typed.
	const bool bBelongsToOpenConversation = !OpenConversationId.IsEmpty() &&
		(Message.SenderCharacterId == OpenConversationId ||
		 Message.RecipientCharacterId == OpenConversationId);
	// Kept whether or not the window is open, so opening it later shows what was already said.
	AppendToHistory(Message);
	if (bBelongsToOpenConversation)
	{
		Conversation = ConversationHistory.FindOrAdd(OpenConversationId);
	}
	// A message counts as unread unless its conversation is the one currently open - if the player is
	// already looking at that whisper window, there is nothing to notify them about. The player's own
	// echo never counts.
	const bool bFromCompanion = Message.SenderCharacterId != GetOwnerCharacterId();
	if (bFromCompanion && !bBelongsToOpenConversation)
	{
		int32& Unread = UnreadByCompanion.FindOrAdd(Message.SenderCharacterId);
		++Unread;
		if (!Message.SenderName.IsEmpty())
		{
			UnreadSenderNames.Add(Message.SenderCharacterId, Message.SenderName);
		}
	}

	OnPrivateMessageReceived.Broadcast(Message);
	OnMessengerChanged.Broadcast();
}

void UMT2MessengerComponent::MarkConversationRead(const FString& CompanionId)
{
	if (UnreadByCompanion.Remove(CompanionId) > 0)
	{
		OnMessengerChanged.Broadcast();
	}
}

void UMT2MessengerComponent::ClientReceiveConversation_Implementation(
	const FString& CompanionId, const TArray<FMT2PrivateMessage>& Messages)
{
	// A reply for a conversation the player already closed (or switched away from) is stale.
	if (OpenConversationId != CompanionId)
	{
		return;
	}
	// The coordinator's copy is the complete one, so it replaces what this client had.
	ConversationHistory.Add(CompanionId, Messages);
	Conversation = Messages;
	OnMessengerChanged.Broadcast();
}

void UMT2MessengerComponent::ClientReceiveActionResult_Implementation(
	FName Action, EMT2MessengerResult Result)
{
	OnMessengerActionResult.Broadcast(Action, Result);
}

void UMT2MessengerComponent::OnRep_Messenger()
{
	OnMessengerChanged.Broadcast();
}
