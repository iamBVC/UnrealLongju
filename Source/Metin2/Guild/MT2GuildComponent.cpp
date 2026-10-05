/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Guild/MT2GuildComponent.h"
#include "Core/MT2ActorUtils.h"

#include "Characters/MT2PlayerCharacter.h"
#include "Guild/MT2GuildSubsystem.h"
#include "Guild/MT2GuildMarkCacheSubsystem.h"
#include "ImageUtils.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Base64.h"
#include "Modules/ModuleManager.h"
#include "Net/UnrealNetwork.h"
#include "Persistence/MT2PersistenceComponent.h"
#include "Player/MT2PlayerState.h"
#include "Player/MT2PlayerController.h"
#include "Server/MT2ServerRuntimeSubsystem.h"
#include "TimerManager.h"
#include "UI/MT2HUD.h"

UMT2GuildComponent::UMT2GuildComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UMT2GuildComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetOwner() && GetOwner()->HasAuthority() && GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(
			LoginTimer, this, &UMT2GuildComponent::TryAnnounceLogin, 0.5f, true, 0.5f);
	}
}

void UMT2GuildComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UMT2GuildComponent, Snapshot, COND_OwnerOnly);
}

FString UMT2GuildComponent::GetCharacterId() const
{
	const AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner());
	const UMT2PersistenceComponent* Persistence = State ? State->GetPersistenceComponent() : nullptr;
	const FString Id = Persistence ? Persistence->GetEntityId() : FString();
	return !Id.IsEmpty() ? Id : (State ? State->GetCharacterName() : FString());
}

UMT2ServerRuntimeSubsystem* UMT2GuildComponent::GetRuntime() const
{
	const UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UMT2ServerRuntimeSubsystem>() : nullptr;
}

void UMT2GuildComponent::TryAnnounceLogin()
{
	const FString CharacterId = GetCharacterId();
	if (CharacterId.IsEmpty()) return;
	GetWorld()->GetTimerManager().ClearTimer(LoginTimer);
	if (UMT2ServerRuntimeSubsystem* Runtime = GetRuntime(); Runtime && Runtime->IsCoordinatorConnected())
	{
		Runtime->PublishGuildLogin(CharacterId);
	}
}

bool UMT2GuildComponent::IsLeader() const
{
	return Snapshot.IsValid() && Snapshot.LeaderCharacterId == GetCharacterId();
}

bool UMT2GuildComponent::HasPermission(EMT2GuildPermission Permission) const
{
	const FString CharacterId = GetCharacterId();
	const FMT2GuildMember* Member = Snapshot.Members.FindByPredicate(
		[&](const FMT2GuildMember& Entry) { return Entry.CharacterId == CharacterId; });
	const FMT2GuildRank* Rank = Member ? Snapshot.Ranks.FindByPredicate(
		[&](const FMT2GuildRank& Entry) { return Entry.Rank == Member->Rank; }) : nullptr;
	return Rank && (Rank->Permissions & static_cast<int32>(Permission)) != 0;
}

void UMT2GuildComponent::CreateGuild(const FString& GuildName)
{
	ServerCreateGuild(GuildName.TrimStartAndEnd().Left(MT2Guild::MaximumNameLength));
}

void UMT2GuildComponent::InvitePlayer(AActor* TargetPlayer)
{
	if (TargetPlayer) ServerInvitePlayer(TargetPlayer);
}

void UMT2GuildComponent::AnswerInvite(bool bAccept)
{
	if (!PendingInviterId.IsEmpty())
	{
		const FString Inviter = PendingInviterId;
		PendingInviterId.Reset();
		ServerAnswerInvite(Inviter, bAccept);
	}
}

void UMT2GuildComponent::LeaveGuild() { ServerGuildAction(TEXT("leave"), FString(), 0, FString(), 0); }
void UMT2GuildComponent::DisbandGuild() { ServerGuildAction(TEXT("disband"), FString(), 0, FString(), 0); }
void UMT2GuildComponent::RemoveMember(const FString& CharacterId) { ServerGuildAction(TEXT("remove"), CharacterId, 0, FString(), 0); }
void UMT2GuildComponent::ChangeMemberRank(const FString& CharacterId, int32 Rank) { ServerGuildAction(TEXT("member_rank"), CharacterId, Rank, FString(), 0); }
void UMT2GuildComponent::ChangeRank(int32 Rank, const FString& Name, int32 Permissions) { ServerGuildAction(TEXT("rank"), FString(), Rank, Name, Permissions); }

void UMT2GuildComponent::SendGuildChat(const FString& Message)
{
	const FString Clean = Message.TrimStartAndEnd().Left(256);
	if (!Clean.IsEmpty()) ServerGuildAction(TEXT("chat"), FString(), 0, Clean, 0);
}

namespace
{
	constexpr int32 GuildMarkWidth = 64;
	constexpr int32 GuildMarkHeight = 48;
	constexpr int32 MaximumSourceBytes = 4 * 1024 * 1024;
	constexpr int32 MaximumSourceDimension = 4096;

	bool NormalizeGuildMark(const TArray<uint8>& Input, TArray<uint8>& Output)
	{
		if (Input.IsEmpty() || Input.Num() > MaximumSourceBytes) return false;
		FImage Source;
		IImageWrapperModule& Wrapper = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		if (!Wrapper.DecompressImage(Input.GetData(), Input.Num(), Source) ||
			Source.SizeX <= 0 || Source.SizeY <= 0 || Source.SizeX > MaximumSourceDimension || Source.SizeY > MaximumSourceDimension)
			return false;
		TArray<FColor> SourceColors;
		Source.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		SourceColors.SetNumUninitialized(Source.SizeX * Source.SizeY);
		FMemory::Memcpy(SourceColors.GetData(), Source.RawData.GetData(), SourceColors.Num() * sizeof(FColor));
		TArray<FColor> Resized;
		FImageUtils::ImageResize(Source.SizeX, Source.SizeY, SourceColors, GuildMarkWidth, GuildMarkHeight, Resized, true, false);
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(GuildMarkWidth, GuildMarkHeight, Resized, Png);
		if (Png.IsEmpty() || Png.Num() > 64 * 1024) return false;
		Output.Append(Png.GetData(), Png.Num());
		return true;
	}
}

void UMT2GuildComponent::UploadGuildMarkFromFile(const FString& FilePath)
{
	TArray<uint8> Source;
	TArray<uint8> Normalized;
	if (!IsLeader() || !FFileHelper::LoadFileToArray(Source, *FilePath) || !NormalizeGuildMark(Source, Normalized))
	{
		ApplyResult(TEXT("mark"), IsLeader() ? EMT2GuildResult::InvalidMark : EMT2GuildResult::PermissionDenied);
		return;
	}
	ServerUploadGuildMark(Normalized);
}

void UMT2GuildComponent::RequestGuildMark(int32 GuildId, int64 KnownRevision)
{
	if (UMT2GuildMarkCacheSubsystem* Cache = GetWorld() && GetWorld()->GetGameInstance()
		? GetWorld()->GetGameInstance()->GetSubsystem<UMT2GuildMarkCacheSubsystem>() : nullptr;
		Cache && Cache->NeedsRequest(GuildId, KnownRevision))
	{
		ServerRequestGuildMark(GuildId, KnownRevision);
	}
}

void UMT2GuildComponent::ApplyGuildMark(int32 GuildId, int64 Revision, const TArray<uint8>& PngData)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		ClientReceiveGuildMark(GuildId, Revision, PngData);
		return;
	}
	if (UMT2GuildMarkCacheSubsystem* Cache = GetWorld() && GetWorld()->GetGameInstance()
		? GetWorld()->GetGameInstance()->GetSubsystem<UMT2GuildMarkCacheSubsystem>() : nullptr)
	{
		Cache->StoreMark(GuildId, Revision, PngData);
		OnGuildChanged.Broadcast();
	}
}

void UMT2GuildComponent::ClientReceiveGuildMark_Implementation(int32 GuildId, int64 Revision, const TArray<uint8>& PngData)
{
	ApplyGuildMark(GuildId, Revision, PngData);
}

void UMT2GuildComponent::ServerUploadGuildMark_Implementation(const TArray<uint8>& PngData)
{
	TArray<uint8> Validated;
	if (!IsLeader()) { ApplyResult(TEXT("mark"), EMT2GuildResult::PermissionDenied); return; }
	if (!NormalizeGuildMark(PngData, Validated)) { ApplyResult(TEXT("mark"), EMT2GuildResult::InvalidMark); return; }
	if (UMT2ServerRuntimeSubsystem* Runtime = GetRuntime(); Runtime && Runtime->IsCoordinatorConnected())
		Runtime->PublishGuildMarkUpload(GetCharacterId(), Validated);
	else if (GetWorld() && GetWorld()->WorldType == EWorldType::PIE)
	{
		ApplyGuildMark(Snapshot.GuildId, FDateTime::UtcNow().ToUnixTimestamp(), Validated);
		ApplyResult(TEXT("mark"), EMT2GuildResult::Success);
	}
	else ApplyResult(TEXT("mark"), EMT2GuildResult::Unavailable);
}

void UMT2GuildComponent::ServerRequestGuildMark_Implementation(int32 GuildId, int64 KnownRevision)
{
	if (GuildId <= 0) return;
	if (UMT2ServerRuntimeSubsystem* Runtime = GetRuntime(); Runtime && Runtime->IsCoordinatorConnected())
		Runtime->PublishGuildMarkRequest(GetCharacterId(), GuildId, KnownRevision);
}

void UMT2GuildComponent::ServerCreateGuild_Implementation(const FString& GuildName)
{
	AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner());
	if (!State || Snapshot.IsValid()) { ApplyResult(TEXT("create"), EMT2GuildResult::AlreadyInGuild); return; }
	if (State->GetYang() < MT2Guild::CreationCost) { ApplyResult(TEXT("create"), EMT2GuildResult::InsufficientYang); return; }
	if (UMT2ServerRuntimeSubsystem* Runtime = GetRuntime(); Runtime && Runtime->IsCoordinatorConnected())
	{
		Runtime->PublishGuildCreate(GetCharacterId(), GuildName);
		return;
	}

	// PIE deliberately has no coordinator process. Use the existing in-memory guild registry so the
	// complete UI/gameplay flow remains testable without weakening the production authority path.
	if (GetWorld() && GetWorld()->WorldType == EWorldType::PIE)
	{
		UMT2GuildSubsystem* Guilds = GetWorld()->GetGameInstance()->GetSubsystem<UMT2GuildSubsystem>();
		int32 GuildId = 0;
		const EMT2GuildResult Result = Guilds
			? Guilds->CreateGuild(GuildName, GetCharacterId(), State->GetCharacterName(), State->GetCharacterLevel(), GuildId)
			: EMT2GuildResult::Unavailable;
		if (Result == EMT2GuildResult::Success)
		{
			FMT2Guild LocalGuild;
			Guilds->GetGuild(GuildId, LocalGuild);
			FMT2GuildSnapshot NewSnapshot;
			NewSnapshot.GuildId = LocalGuild.GuildId;
			NewSnapshot.Name = LocalGuild.GuildName;
			NewSnapshot.LeaderCharacterId = LocalGuild.MasterCharacterId;
			NewSnapshot.Level = LocalGuild.Level;
			NewSnapshot.Experience = LocalGuild.Experience;
			NewSnapshot.Members = LocalGuild.Members;
			for (int32 RankIndex = 1; RankIndex <= MT2Guild::RankCount; ++RankIndex)
			{
				FMT2GuildRank& Rank = NewSnapshot.Ranks.AddDefaulted_GetRef();
				Rank.Rank = RankIndex;
				Rank.Name = RankIndex == 1 ? TEXT("Leader") : TEXT("Member");
				Rank.Permissions = RankIndex == 1 ? static_cast<int32>(EMT2GuildPermission::InviteMembers) |
					static_cast<int32>(EMT2GuildPermission::RemoveMembers) |
					static_cast<int32>(EMT2GuildPermission::WriteNotice) |
					static_cast<int32>(EMT2GuildPermission::UseSkills) : 0;
			}
			ApplySnapshot(MoveTemp(NewSnapshot));
		}
		ApplyResult(TEXT("create"), Result);
		return;
	}

	ApplyResult(TEXT("create"), EMT2GuildResult::Unavailable);
}

void UMT2GuildComponent::ServerInvitePlayer_Implementation(AActor* TargetPlayer)
{
	const AMT2PlayerState* TargetState = MT2ActorUtils::ResolvePlayerState(TargetPlayer, false);
	const UMT2PersistenceComponent* Persistence = TargetState ? TargetState->GetPersistenceComponent() : nullptr;
	const FString TargetId = Persistence && !Persistence->GetEntityId().IsEmpty()
		? Persistence->GetEntityId() : (TargetState ? TargetState->GetCharacterName() : FString());
	if (TargetId.IsEmpty()) { ApplyResult(TEXT("invite"), EMT2GuildResult::UnknownCharacter); return; }
	if (UMT2ServerRuntimeSubsystem* Runtime = GetRuntime(); Runtime && Runtime->IsCoordinatorConnected())
		Runtime->PublishGuildInvite(GetCharacterId(), TargetId);
	else ApplyResult(TEXT("invite"), EMT2GuildResult::Unavailable);
}

void UMT2GuildComponent::ServerAnswerInvite_Implementation(const FString& InviterId, bool bAccept)
{
	if (UMT2ServerRuntimeSubsystem* Runtime = GetRuntime(); Runtime && Runtime->IsCoordinatorConnected())
		Runtime->PublishGuildAnswer(GetCharacterId(), InviterId, bAccept);
	else ApplyResult(TEXT("answer"), EMT2GuildResult::Unavailable);
}

void UMT2GuildComponent::ServerGuildAction_Implementation(
	FName Action, const FString& TargetId, int32 Rank, const FString& Text, int32 Flags)
{
	if (UMT2ServerRuntimeSubsystem* Runtime = GetRuntime(); Runtime && Runtime->IsCoordinatorConnected())
	{
		if (Action == TEXT("chat"))
		{
			const AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner());
			Runtime->PublishGuildChat(GetCharacterId(), State ? State->GetCharacterName() : FString(), Text);
		}
		else Runtime->PublishGuildAction(GetCharacterId(), Action, TargetId, Rank, Text, Flags);
	}
	else ApplyResult(Action, EMT2GuildResult::Unavailable);
}

void UMT2GuildComponent::ApplySnapshot(FMT2GuildSnapshot NewSnapshot)
{
	Snapshot = MoveTemp(NewSnapshot);
	if (AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner()); State && State->HasAuthority())
	{
		State->SetGuild(Snapshot.GuildId, Snapshot.Name);
	}
	OnRep_Snapshot();
	GetOwner()->ForceNetUpdate();
}

void UMT2GuildComponent::ApplyInvite(
	const FString& InviterId, const FString& InviterName, const FString& GuildName)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		ClientReceiveInvite(InviterId, InviterName, GuildName);
		return;
	}
	PendingInviterId = InviterId;
	OnGuildInvite.Broadcast(InviterId, InviterName, GuildName);
}

void UMT2GuildComponent::ApplyResult(FName Action, EMT2GuildResult Result)
{
	if (Action == TEXT("create") && Result == EMT2GuildResult::Success)
	{
		if (AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner()); State && State->HasAuthority())
		{
			// SQLite already charged the authoritative persisted row. Mirror that transaction into the
			// live state so the HUD updates immediately; persistence later writes the same final value.
			State->SetYang(FMath::Max<int64>(0, State->GetYang() - MT2Guild::CreationCost));
		}
	}
	if (GetOwner() && GetOwner()->HasAuthority()) ClientReceiveResult(Action, Result);
	else OnGuildActionResult.Broadcast(Action, Result);
}

void UMT2GuildComponent::ApplyChat(const FString& SenderName, const FString& Message)
{
	if (GetOwner() && GetOwner()->HasAuthority()) ClientReceiveChat(SenderName, Message);
	else OnGuildChat.Broadcast(SenderName, Message);
}

void UMT2GuildComponent::ClientReceiveInvite_Implementation(
	const FString& InviterId, const FString& InviterName, const FString& GuildName)
{
	PendingInviterId = InviterId;
	OnGuildInvite.Broadcast(InviterId, InviterName, GuildName);
	const AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner());
	const APlayerController* Controller = State ? Cast<APlayerController>(State->GetOwner()) : nullptr;
	if (AMT2HUD* HUD = Controller ? Controller->GetHUD<AMT2HUD>() : nullptr)
		HUD->ShowGuildInvite(InviterName, GuildName);
}

void UMT2GuildComponent::ClientReceiveResult_Implementation(FName Action, EMT2GuildResult Result)
{
	OnGuildActionResult.Broadcast(Action, Result);
}

void UMT2GuildComponent::ClientReceiveChat_Implementation(
	const FString& SenderName, const FString& Message)
{
	OnGuildChat.Broadcast(SenderName, Message);
	const AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner());
	if (AMT2PlayerController* Controller = State ? Cast<AMT2PlayerController>(State->GetOwner()) : nullptr)
		Controller->AddInfoChatLine(FString::Printf(TEXT("[Guild] %s: %s"), *SenderName, *Message));
}

void UMT2GuildComponent::OnRep_Snapshot()
{
	OnGuildChanged.Broadcast();
}
