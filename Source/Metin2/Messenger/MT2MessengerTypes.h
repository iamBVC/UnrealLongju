/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "MT2MessengerTypes.generated.h"

// One entry of the player's friend list. Friendship is symmetric and mutually agreed, matching the old
// MessengerManager (RequestToAdd -> messenger_auth -> AuthToAdd adds both directions).
//
// Identity is the persistent character id, so a friendship survives a rename; the name rides along for
// display and is refreshed from the coordinator whenever presence changes.
USTRUCT(BlueprintType)
struct METIN2_API FMT2FriendEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Messenger")
	FString CharacterId;

	UPROPERTY(BlueprintReadOnly, Category = "Messenger")
	FString CharacterName;

	UPROPERTY(BlueprintReadOnly, Category = "Messenger")
	bool bOnline = false;

	// Where the friend is, when online. Empty/0 while offline. The old messenger showed only a lamp;
	// the map is useful now that friends can be on a different server.
	UPROPERTY(BlueprintReadOnly, Category = "Messenger")
	FString MapId;

	UPROPERTY(BlueprintReadOnly, Category = "Messenger")
	int32 Channel = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Messenger")
	int32 Level = 0;

	// Unread private messages waiting from this friend.
	UPROPERTY(BlueprintReadOnly, Category = "Messenger")
	int32 UnreadCount = 0;

	bool operator==(const FMT2FriendEntry& Other) const { return CharacterId == Other.CharacterId; }
};

// A pending "X wants to add you" request the player has not answered yet.
USTRUCT(BlueprintType)
struct METIN2_API FMT2FriendRequest
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Messenger")
	FString FromCharacterId;

	UPROPERTY(BlueprintReadOnly, Category = "Messenger")
	FString FromCharacterName;

	bool operator==(const FMT2FriendRequest& Other) const
	{
		return FromCharacterId == Other.FromCharacterId;
	}
};

// One private message. Unlike the old game - where a whisper to an offline player was simply refused -
// these are stored by the coordinator and delivered when the recipient next logs in, on any server.
USTRUCT(BlueprintType)
struct METIN2_API FMT2PrivateMessage
{
	GENERATED_BODY()

	// Coordinator-assigned; used to mark a message read without ambiguity.
	UPROPERTY(BlueprintReadOnly, Category = "Messenger")
	int64 MessageId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Messenger")
	FString SenderCharacterId;

	UPROPERTY(BlueprintReadOnly, Category = "Messenger")
	FString SenderName;

	UPROPERTY(BlueprintReadOnly, Category = "Messenger")
	FString RecipientCharacterId;

	UPROPERTY(BlueprintReadOnly, Category = "Messenger")
	FString Body;

	// Seconds since the Unix epoch, so the client can render a local timestamp.
	UPROPERTY(BlueprintReadOnly, Category = "Messenger")
	int64 SentUnixTime = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Messenger")
	bool bRead = false;

	// True when the message sat in the store before being shown, i.e. the player was offline for it.
	UPROPERTY(BlueprintReadOnly, Category = "Messenger")
	bool bWasOffline = false;
};

// Why a messenger action was refused. The old client printed a plain chat line for each of these.
UENUM(BlueprintType)
enum class EMT2MessengerResult : uint8
{
	Success,
	UnknownCharacter,   // no character by that name
	AlreadyFriends,
	RequestPending,     // a request to this character is already outstanding
	CannotAddSelf,
	ListFull,
	NotFriends,         // messaging or removing someone who is not on the list
	MessageTooLong,
	Unavailable         // coordinator not reachable; the action was not performed
};

// The old client's list capped at 32 companions (uimessenger.py builds a fixed-height list).
namespace MT2Messenger
{
	constexpr int32 MaxFriends = 32;
	constexpr int32 MaxMessageLength = 256;
	// How much of a conversation the client keeps; older messages stay in the store.
	constexpr int32 MaxConversationHistory = 64;
}
