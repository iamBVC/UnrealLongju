/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Components/ActorComponent.h"
#include "Guild/MT2GuildTypes.h"
#include "MT2GuildComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMT2GuildSnapshotChangedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FMT2GuildInviteSignature, const FString&, InviterId, const FString&, InviterName, const FString&, GuildName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FMT2GuildResultSignature, FName, Action, EMT2GuildResult, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FMT2GuildChatSignature, const FString&, SenderName, const FString&, Message);

UCLASS(ClassGroup = "MT2", meta = (BlueprintSpawnableComponent))
class METIN2_API UMT2GuildComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMT2GuildComponent();
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Guild") const FMT2GuildSnapshot& GetSnapshot() const { return Snapshot; }
	UFUNCTION(BlueprintPure, Category = "Guild") bool IsInGuild() const { return Snapshot.IsValid(); }
	UFUNCTION(BlueprintPure, Category = "Guild") bool IsLeader() const;
	UFUNCTION(BlueprintPure, Category = "Guild") bool HasPermission(EMT2GuildPermission Permission) const;

	UFUNCTION(BlueprintCallable, Category = "Guild") void CreateGuild(const FString& GuildName);
	UFUNCTION(BlueprintCallable, Category = "Guild") void InvitePlayer(AActor* TargetPlayer);
	UFUNCTION(BlueprintCallable, Category = "Guild") void AnswerInvite(bool bAccept);
	UFUNCTION(BlueprintCallable, Category = "Guild") void LeaveGuild();
	UFUNCTION(BlueprintCallable, Category = "Guild") void DisbandGuild();
	UFUNCTION(BlueprintCallable, Category = "Guild") void RemoveMember(const FString& CharacterId);
	UFUNCTION(BlueprintCallable, Category = "Guild") void ChangeMemberRank(const FString& CharacterId, int32 Rank);
	UFUNCTION(BlueprintCallable, Category = "Guild") void ChangeRank(int32 Rank, const FString& Name, int32 Permissions);
	UFUNCTION(BlueprintCallable, Category = "Guild") void SendGuildChat(const FString& Message);
	UFUNCTION(BlueprintCallable, Category = "Guild|Mark") void UploadGuildMarkFromFile(const FString& FilePath);
	void RequestGuildMark(int32 GuildId, int64 KnownRevision);
	void ApplyGuildMark(int32 GuildId, int64 Revision, const TArray<uint8>& PngData);

	UPROPERTY(BlueprintAssignable, Category = "Guild") FMT2GuildSnapshotChangedSignature OnGuildChanged;
	UPROPERTY(BlueprintAssignable, Category = "Guild") FMT2GuildInviteSignature OnGuildInvite;
	UPROPERTY(BlueprintAssignable, Category = "Guild") FMT2GuildResultSignature OnGuildActionResult;
	UPROPERTY(BlueprintAssignable, Category = "Guild") FMT2GuildChatSignature OnGuildChat;

	void ApplySnapshot(FMT2GuildSnapshot NewSnapshot);
	void ApplyInvite(const FString& InviterId, const FString& InviterName, const FString& GuildName);
	void ApplyResult(FName Action, EMT2GuildResult Result);
	void ApplyChat(const FString& SenderName, const FString& Message);

private:
	UFUNCTION(Server, Reliable) void ServerCreateGuild(const FString& GuildName);
	UFUNCTION(Server, Reliable) void ServerInvitePlayer(AActor* TargetPlayer);
	UFUNCTION(Server, Reliable) void ServerAnswerInvite(const FString& InviterId, bool bAccept);
	UFUNCTION(Server, Reliable) void ServerGuildAction(FName Action, const FString& TargetId, int32 Rank, const FString& Text, int32 Flags);
	UFUNCTION(Server, Reliable) void ServerUploadGuildMark(const TArray<uint8>& PngData);
	UFUNCTION(Server, Reliable) void ServerRequestGuildMark(int32 GuildId, int64 KnownRevision);
	UFUNCTION(Client, Reliable) void ClientReceiveInvite(const FString& InviterId, const FString& InviterName, const FString& GuildName);
	UFUNCTION(Client, Reliable) void ClientReceiveResult(FName Action, EMT2GuildResult Result);
	UFUNCTION(Client, Reliable) void ClientReceiveChat(const FString& SenderName, const FString& Message);
	UFUNCTION(Client, Reliable) void ClientReceiveGuildMark(int32 GuildId, int64 Revision, const TArray<uint8>& PngData);
	UFUNCTION() void OnRep_Snapshot();
	void TryAnnounceLogin();
	FString GetCharacterId() const;
	class UMT2ServerRuntimeSubsystem* GetRuntime() const;

	UPROPERTY(ReplicatedUsing = OnRep_Snapshot) FMT2GuildSnapshot Snapshot;
	FString PendingInviterId;
	FTimerHandle LoginTimer;
};
