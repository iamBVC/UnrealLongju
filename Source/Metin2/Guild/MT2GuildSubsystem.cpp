/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Guild/MT2GuildSubsystem.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogMT2Guild, Log, All);

namespace
{
	int64 ReadInt64JsonField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName)
	{
		FString StringValue;
		if (Object && Object->TryGetStringField(FieldName, StringValue))
		{
			return FCString::Atoi64(*StringValue);
		}
		double NumberValue = 0.0;
		return Object && Object->TryGetNumberField(FieldName, NumberValue)
			? static_cast<int64>(NumberValue) : 0;
	}
}

void UMT2GuildSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bTransientPIESession = GetWorld() && GetWorld()->WorldType == EWorldType::PIE;
	LoadGuilds();
}

void UMT2GuildSubsystem::Deinitialize()
{
	SaveGuilds();
	Super::Deinitialize();
}

FMT2Guild* UMT2GuildSubsystem::FindGuildMutable(int32 GuildId)
{
	return Guilds.FindByPredicate(
		[GuildId](const FMT2Guild& Guild) { return Guild.GuildId == GuildId; });
}

const FMT2Guild* UMT2GuildSubsystem::FindGuild(int32 GuildId) const
{
	return Guilds.FindByPredicate(
		[GuildId](const FMT2Guild& Guild) { return Guild.GuildId == GuildId; });
}

bool UMT2GuildSubsystem::GetGuild(int32 GuildId, FMT2Guild& OutGuild) const
{
	if (const FMT2Guild* Guild = FindGuild(GuildId))
	{
		OutGuild = *Guild;
		return true;
	}
	return false;
}

int32 UMT2GuildSubsystem::FindGuildOfCharacter(const FString& CharacterId) const
{
	if (CharacterId.IsEmpty())
	{
		return 0;
	}
	for (const FMT2Guild& Guild : Guilds)
	{
		if (Guild.FindMember(CharacterId))
		{
			return Guild.GuildId;
		}
	}
	return 0;
}

int32 UMT2GuildSubsystem::GetGuildLevel(int32 GuildId) const
{
	const FMT2Guild* Guild = FindGuild(GuildId);
	return Guild ? Guild->Level : 0;
}

FString UMT2GuildSubsystem::GetGuildName(int32 GuildId) const
{
	const FMT2Guild* Guild = FindGuild(GuildId);
	return Guild ? Guild->GuildName : FString();
}

bool UMT2GuildSubsystem::IsGuildMaster(int32 GuildId, const FString& CharacterId) const
{
	const FMT2Guild* Guild = FindGuild(GuildId);
	return Guild && !CharacterId.IsEmpty() && Guild->MasterCharacterId == CharacterId;
}

EMT2GuildRank UMT2GuildSubsystem::GetMemberRank(int32 GuildId, const FString& CharacterId) const
{
	const FMT2Guild* Guild = FindGuild(GuildId);
	const FMT2GuildMember* Member = Guild ? Guild->FindMember(CharacterId) : nullptr;
	return Member ? static_cast<EMT2GuildRank>(Member->Rank) : EMT2GuildRank::Member;
}

bool UMT2GuildSubsystem::IsNameTaken(const FString& GuildName) const
{
	return Guilds.ContainsByPredicate([&GuildName](const FMT2Guild& Guild)
	{
		return Guild.GuildName.Equals(GuildName, ESearchCase::IgnoreCase);
	});
}

int64 UMT2GuildSubsystem::GetExperienceForLevel(int32 Level) const
{
	// Old game's curve shape: each level costs progressively more. Level 1 -> 2 costs 5,000.
	const int32 Clamped = FMath::Clamp(Level, 1, MT2GuildLimits::MaxGuildLevel);
	return static_cast<int64>(5000) * Clamped * Clamped;
}

EMT2GuildResult UMT2GuildSubsystem::CreateGuild(
	const FString& GuildName, const FString& MasterCharacterId, const FString& MasterName,
	int32 CharacterLevel, int32& OutGuildId)
{
	OutGuildId = 0;
	const FString TrimmedName = GuildName.TrimStartAndEnd();
	if (TrimmedName.IsEmpty() || TrimmedName.Len() > 12)
	{
		return EMT2GuildResult::InvalidName;
	}
	if (MasterCharacterId.IsEmpty())
	{
		return EMT2GuildResult::UnknownCharacter;
	}
	if (CharacterLevel < MT2GuildLimits::MinCreateLevel)
	{
		return EMT2GuildResult::LevelTooLow;
	}
	if (FindGuildOfCharacter(MasterCharacterId) != 0)
	{
		return EMT2GuildResult::AlreadyInGuild;
	}
	if (IsNameTaken(TrimmedName))
	{
		return EMT2GuildResult::NameUnavailable;
	}

	FMT2Guild& Guild = Guilds.AddDefaulted_GetRef();
	Guild.GuildId = NextGuildId++;
	Guild.GuildName = TrimmedName;
	Guild.Level = 1;
	Guild.MasterCharacterId = MasterCharacterId;

	FMT2GuildMember& Master = Guild.Members.AddDefaulted_GetRef();
	Master.CharacterId = MasterCharacterId;
	Master.CharacterName = MasterName;
	Master.Level = CharacterLevel;
	Master.Rank = static_cast<int32>(EMT2GuildRank::Master);

	OutGuildId = Guild.GuildId;
	BroadcastUpdate(Guild.GuildId);
	SaveGuilds();
	UE_LOG(LogMT2Guild, Display, TEXT("Guild '%s' (%d) founded by %s."),
		*Guild.GuildName, Guild.GuildId, *MasterName);
	return EMT2GuildResult::Success;
}

EMT2GuildResult UMT2GuildSubsystem::AddMember(
	int32 GuildId, const FString& CharacterId, const FString& CharacterName, int32 CharacterLevel)
{
	FMT2Guild* Guild = FindGuildMutable(GuildId);
	if (!Guild)
	{
		return EMT2GuildResult::NotInGuild;
	}
	if (CharacterId.IsEmpty())
	{
		return EMT2GuildResult::UnknownCharacter;
	}
	if (FindGuildOfCharacter(CharacterId) != 0)
	{
		return EMT2GuildResult::AlreadyInGuild;
	}
	if (Guild->Members.Num() >= Guild->GetMemberLimit())
	{
		return EMT2GuildResult::GuildFull;
	}

	FMT2GuildMember& Member = Guild->Members.AddDefaulted_GetRef();
	Member.CharacterId = CharacterId;
	Member.CharacterName = CharacterName;
	Member.Level = CharacterLevel;
	Member.Rank = static_cast<int32>(EMT2GuildRank::Member);

	BroadcastUpdate(GuildId);
	SaveGuilds();
	return EMT2GuildResult::Success;
}

EMT2GuildResult UMT2GuildSubsystem::RemoveMember(int32 GuildId, const FString& CharacterId)
{
	FMT2Guild* Guild = FindGuildMutable(GuildId);
	if (!Guild)
	{
		return EMT2GuildResult::NotInGuild;
	}
	if (!Guild->FindMember(CharacterId))
	{
		return EMT2GuildResult::NotInGuild;
	}
	// The master must hand the guild over (or disband) rather than leave it headless.
	if (Guild->MasterCharacterId == CharacterId)
	{
		return EMT2GuildResult::LeaderCannotLeave;
	}

	Guild->Members.RemoveAll(
		[&CharacterId](const FMT2GuildMember& Member) { return Member.CharacterId == CharacterId; });
	BroadcastUpdate(GuildId);
	SaveGuilds();
	return EMT2GuildResult::Success;
}

EMT2GuildResult UMT2GuildSubsystem::SetMemberRank(
	int32 GuildId, const FString& ActorCharacterId, const FString& TargetCharacterId,
	EMT2GuildRank NewRank)
{
	FMT2Guild* Guild = FindGuildMutable(GuildId);
	if (!Guild)
	{
		return EMT2GuildResult::NotInGuild;
	}
	// Only the master promotes, and mastery is transferred explicitly, never assigned as a rank.
	if (Guild->MasterCharacterId != ActorCharacterId || NewRank == EMT2GuildRank::Master)
	{
		return EMT2GuildResult::PermissionDenied;
	}
	FMT2GuildMember* Target = Guild->Members.FindByPredicate(
		[&TargetCharacterId](const FMT2GuildMember& Member) { return Member.CharacterId == TargetCharacterId; });
	if (!Target)
	{
		return EMT2GuildResult::UnknownCharacter;
	}
	if (Target->CharacterId == Guild->MasterCharacterId)
	{
		return EMT2GuildResult::PermissionDenied;
	}
	Target->Rank = static_cast<int32>(NewRank);
	BroadcastUpdate(GuildId);
	SaveGuilds();
	return EMT2GuildResult::Success;
}

EMT2GuildResult UMT2GuildSubsystem::TransferMastery(
	int32 GuildId, const FString& CurrentMasterId, const FString& NewMasterId)
{
	FMT2Guild* Guild = FindGuildMutable(GuildId);
	if (!Guild)
	{
		return EMT2GuildResult::NotInGuild;
	}
	if (Guild->MasterCharacterId != CurrentMasterId)
	{
		return EMT2GuildResult::PermissionDenied;
	}
	FMT2GuildMember* NewMaster = Guild->Members.FindByPredicate(
		[&NewMasterId](const FMT2GuildMember& Member) { return Member.CharacterId == NewMasterId; });
	if (!NewMaster)
	{
		return EMT2GuildResult::UnknownCharacter;
	}
	if (FMT2GuildMember* OldMaster = Guild->Members.FindByPredicate(
		[&CurrentMasterId](const FMT2GuildMember& Member) { return Member.CharacterId == CurrentMasterId; }))
	{
		OldMaster->Rank = static_cast<int32>(EMT2GuildRank::Officer);
	}
	NewMaster->Rank = static_cast<int32>(EMT2GuildRank::Master);
	Guild->MasterCharacterId = NewMasterId;
	BroadcastUpdate(GuildId);
	SaveGuilds();
	return EMT2GuildResult::Success;
}

EMT2GuildResult UMT2GuildSubsystem::DisbandGuild(int32 GuildId, const FString& ActorCharacterId)
{
	const FMT2Guild* Guild = FindGuild(GuildId);
	if (!Guild)
	{
		return EMT2GuildResult::NotInGuild;
	}
	if (Guild->MasterCharacterId != ActorCharacterId)
	{
		return EMT2GuildResult::PermissionDenied;
	}
	Guilds.RemoveAll([GuildId](const FMT2Guild& Entry) { return Entry.GuildId == GuildId; });
	BroadcastUpdate(GuildId); // listeners clear their cached membership
	SaveGuilds();
	return EMT2GuildResult::Success;
}

EMT2GuildResult UMT2GuildSubsystem::SetNotice(
	int32 GuildId, const FString& ActorCharacterId, const FString& Notice)
{
	FMT2Guild* Guild = FindGuildMutable(GuildId);
	if (!Guild)
	{
		return EMT2GuildResult::NotInGuild;
	}
	const FMT2GuildMember* Actor = Guild->FindMember(ActorCharacterId);
	if (!Actor || Actor->Rank == static_cast<int32>(EMT2GuildRank::Member))
	{
		return EMT2GuildResult::PermissionDenied;
	}
	Guild->Notice = Notice.Left(256);
	BroadcastUpdate(GuildId);
	SaveGuilds();
	return EMT2GuildResult::Success;
}

int32 UMT2GuildSubsystem::AddGuildExperience(
	int32 GuildId, const FString& ContributorCharacterId, int64 Amount)
{
	FMT2Guild* Guild = FindGuildMutable(GuildId);
	if (!Guild || Amount <= 0)
	{
		return Guild ? Guild->Level : 0;
	}
	Guild->Experience += Amount;
	if (FMT2GuildMember* Contributor = Guild->Members.FindByPredicate(
		[&ContributorCharacterId](const FMT2GuildMember& Member)
		{ return Member.CharacterId == ContributorCharacterId; }))
	{
		Contributor->ContributedExperience += Amount;
	}

	// Consume thresholds one level at a time so a single large contribution can cross several.
	while (Guild->Level < MT2GuildLimits::MaxGuildLevel)
	{
		const int64 Needed = GetExperienceForLevel(Guild->Level);
		if (Guild->Experience < Needed)
		{
			break;
		}
		Guild->Experience -= Needed;
		++Guild->Level;
	}
	if (Guild->Level >= MT2GuildLimits::MaxGuildLevel)
	{
		Guild->Experience = 0;
	}

	const int32 NewLevel = Guild->Level;
	BroadcastUpdate(GuildId);
	SaveGuilds();
	return NewLevel;
}

void UMT2GuildSubsystem::BroadcastUpdate(int32 GuildId)
{
	OnGuildUpdated.Broadcast(GuildId);
}

FString UMT2GuildSubsystem::GetSaveFilePath() const
{
	return FPaths::ProjectSavedDir() / TEXT("MT2Guilds.json");
}

void UMT2GuildSubsystem::SaveGuilds()
{
	if (bTransientPIESession) return;

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("next_id"), NextGuildId);

	TArray<TSharedPtr<FJsonValue>> GuildValues;
	for (const FMT2Guild& Guild : Guilds)
	{
		TSharedRef<FJsonObject> GuildObject = MakeShared<FJsonObject>();
		GuildObject->SetNumberField(TEXT("id"), Guild.GuildId);
		GuildObject->SetStringField(TEXT("name"), Guild.GuildName);
		GuildObject->SetNumberField(TEXT("level"), Guild.Level);
		GuildObject->SetStringField(TEXT("exp"), LexToString(Guild.Experience));
		GuildObject->SetStringField(TEXT("master"), Guild.MasterCharacterId);
		GuildObject->SetStringField(TEXT("notice"), Guild.Notice);
		GuildObject->SetNumberField(TEXT("ladder"), Guild.LadderPoints);

		TArray<TSharedPtr<FJsonValue>> MemberValues;
		for (const FMT2GuildMember& Member : Guild.Members)
		{
			TSharedRef<FJsonObject> MemberObject = MakeShared<FJsonObject>();
			MemberObject->SetStringField(TEXT("id"), Member.CharacterId);
			MemberObject->SetStringField(TEXT("name"), Member.CharacterName);
			MemberObject->SetNumberField(TEXT("level"), Member.Level);
			MemberObject->SetNumberField(TEXT("rank"), static_cast<int32>(Member.Rank));
			MemberObject->SetStringField(TEXT("contrib"), LexToString(Member.ContributedExperience));
			MemberValues.Add(MakeShared<FJsonValueObject>(MemberObject));
		}
		GuildObject->SetArrayField(TEXT("members"), MemberValues);
		GuildValues.Add(MakeShared<FJsonValueObject>(GuildObject));
	}
	Root->SetArrayField(TEXT("guilds"), GuildValues);

	FString Payload;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Payload);
	FJsonSerializer::Serialize(Root, Writer);
	FFileHelper::SaveStringToFile(Payload, *GetSaveFilePath());
}

void UMT2GuildSubsystem::LoadGuilds()
{
	Guilds.Reset();
	NextGuildId = 1;
	if (bTransientPIESession) return;

	FString Payload;
	if (!FFileHelper::LoadFileToString(Payload, *GetSaveFilePath()))
	{
		return; // no guilds yet
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Payload);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogMT2Guild, Warning, TEXT("Guild save file is unreadable; starting with no guilds."));
		return;
	}

	double NextId = 1.0;
	Root->TryGetNumberField(TEXT("next_id"), NextId);
	NextGuildId = FMath::Max(1, FMath::RoundToInt(NextId));

	const TArray<TSharedPtr<FJsonValue>>* GuildValues = nullptr;
	if (!Root->TryGetArrayField(TEXT("guilds"), GuildValues) || !GuildValues)
	{
		return;
	}
	for (const TSharedPtr<FJsonValue>& Value : *GuildValues)
	{
		const TSharedPtr<FJsonObject> GuildObject = Value.IsValid() ? Value->AsObject() : nullptr;
		if (!GuildObject.IsValid())
		{
			continue;
		}
		FMT2Guild& Guild = Guilds.AddDefaulted_GetRef();
		Guild.GuildId = FMath::RoundToInt(GuildObject->GetNumberField(TEXT("id")));
		Guild.GuildName = GuildObject->GetStringField(TEXT("name"));
		Guild.Level = FMath::RoundToInt(GuildObject->GetNumberField(TEXT("level")));
		Guild.Experience = ReadInt64JsonField(GuildObject, TEXT("exp"));
		Guild.MasterCharacterId = GuildObject->GetStringField(TEXT("master"));
		GuildObject->TryGetStringField(TEXT("notice"), Guild.Notice);
		double Ladder = 0.0;
		GuildObject->TryGetNumberField(TEXT("ladder"), Ladder);
		Guild.LadderPoints = FMath::RoundToInt(Ladder);

		const TArray<TSharedPtr<FJsonValue>>* MemberValues = nullptr;
		if (GuildObject->TryGetArrayField(TEXT("members"), MemberValues) && MemberValues)
		{
			for (const TSharedPtr<FJsonValue>& MemberValue : *MemberValues)
			{
				const TSharedPtr<FJsonObject> MemberObject =
					MemberValue.IsValid() ? MemberValue->AsObject() : nullptr;
				if (!MemberObject.IsValid())
				{
					continue;
				}
				FMT2GuildMember& Member = Guild.Members.AddDefaulted_GetRef();
				Member.CharacterId = MemberObject->GetStringField(TEXT("id"));
				Member.CharacterName = MemberObject->GetStringField(TEXT("name"));
				Member.Level = FMath::RoundToInt(MemberObject->GetNumberField(TEXT("level")));
				Member.Rank = static_cast<int32>(
					FMath::RoundToInt(MemberObject->GetNumberField(TEXT("rank"))));
				Member.ContributedExperience = ReadInt64JsonField(MemberObject, TEXT("contrib"));
			}
		}
		// Never hand out an id that already exists.
		NextGuildId = FMath::Max(NextGuildId, Guild.GuildId + 1);
	}
	UE_LOG(LogMT2Guild, Display, TEXT("Loaded %d guild(s)."), Guilds.Num());
}
