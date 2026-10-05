/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Persistence/MT2PersistenceTypes.h"
#include "Messenger/MT2MessengerTypes.h"
#include "Guild/MT2GuildTypes.h"
#include "Server/MT2ServerRuntimeTypes.h"

struct FMT2PersistenceBackendConfig
{
	FString RootDirectory;
	FString Filename = TEXT("metin2.db");
	FString SynchronousMode = TEXT("FULL");
	int32 BusyTimeoutMilliseconds = 5000;
	bool bCheckIntegrity = true;
};

class FMT2PersistenceBackend : public TSharedFromThis<FMT2PersistenceBackend, ESPMode::ThreadSafe>
{
public:
	explicit FMT2PersistenceBackend(FMT2PersistenceBackendConfig InConfig);
	~FMT2PersistenceBackend();

	bool Initialize(FString& OutError);
	bool Flush(FString& OutError);
	FMT2PersistenceLoadResult Load(FName EntityType, const FString& EntityId);
	FMT2PersistenceSaveResult Save(const FMT2PersistentRecord& Record);
	FMT2AccountRegistrationResult RegisterAccount(
		const FString& Username, const FString& Salt, const FString& Hash);
	FMT2AccountLoginResult AuthenticateAccount(const FString& Username, const FString& Password);
	FMT2CharacterCreateResult CreateCharacter(
		const FString& AccountId, const FString& CharacterName,
		const FMT2CharacterAppearance& Appearance, EMT2Empire Empire, const FString& InitialMapId);
	FMT2CharacterCreateResult FindCharacter(const FString& AccountId, const FString& CharacterId);
	bool UpdateCharacterLocation(
		const FString& CharacterId, const FString& MapId, int32 Channel, FString& OutError);

	// ---- messenger ----
	// Friend rows are symmetric: both directions are written and removed together, so either side of a
	// pair reads the same list. Names and levels are joined from players rather than copied, so a
	// rename or a level-up shows up without a migration.
	TArray<FMT2FriendEntry> LoadFriends(const FString& CharacterId);
	bool AddFriendPair(const FString& CharacterId, const FString& CompanionId, FString& OutError);
	bool RemoveFriendPair(const FString& CharacterId, const FString& CompanionId, FString& OutError);
	bool AreFriends(const FString& CharacterId, const FString& CompanionId);
	// Resolves a display name to its persistent character id (names are unique, case-insensitively).
	bool FindCharacterIdByName(const FString& CharacterName, FString& OutCharacterId, FString& OutName);

	// Returns the stored message id, or 0 on failure.
	int64 StorePrivateMessage(const FMT2PrivateMessage& PrivateMessage, FString& OutError);
	// Everything this character has not read yet, oldest first - the backlog shown at login.
	TArray<FMT2PrivateMessage> LoadUndeliveredMessages(const FString& CharacterId);
	// The recent conversation between two characters, newest last.
	TArray<FMT2PrivateMessage> LoadConversation(
		const FString& CharacterId, const FString& CompanionId, int32 MaxMessages);
	void MarkMessagesRead(const FString& CharacterId, const FString& CompanionId);
	void MarkMessagesDelivered(const TArray<int64>& MessageIds);

	// ---- guilds ----
	bool LoadGuildForCharacter(const FString& CharacterId, FMT2GuildSnapshot& OutGuild);
	bool LoadGuild(int32 GuildId, FMT2GuildSnapshot& OutGuild);
	EMT2GuildResult CreateGuild(
		const FString& LeaderCharacterId, const FString& GuildName, FMT2GuildSnapshot& OutGuild);
	EMT2GuildResult AddGuildMember(int32 GuildId, const FString& CharacterId, int32 Rank);
	EMT2GuildResult RemoveGuildMember(int32 GuildId, const FString& CharacterId);
	EMT2GuildResult SetGuildMemberRank(int32 GuildId, const FString& CharacterId, int32 Rank);
	EMT2GuildResult SetGuildRank(
		int32 GuildId, int32 Rank, const FString& Name, int32 Permissions);
	EMT2GuildResult DisbandGuild(int32 GuildId);

private:
	struct FImpl;
	TUniquePtr<FImpl> Impl;
	FMT2PersistenceBackendConfig Config;
};
