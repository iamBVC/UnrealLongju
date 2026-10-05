/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Guild/MT2GuildTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MT2GuildSubsystem.generated.h"

class FJsonObject;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMT2GuildUpdatedSignature, int32, GuildId);

// Server-side registry of every guild on this map server: creation, membership, ranks, levelling and
// disbanding. Guilds are keyed by id; a player's membership is mirrored onto their PlayerState
// (GuildId/GuildName) so nameplates and the character window keep working unchanged.
//
// Guild records live here rather than on the player so a guild keeps existing while its members are
// offline. Persistence is a plain JSON snapshot next to the other server data.
UCLASS()
class METIN2_API UMT2GuildSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ---- lookup ----
	UFUNCTION(BlueprintPure, Category = "Guild")
	bool GetGuild(int32 GuildId, FMT2Guild& OutGuild) const;

	// The guild a character belongs to, or 0.
	UFUNCTION(BlueprintPure, Category = "Guild")
	int32 FindGuildOfCharacter(const FString& CharacterId) const;

	UFUNCTION(BlueprintPure, Category = "Guild")
	int32 GetGuildLevel(int32 GuildId) const;

	UFUNCTION(BlueprintPure, Category = "Guild")
	FString GetGuildName(int32 GuildId) const;

	UFUNCTION(BlueprintPure, Category = "Guild")
	bool IsGuildMaster(int32 GuildId, const FString& CharacterId) const;

	UFUNCTION(BlueprintPure, Category = "Guild")
	EMT2GuildRank GetMemberRank(int32 GuildId, const FString& CharacterId) const;

	// ---- operations (server authority) ----
	// Founds a guild with the character as its master. CharacterLevel gates it like the old game.
	EMT2GuildResult CreateGuild(
		const FString& GuildName, const FString& MasterCharacterId, const FString& MasterName,
		int32 CharacterLevel, int32& OutGuildId);

	EMT2GuildResult AddMember(
		int32 GuildId, const FString& CharacterId, const FString& CharacterName, int32 CharacterLevel);

	// Leaving or being expelled. The master cannot leave without handing the guild over first.
	EMT2GuildResult RemoveMember(int32 GuildId, const FString& CharacterId);

	EMT2GuildResult SetMemberRank(
		int32 GuildId, const FString& ActorCharacterId, const FString& TargetCharacterId,
		EMT2GuildRank NewRank);

	// Hands mastery to another member; the old master drops to Officer.
	EMT2GuildResult TransferMastery(
		int32 GuildId, const FString& CurrentMasterId, const FString& NewMasterId);

	EMT2GuildResult DisbandGuild(int32 GuildId, const FString& ActorCharacterId);

	EMT2GuildResult SetNotice(int32 GuildId, const FString& ActorCharacterId, const FString& Notice);

	// Guild experience; levels up while the threshold is met. Returns the new level.
	int32 AddGuildExperience(int32 GuildId, const FString& ContributorCharacterId, int64 Amount);

	// Experience needed to reach the next level from Level.
	UFUNCTION(BlueprintPure, Category = "Guild")
	int64 GetExperienceForLevel(int32 Level) const;

	UPROPERTY(BlueprintAssignable, Category = "Guild")
	FMT2GuildUpdatedSignature OnGuildUpdated;

	// ---- persistence ----
	void SaveGuilds();
	void LoadGuilds();

private:
	FMT2Guild* FindGuildMutable(int32 GuildId);
	const FMT2Guild* FindGuild(int32 GuildId) const;
	bool IsNameTaken(const FString& GuildName) const;
	void BroadcastUpdate(int32 GuildId);
	FString GetSaveFilePath() const;

	UPROPERTY()
	TArray<FMT2Guild> Guilds;

	// Next id to hand out; persisted so ids are never reused after a restart.
	int32 NextGuildId = 1;
	bool bTransientPIESession = false;
};
