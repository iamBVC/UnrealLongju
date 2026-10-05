/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "MT2GuildTypes.generated.h"

UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EMT2GuildPermission : uint8
{
	None = 0,
	InviteMembers = 1 << 0,
	RemoveMembers = 1 << 1,
	WriteNotice = 1 << 2,
	UseSkills = 1 << 3
};
ENUM_CLASS_FLAGS(EMT2GuildPermission);

UENUM(BlueprintType)
enum class EMT2GuildResult : uint8
{
	Success,
	Unavailable,
	InvalidName,
	NameUnavailable,
	LevelTooLow,
	AlreadyInGuild,
	NotInGuild,
	UnknownCharacter,
	PermissionDenied,
	GuildFull,
	InvitePending,
	InviteExpired,
	CannotTargetSelf,
	InvalidRank,
	LeaderCannotLeave,
	InsufficientYang,
	InvalidMark,
	MarkUploadTooSoon
};

// Compatibility rank names used by quest expressions and the earlier editor-facing guild API.
// Runtime permissions still come from the editable 15-rank table below.
UENUM(BlueprintType)
enum class EMT2GuildRank : uint8
{
	None = 0,
	Master = 1,
	Officer = 2,
	Member = 15
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2GuildRank
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Guild") int32 Rank = 15;
	UPROPERTY(BlueprintReadOnly, Category = "Guild") FString Name = TEXT("Member");
	UPROPERTY(BlueprintReadOnly, Category = "Guild", meta = (Bitmask, BitmaskEnum = "/Script/Metin2.EMT2GuildPermission"))
	int32 Permissions = 0;
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2GuildMember
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Guild") FString CharacterId;
	UPROPERTY(BlueprintReadOnly, Category = "Guild") FString CharacterName;
	UPROPERTY(BlueprintReadOnly, Category = "Guild") int32 Rank = 15;
	UPROPERTY(BlueprintReadOnly, Category = "Guild") int32 Level = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Guild") int64 ContributedExperience = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Guild") bool bOnline = false;
	UPROPERTY(BlueprintReadOnly, Category = "Guild") FString MapId;
	UPROPERTY(BlueprintReadOnly, Category = "Guild") int32 Channel = 0;
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2GuildSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Guild") int32 GuildId = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Guild") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "Guild") FString LeaderCharacterId;
	UPROPERTY(BlueprintReadOnly, Category = "Guild") int32 Level = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Guild") int64 Experience = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Guild") int32 LadderPoints = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Guild") int64 Yang = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Guild") int64 MarkRevision = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Guild") TArray<FMT2GuildRank> Ranks;
	UPROPERTY(BlueprintReadOnly, Category = "Guild") TArray<FMT2GuildMember> Members;

	bool IsValid() const { return GuildId > 0 && !Name.IsEmpty(); }
};

// Editor/quest view retained for existing quest nodes. Network gameplay uses FMT2GuildSnapshot.
USTRUCT(BlueprintType)
struct METIN2_API FMT2Guild
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Guild") int32 GuildId = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Guild") FString GuildName;
	UPROPERTY(BlueprintReadOnly, Category = "Guild") FString MasterCharacterId;
	UPROPERTY(BlueprintReadOnly, Category = "Guild") int32 Level = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Guild") int64 Experience = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Guild") int32 LadderPoints = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Guild") FString Notice;
	UPROPERTY(BlueprintReadOnly, Category = "Guild") TArray<FMT2GuildMember> Members;

	const FMT2GuildMember* FindMember(const FString& CharacterId) const
	{
		return Members.FindByPredicate([&](const FMT2GuildMember& Member)
		{
			return Member.CharacterId == CharacterId;
		});
	}
	int32 GetMemberLimit() const { return 20; }
};

namespace MT2Guild
{
	constexpr int32 RankCount = 15;
	constexpr int32 MaximumMembers = 20;
	constexpr int32 MaximumNameLength = 12;
	constexpr int64 CreationCost = 200000;
	constexpr double InviteLifetimeSeconds = 30.0;
}

namespace MT2GuildLimits
{
	constexpr int32 MinCreateLevel = 40;
	constexpr int32 MaxGuildLevel = 20;
}
