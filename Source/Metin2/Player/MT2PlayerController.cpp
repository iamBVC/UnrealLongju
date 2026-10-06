/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Player/MT2PlayerController.h"
#include "Config/MT2PathSettings.h"

#include "Net/NetworkProfiler.h"

#include "Authentication/MT2ClientSessionSubsystem.h"
#include "Audio/MT2AudioUserSettings.h"
#include "Characters/MT2PlayerCharacter.h"
#include "Config/MT2GameplaySettings.h"
#include "EnhancedInputSubsystems.h"
#include "Mounts/MT2MountComponent.h"
#include "Engine/NetConnection.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "Effects/MT2ExperienceOrbActor.h"
#include "Game/MT2GameModeBase.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "InputCoreTypes.h"
#include "Input/MT2InputConfig.h"
#include "Input/MT2KeyBindingSubsystem.h"
#include "Core/MT2VnumRegistrySubsystem.h"
#include "Items/MT2ItemTemplate.h"
#include "Mobs/MT2Mob.h"
#include "Guild/MT2GuildComponent.h"
#include "Party/MT2Party.h"
#include "Persistence/MT2PersistenceComponent.h"
#include "Player/MT2PlayerState.h"
#include "Trade/MT2TradeComponent.h"
#include "Server/MT2ServerRuntimeSubsystem.h"
#include "Skills/MT2SkillDefinition.h"
#include "Skills/MT2SkillSet.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#include "Slate/SObjectWidget.h"
#include "UI/MT2ChatWidget.h"
#include "Voice/MT2VoiceChatClientSubsystem.h"
#include "UI/MT2HUD.h"
#include "UI/MT2NameplateComponent.h"
#include "UI/MT2NotificationsWidget.h"
#include "World/MT2Portal.h"

DEFINE_LOG_CATEGORY_STATIC(LogMT2MapTravel, Log, All);

void AMT2PlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		if (UMT2ServerRuntimeSubsystem* Runtime = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UMT2ServerRuntimeSubsystem>() : nullptr)
		{
			Runtime->OnChatReceived.AddUObject(this, &AMT2PlayerController::HandleChatReceived);
			Runtime->OnAccountSessionRevoked.AddUObject(
				this, &AMT2PlayerController::HandleAccountSessionRevoked);
		}
	}
	if (IsLocalPlayerController())
	{
		if (UMT2ClientSessionSubsystem* Session = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UMT2ClientSessionSubsystem>() : nullptr)
		{
			if (Session->GetState() == EMT2ClientSessionState::ConnectingGateway)
			{
				Session->MarkGatewayConnected();
			}
		}
	}
}

void AMT2PlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		if (AMT2PlayerState* State = GetPlayerState<AMT2PlayerState>())
		{
			if (AMT2Party* Party = State->GetParty())
			{
				if (bMigrationPending) Party->DetachLocalMemberForTravel(State);
				else Party->RemoveMember(State);
			}
		}
		ClearMigrationSaveDelegate();
		if (UMT2ServerRuntimeSubsystem* Runtime = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UMT2ServerRuntimeSubsystem>() : nullptr)
		{
			Runtime->OnChatReceived.RemoveAll(this);
			Runtime->OnAccountSessionRevoked.RemoveAll(this);
			if (Runtime->IsMapServer())
			{
				if (AMT2PlayerState* State = GetPlayerState<AMT2PlayerState>())
				{
					if (UMT2PersistenceComponent* Persistence = State->GetPersistenceComponent())
					{
						Persistence->RequestSave(true);
						Runtime->NotifyCharacterLogout(
							Persistence->GetEntityId(), Persistence->GetOwnerId());
					}
				}
			}
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AMT2PlayerController::RequestPartyInvite(AMT2PlayerCharacter* TargetPlayer)
{
	if (!IsLocalPlayerController() || !TargetPlayer)
	{
		return;
	}
	ServerRequestPartyInvite(TargetPlayer->GetPlayerState<AMT2PlayerState>());
}

void AMT2PlayerController::RequestTrade(AMT2PlayerCharacter* TargetPlayer)
{
	if (TargetPlayer) ServerRequestTrade(TargetPlayer);
}

void AMT2PlayerController::ServerRequestTrade_Implementation(AMT2PlayerCharacter* TargetPlayer)
{
	AMT2PlayerState* State = GetPlayerState<AMT2PlayerState>();
	if (State && State->GetTradeComponent()) State->GetTradeComponent()->ServerRequestTrade(TargetPlayer);
}

void AMT2PlayerController::RespondToPartyInvite(int32 InviteId, bool bAccept)
{
	if (IsLocalPlayerController())
	{
		ServerRespondToPartyInvite(InviteId, bAccept);
	}
}

void AMT2PlayerController::RequestDisbandParty()
{
	if (IsLocalPlayerController())
	{
		ServerDisbandParty();
	}
}

void AMT2PlayerController::RequestLeaveParty()
{
	if (IsLocalPlayerController())
	{
		ServerLeaveParty();
	}
}

void AMT2PlayerController::RequestKickPartyMember(AMT2PlayerState* Member)
{
	if (IsLocalPlayerController() && Member)
	{
		ServerKickPartyMember(Member);
	}
}

void AMT2PlayerController::RequestKickPartyMemberById(const FString& CharacterId)
{
	if (IsLocalPlayerController() && !CharacterId.IsEmpty())
	{
		ServerKickPartyMemberById(CharacterId);
	}
}

void AMT2PlayerController::ClientReceivePartyInvite_Implementation(
	int32 InviteId, const FString& InviterName)
{
	if (AMT2HUD* HUD = Cast<AMT2HUD>(GetHUD()))
	{
		HUD->ShowPartyInvite(InviteId, InviterName);
	}
}

void AMT2PlayerController::ServerRequestPartyInvite_Implementation(
	AMT2PlayerState* TargetPlayerState)
{
	AMT2PlayerState* InviterState = GetPlayerState<AMT2PlayerState>();
	AMT2PlayerController* TargetController = TargetPlayerState
		? Cast<AMT2PlayerController>(TargetPlayerState->GetOwner()) : nullptr;
	if (!InviterState || !TargetPlayerState || TargetPlayerState == InviterState || !TargetController
		|| TargetPlayerState->GetWorld() != GetWorld())
	{
		ClientSystemChatMessage(TEXT("Party invitation failed: invalid player."));
		return;
	}
	if (TargetPlayerState->IsInParty())
	{
		ClientSystemChatMessage(TEXT("That player is already in a party."));
		return;
	}

	AMT2Party* ExistingParty = InviterState->GetParty();
	if (ExistingParty && !ExistingParty->IsLeader(InviterState))
	{
		ClientSystemChatMessage(TEXT("Only the party leader can invite players."));
		return;
	}
	const int32 MaximumMembers = FMath::Clamp(
		UMT2GameplaySettings::Get().PartyMaximumMembers, 2, 8);
	if (ExistingParty && ExistingParty->GetMemberCount() >= MaximumMembers)
	{
		ClientSystemChatMessage(TEXT("The party is full."));
		return;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	if (TargetController->PendingPartyInviter.IsValid()
		&& TargetController->PendingPartyInviteExpiry > Now)
	{
		ClientSystemChatMessage(TEXT("That player is considering another party invitation."));
		return;
	}

	TargetController->PendingPartyInviter = InviterState;
	TargetController->PendingPartyInviteId = TargetController->NextPartyInviteId++;
	TargetController->PendingPartyInviteExpiry = Now
		+ FMath::Max(UMT2GameplaySettings::Get().PartyInviteTimeout, 5.0f);
	TargetController->ClientReceivePartyInvite(
		TargetController->PendingPartyInviteId, InviterState->GetCharacterName());
	ClientSystemChatMessage(FString::Printf(
		TEXT("Party invitation sent to %s."), *TargetPlayerState->GetCharacterName()));
}

void AMT2PlayerController::ServerRespondToPartyInvite_Implementation(int32 InviteId, bool bAccept)
{
	AMT2PlayerState* TargetState = GetPlayerState<AMT2PlayerState>();
	AMT2PlayerState* InviterState = PendingPartyInviter.Get();
	const bool bValidInvite = InviteId > 0 && InviteId == PendingPartyInviteId
		&& InviterState && TargetState && InviterState != TargetState
		&& GetWorld()->GetTimeSeconds() <= PendingPartyInviteExpiry;

	PendingPartyInviter.Reset();
	PendingPartyInviteId = 0;
	PendingPartyInviteExpiry = 0.0;
	if (!bValidInvite)
	{
		ClientSystemChatMessage(TEXT("That party invitation is no longer valid."));
		return;
	}

	AMT2PlayerController* InviterController =
		Cast<AMT2PlayerController>(InviterState->GetOwner());
	if (!bAccept)
	{
		if (InviterController)
		{
			InviterController->ClientSystemChatMessage(FString::Printf(
				TEXT("%s declined your party invitation."), *TargetState->GetCharacterName()));
		}
		return;
	}
	if (TargetState->IsInParty())
	{
		ClientSystemChatMessage(TEXT("You are already in a party."));
		return;
	}

	bool bJoined = false;
	if (AMT2Party* ExistingParty = InviterState->GetParty())
	{
		if (ExistingParty->IsLeader(InviterState))
		{
			bJoined = ExistingParty->AddMember(TargetState);
		}
	}
	else
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = this;
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AMT2Party* NewParty = GetWorld()->SpawnActor<AMT2Party>(
			AMT2Party::StaticClass(), FTransform::Identity, SpawnParameters))
		{
			bJoined = NewParty->InitializeParty(InviterState, TargetState);
			if (!bJoined)
			{
				NewParty->Destroy();
			}
		}
	}

	if (!bJoined)
	{
		ClientSystemChatMessage(TEXT("Could not join the party."));
		return;
	}
	ClientSystemChatMessage(FString::Printf(
		TEXT("You joined %s's party."), *InviterState->GetCharacterName()));
	if (InviterController)
	{
		InviterController->ClientSystemChatMessage(FString::Printf(
			TEXT("%s joined your party."), *TargetState->GetCharacterName()));
	}
}

void AMT2PlayerController::ServerDisbandParty_Implementation()
{
	AMT2PlayerState* State = GetPlayerState<AMT2PlayerState>();
	AMT2Party* Party = State ? State->GetParty() : nullptr;
	if (!Party || !Party->IsLeader(State))
	{
		ClientSystemChatMessage(TEXT("Only the party leader can disband the party."));
		return;
	}
	Party->Disband();
	ClientSystemChatMessage(TEXT("The party was disbanded."));
}

void AMT2PlayerController::ServerLeaveParty_Implementation()
{
	AMT2PlayerState* State = GetPlayerState<AMT2PlayerState>();
	AMT2Party* Party = State ? State->GetParty() : nullptr;
	if (!Party)
	{
		ClientSystemChatMessage(TEXT("You are not in a party."));
		return;
	}
	if (Party->IsLeader(State))
	{
		ClientSystemChatMessage(TEXT("The party leader must disband the party."));
		return;
	}
	Party->RemoveMember(State);
	ClientSystemChatMessage(TEXT("You left the party."));
}

void AMT2PlayerController::ServerKickPartyMember_Implementation(AMT2PlayerState* Member)
{
	AMT2PlayerState* Leader = GetPlayerState<AMT2PlayerState>();
	AMT2Party* Party = Leader ? Leader->GetParty() : nullptr;
	if (!Party || !Party->IsLeader(Leader))
	{
		ClientSystemChatMessage(TEXT("Only the party leader can remove members."));
		return;
	}
	if (!Member || Member == Leader || !Party->ContainsMember(Member))
	{
		ClientSystemChatMessage(TEXT("That party member cannot be removed."));
		return;
	}

	const FString MemberName = Member->GetCharacterName();
	if (AMT2PlayerController* MemberController = Cast<AMT2PlayerController>(Member->GetOwner()))
	{
		MemberController->ClientSystemChatMessage(TEXT("You were removed from the party."));
	}
	Party->RemoveMember(Member);
	ClientSystemChatMessage(FString::Printf(
		TEXT("%s was removed from the party."), *MemberName));
}

void AMT2PlayerController::ServerKickPartyMemberById_Implementation(const FString& CharacterId)
{
	AMT2PlayerState* Leader = GetPlayerState<AMT2PlayerState>();
	AMT2Party* Party = Leader ? Leader->GetParty() : nullptr;
	if (!Party || !Party->IsLeader(Leader))
	{
		ClientSystemChatMessage(TEXT("Only the party leader can remove members."));
		return;
	}
	const UMT2PersistenceComponent* LeaderPersistence = Leader->GetPersistenceComponent();
	if (CharacterId.IsEmpty()
		|| (LeaderPersistence && CharacterId == LeaderPersistence->GetEntityId())
		|| !Party->ContainsCharacter(CharacterId))
	{
		ClientSystemChatMessage(TEXT("That party member cannot be removed."));
		return;
	}
	FString MemberName = TEXT("Party member");
	for (const FMT2PartyMemberData& Member : Party->GetMembers())
	{
		if (Member.CharacterId == CharacterId)
		{
			MemberName = Member.CharacterName;
			if (AMT2PlayerController* MemberController = Member.PlayerState
				? Cast<AMT2PlayerController>(Member.PlayerState->GetOwner()) : nullptr)
			{
				MemberController->ClientSystemChatMessage(TEXT("You were removed from the party."));
			}
			break;
		}
	}
	Party->RemoveMemberByCharacterId(CharacterId);
	ClientSystemChatMessage(FString::Printf(TEXT("%s was removed from the party."), *MemberName));
}

bool AMT2PlayerController::ConnectToServer(const FString& Address)
{
	FString CleanAddress = Address;
	CleanAddress.TrimStartAndEndInline();

	if (!IsLocalPlayerController() || CleanAddress.IsEmpty())
	{
		return false;
	}
	if (UMT2ClientSessionSubsystem* Session = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMT2ClientSessionSubsystem>() : nullptr)
	{
		Session->MarkConnectingGateway();
	}

	ClientTravel(CleanAddress, TRAVEL_Absolute);
	return true;
}

void AMT2PlayerController::RegisterAccount(const FString& Username, const FString& Password)
{
	if (IsLocalPlayerController()) ServerRegisterAccount(Username, Password);
}

void AMT2PlayerController::Login(const FString& Username, const FString& Password)
{
	if (IsLocalPlayerController())
	{
		if (UMT2ClientSessionSubsystem* Session = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UMT2ClientSessionSubsystem>() : nullptr)
		{
			Session->MarkAuthenticating();
		}
		ServerLogin(Username, Password);
	}
}

void AMT2PlayerController::CreateCharacter(
	const FString& CharacterName, const FMT2CharacterAppearance& Appearance, EMT2Empire Empire)
{
	if (IsLocalPlayerController()) ServerCreateCharacter(CharacterName, Appearance, Empire);
}

void AMT2PlayerController::SelectCharacter(const FString& CharacterId, int32 PreferredChannel)
{
	if (IsLocalPlayerController()) ServerSelectCharacter(CharacterId, PreferredChannel);
}

void AMT2PlayerController::RequestMapTransfer(const FString& DestinationMapId, int32 PreferredChannel)
{
	if (IsLocalPlayerController()) ServerRequestMapTransfer(DestinationMapId, PreferredChannel);
}

void AMT2PlayerController::ServerRegisterAccount_Implementation(
	const FString& Username, const FString& Password)
{
	UMT2ServerRuntimeSubsystem* Runtime = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMT2ServerRuntimeSubsystem>() : nullptr;
	if (!Runtime || !Runtime->IsGateway() || bGatewayRequestPending)
	{
		ClientRegistrationResult(false, TEXT("Gateway is unavailable or busy."));
		return;
	}
	bGatewayRequestPending = true;
	TWeakObjectPtr<AMT2PlayerController> WeakThis(this);
	Runtime->RequestAccountRegistration(Username, Password,
		[WeakThis](FMT2AccountRegistrationResult&& Result)
		{
			if (!WeakThis.IsValid()) return;
			WeakThis->bGatewayRequestPending = false;
			WeakThis->ClientRegistrationResult(Result.bSucceeded, Result.Error);
		});
}

void AMT2PlayerController::ServerLogin_Implementation(
	const FString& Username, const FString& Password)
{
	UMT2ServerRuntimeSubsystem* Runtime = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMT2ServerRuntimeSubsystem>() : nullptr;
	if (!Runtime || !Runtime->IsGateway() || bGatewayRequestPending)
	{
		ClientLoginResult(false, TEXT("Gateway is unavailable or busy."), {});
		return;
	}
	// Brute-force protection: the client UI can't be trusted, so the gateway enforces one login
	// attempt per 5 seconds. Per-connection first (cheap), then per-IP/per-account in the runtime
	// so parallel connections or reconnect loops don't bypass it.
	const double Now = FPlatformTime::Seconds();
	FString RemoteIp;
	if (UNetConnection* Connection = GetNetConnection())
	{
		RemoteIp = Connection->LowLevelGetRemoteAddress(false);
	}
	if (Now - LastServerLoginAttemptTime < UMT2ServerRuntimeSubsystem::LoginAttemptCooldownSeconds ||
		Runtime->ShouldThrottleLoginAttempt(RemoteIp, Username))
	{
		ClientLoginResult(false, TEXT("Too many login attempts. Wait a few seconds and try again."), {});
		return;
	}
	LastServerLoginAttemptTime = Now;
	bGatewayRequestPending = true;
	TWeakObjectPtr<AMT2PlayerController> WeakThis(this);
	Runtime->RequestAccountLogin(Username, Password, SessionToken,
		[WeakThis](FMT2AccountLoginResult&& Result)
		{
			if (!WeakThis.IsValid()) return;
			WeakThis->bGatewayRequestPending = false;
			if (Result.bSucceeded)
			{
				WeakThis->AuthenticatedAccountId = Result.AccountId;
				WeakThis->SessionToken = Result.SessionToken;
				WeakThis->CachedCharacters = Result.Characters;
			}
			WeakThis->ClientLoginResult(Result.bSucceeded, Result.Error, Result.Characters);
		});
}

void AMT2PlayerController::ServerCreateCharacter_Implementation(
	const FString& CharacterName, const FMT2CharacterAppearance& Appearance, EMT2Empire Empire)
{
	UMT2ServerRuntimeSubsystem* Runtime = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMT2ServerRuntimeSubsystem>() : nullptr;
	if (!Runtime || !Runtime->IsGateway() || SessionToken.IsEmpty() || bGatewayRequestPending)
	{
		ClientCharacterCreated(false, TEXT("A valid login session is required."), {});
		return;
	}
	bGatewayRequestPending = true;
	TWeakObjectPtr<AMT2PlayerController> WeakThis(this);
	Runtime->RequestCharacterCreation(SessionToken, CharacterName, Appearance, Empire,
		[WeakThis](FMT2CharacterCreateResult&& Result)
		{
			if (!WeakThis.IsValid()) return;
			WeakThis->bGatewayRequestPending = false;
			if (Result.bSucceeded) WeakThis->CachedCharacters.Add(Result.Character);
			WeakThis->ClientCharacterCreated(Result.bSucceeded, Result.Error, Result.Character);
		});
}

void AMT2PlayerController::ServerSelectCharacter_Implementation(
	const FString& CharacterId, int32 PreferredChannel)
{
	UMT2ServerRuntimeSubsystem* Runtime = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMT2ServerRuntimeSubsystem>() : nullptr;
	if (!Runtime || !Runtime->IsGateway() || SessionToken.IsEmpty() || bGatewayRequestPending)
	{
		ClientTravelFailed(TEXT("A valid login session is required."));
		return;
	}
	bGatewayRequestPending = true;
	TWeakObjectPtr<AMT2PlayerController> WeakThis(this);
	Runtime->RequestCharacterAdmission(SessionToken, CharacterId, PreferredChannel,
		[WeakThis](FMT2CharacterAdmissionResult&& Result)
		{
			if (!WeakThis.IsValid()) return;
			WeakThis->bGatewayRequestPending = false;
			if (!Result.bSucceeded)
			{
				WeakThis->ClientTravelFailed(Result.Error);
				return;
			}
			const FString Endpoint = FString::Printf(
				TEXT("%s:%d"), *Result.Server.PublicIp, Result.Server.GamePort);
			WeakThis->ClientBeginMapTravel(Endpoint, Result.Ticket);
		});
}

void AMT2PlayerController::ServerRequestMapTransfer_Implementation(
	const FString& DestinationMapId, int32 PreferredChannel)
{
	BeginMapTransfer(DestinationMapId, PreferredChannel);
}

bool AMT2PlayerController::RequestMapTransferFromServer(
	const FString& DestinationMapId, int32 PreferredChannel, bool bForceTownSpawn,
	bool bClearPendingSpawnOnFailure)
{
	return HasAuthority() && BeginMapTransfer(
		DestinationMapId, PreferredChannel, bForceTownSpawn, bClearPendingSpawnOnFailure);
}

bool AMT2PlayerController::BeginMapTransfer(
	const FString& DestinationMapId, int32 PreferredChannel, bool bForceTownSpawn,
	bool bClearPendingSpawnOnFailure)
{
	const FString RequestedMapId = DestinationMapId.TrimStartAndEnd();
	UMT2ServerRuntimeSubsystem* Runtime = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMT2ServerRuntimeSubsystem>() : nullptr;
	AMT2PlayerState* MT2PlayerState = GetPlayerState<AMT2PlayerState>();
	UMT2PersistenceComponent* Persistence = MT2PlayerState ? MT2PlayerState->GetPersistenceComponent() : nullptr;
	if (!Runtime || !Runtime->IsMapServer())
	{
		ClientTravelFailed(TEXT("Map transfer requires an active map-server runtime."));
		return false;
	}
	if (RequestedMapId.IsEmpty())
	{
		ClientTravelFailed(TEXT("A destination map is required."));
		return false;
	}
	if (bMigrationPending)
	{
		ClientTravelFailed(TEXT("A map transfer is already in progress."));
		return false;
	}

	const FMT2ServerRuntimeConfig& RuntimeConfig = Runtime->GetConfig();
	const bool bSameMap = RequestedMapId.Equals(RuntimeConfig.MapId, ESearchCase::IgnoreCase);
	const bool bSameChannel = PreferredChannel <= 0 || PreferredChannel == RuntimeConfig.Channel;
	if (bSameMap && bSameChannel)
	{
		AMT2GameModeBase* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AMT2GameModeBase>() : nullptr;
		if (!GameMode || !GameMode->RepairPlayerSpawn(this, bForceTownSpawn))
		{
			ClientTravelFailed(TEXT("The local map destination could not be reached."));
			return false;
		}
		if (Persistence)
		{
			Persistence->RequestSave(true);
		}
		return true;
	}
	if (!Persistence || Persistence->GetEntityId().IsEmpty() || Persistence->GetOwnerId().IsEmpty())
	{
		ClientTravelFailed(TEXT("Character transfer identity is unavailable."));
		return false;
	}
	if (Persistence->GetPersistenceState() != EMT2PersistenceState::Ready)
	{
		ClientTravelFailed(TEXT("Character persistence is busy. Try the transfer again."));
		return false;
	}
	bMigrationPending = true;
	PendingMigrationMapId = RequestedMapId;
	PendingMigrationChannel = FMath::Max(PreferredChannel, 0);
	bPendingMigrationForceTownSpawn = bForceTownSpawn;
	bClearPendingSpawnOnMigrationFailure = bClearPendingSpawnOnFailure;
	if (AMT2PlayerCharacter* PlayerCharacter = Cast<AMT2PlayerCharacter>(GetPawn()))
	{
		if (UCharacterMovementComponent* Movement = PlayerCharacter->GetCharacterMovement()) Movement->DisableMovement();
	}
	const FString CharacterId = Persistence->GetEntityId();
	const FString AccountId = Persistence->GetOwnerId();
	TWeakObjectPtr<AMT2PlayerController> WeakThis(this);
	TWeakObjectPtr<UMT2ServerRuntimeSubsystem> WeakRuntime(Runtime);
	MigrationSaveHandle = Persistence->OnSaveFinishedNative.AddWeakLambda(this,
		[WeakThis, WeakRuntime, CharacterId, AccountId]
		(bool bSucceeded, int64 Revision, const FString& Error)
		{
			AMT2PlayerController* Controller = WeakThis.Get();
			if (!Controller || !Controller->bMigrationPending) return;
			// Removing this delegate destroys the lambda capture storage. Preserve every captured
			// value needed below before unbinding from inside the callback.
			UMT2ServerRuntimeSubsystem* ActiveRuntime = WeakRuntime.Get();
			const FString TransferCharacterId = CharacterId;
			const FString TransferAccountId = AccountId;
			Controller->ClearMigrationSaveDelegate();
			if (!bSucceeded)
			{
				Controller->AbortMapTransfer(
					Error.IsEmpty() ? TEXT("Character save failed.") : Error);
				return;
			}
			if (!ActiveRuntime || !ActiveRuntime->IsMapServer())
			{
				Controller->AbortMapTransfer(TEXT("Map transfer service became unavailable."));
				return;
			}
			ActiveRuntime->RequestTransferTicket(
				TransferCharacterId, TransferAccountId,
				Controller->PendingMigrationMapId, Controller->PendingMigrationChannel,
				Controller->bPendingMigrationForceTownSpawn,
				[WeakThis](FMT2TransferTicketResult&& Result)
				{
					AMT2PlayerController* ActiveController = WeakThis.Get();
					if (!ActiveController || !ActiveController->bMigrationPending) return;
					if (!Result.bSucceeded)
					{
						ActiveController->AbortMapTransfer(Result.Error);
						return;
					}
					ActiveController->PendingMigrationMapId.Reset();
					ActiveController->PendingMigrationChannel = 0;
					ActiveController->bPendingMigrationForceTownSpawn = false;
					ActiveController->bClearPendingSpawnOnMigrationFailure = false;
					const FString Endpoint = FString::Printf(
						TEXT("%s:%d"), *Result.Server.PublicIp, Result.Server.GamePort);
					UE_LOG(LogMT2MapTravel, Display,
						TEXT("[MapTravel] Dispatching client travel to endpoint='%s' map='%s' channel=%d."),
						*Endpoint, *Result.Server.MapId, Result.Server.Channel);
					ActiveController->ClientBeginMapTravel(Endpoint, Result.Ticket);
				});
		});
	Persistence->RequestSave(true);
	return true;
}

void AMT2PlayerController::ClearMigrationSaveDelegate()
{
	const FDelegateHandle SaveHandle = MigrationSaveHandle;
	MigrationSaveHandle.Reset();
	if (!SaveHandle.IsValid()) return;

	if (AMT2PlayerState* State = GetPlayerState<AMT2PlayerState>())
	{
		if (UMT2PersistenceComponent* Persistence = State->GetPersistenceComponent())
		{
			Persistence->OnSaveFinishedNative.Remove(SaveHandle);
		}
	}
}

void AMT2PlayerController::AbortMapTransfer(const FString& Error)
{
	ClearMigrationSaveDelegate();
	bMigrationPending = false;
	PendingMigrationMapId.Reset();
	PendingMigrationChannel = 0;
	bPendingMigrationForceTownSpawn = false;
	const bool bClearSpawnOverride = bClearPendingSpawnOnMigrationFailure;
	bClearPendingSpawnOnMigrationFailure = false;

	if (AMT2PlayerState* State = GetPlayerState<AMT2PlayerState>())
	{
		if (bClearSpawnOverride)
		{
			State->ClearPendingSpawnLocation();
			// The first migration save may already contain the override. Rewrite without it so a later
			// login cannot unexpectedly consume a destination from a failed portal transfer.
			if (UMT2PersistenceComponent* Persistence = State->GetPersistenceComponent())
			{
				Persistence->RequestSave(true);
			}
		}
	}
	if (AMT2PlayerCharacter* PlayerCharacter = Cast<AMT2PlayerCharacter>(GetPawn()))
	{
		if (UCharacterMovementComponent* Movement = PlayerCharacter->GetCharacterMovement())
		{
			Movement->SetMovementMode(MOVE_Walking);
		}
	}
	ClientTravelFailed(Error.IsEmpty() ? TEXT("Map transfer failed.") : Error);
}

void AMT2PlayerController::ClientRegistrationResult_Implementation(bool bSucceeded, const FString& Error)
{
	OnRegistrationCompleted.Broadcast(bSucceeded, Error);
}

void AMT2PlayerController::ClientLoginResult_Implementation(
	bool bSucceeded, const FString& Error, const TArray<FMT2CharacterSummary>& Characters)
{
	CachedCharacters = Characters;
	if (UMT2ClientSessionSubsystem* Session = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMT2ClientSessionSubsystem>() : nullptr)
	{
		Session->MarkLoginResult(bSucceeded, Error, CachedCharacters);
	}
	OnLoginCompleted.Broadcast(bSucceeded, Error);
	if (bSucceeded && !Error.IsEmpty())
	{
		ClientMessage(Error);
	}
	if (bSucceeded) OnCharacterListUpdated.Broadcast(CachedCharacters);
}

void AMT2PlayerController::ClientCharacterCreated_Implementation(
	bool bSucceeded, const FString& Error, const FMT2CharacterSummary& CharacterSummary)
{
	if (bSucceeded) CachedCharacters.Add(CharacterSummary);
	if (bSucceeded)
	{
		if (UMT2ClientSessionSubsystem* Session = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UMT2ClientSessionSubsystem>() : nullptr)
		{
			Session->SetCharacters(CachedCharacters);
		}
	}
	OnCharacterCreationCompleted.Broadcast(bSucceeded, Error);
	if (bSucceeded) OnCharacterListUpdated.Broadcast(CachedCharacters);
}

void AMT2PlayerController::ClientBeginMapTravel_Implementation(const FString& Endpoint, const FString& Ticket)
{
	if (Endpoint.IsEmpty() || Ticket.IsEmpty())
	{
		UE_LOG(LogMT2MapTravel, Error,
			TEXT("[MapTravel] Client rejected invalid transfer data: endpoint_empty=%s ticket_empty=%s."),
			Endpoint.IsEmpty() ? TEXT("true") : TEXT("false"),
			Ticket.IsEmpty() ? TEXT("true") : TEXT("false"));
		OnTravelFailed.Broadcast(false, TEXT("Map endpoint or ticket is invalid."));
		return;
	}
	if (UMT2ClientSessionSubsystem* Session = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMT2ClientSessionSubsystem>() : nullptr)
	{
		Session->MarkTravelingToMap();
	}
	UE_LOG(LogMT2MapTravel, Display,
		TEXT("[MapTravel] Client beginning absolute travel to endpoint='%s' ticket_length=%d."),
		*Endpoint, Ticket.Len());
	ClientTravel(FString::Printf(TEXT("%s?ticket=%s"), *Endpoint, *Ticket), TRAVEL_Absolute);
}

void AMT2PlayerController::ClientTravelFailed_Implementation(const FString& Error)
{
	if (!Error.IsEmpty())
	{
		AddInfoChatLine(Error);
		ClientMessage(Error);
	}
	if (UMT2ClientSessionSubsystem* Session = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMT2ClientSessionSubsystem>() : nullptr)
	{
		Session->MarkFailure(Error);
	}
	OnTravelFailed.Broadcast(false, Error);
}

void AMT2PlayerController::ClientWorldReady_Implementation()
{
	if (UMT2ClientSessionSubsystem* Session = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMT2ClientSessionSubsystem>() : nullptr)
	{
		Session->MarkInWorld();
	}
}

void AMT2PlayerController::ClientSessionRevoked_Implementation(const FString& Reason)
{
	const FString Message = Reason.IsEmpty()
		? TEXT("Disconnected by another login instance.") : Reason;
	if (UMT2ClientSessionSubsystem* Session = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMT2ClientSessionSubsystem>() : nullptr)
	{
		Session->MarkFailure(Message);
	}
	ClientMessage(Message);
	OnLoginCompleted.Broadcast(false, Message);
	OnTravelFailed.Broadcast(false, Message);
}

void AMT2PlayerController::ClientServerMaintenanceMessage_Implementation(const FString& Message)
{
	AddInfoChatLine(Message);
	ClientMessage(Message);
	if (UMT2ClientSessionSubsystem* Session = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMT2ClientSessionSubsystem>() : nullptr)
	{
		Session->MarkFailure(Message);
	}
}

void AMT2PlayerController::ClientSystemChatMessage_Implementation(const FString& Message)
{
	TArray<FString> Lines;
	Message.ParseIntoArrayLines(Lines, false);
	if (Lines.IsEmpty()) Lines.Add(Message);
	for (const FString& Line : Lines) AddInfoChatLine(Line);
}

void AMT2PlayerController::ClientNotifyYangReceived_Implementation(int64 Amount)
{
	if (Amount <= 0) return;
	AddRewardChatLine(FText::Format(
		NSLOCTEXT("MT2Rewards", "YangReceived", "You received {0} Yang."),
		FText::AsNumber(Amount)).ToString());
}

void AMT2PlayerController::ClientNotifyExperienceReceived_Implementation(int64 Amount)
{
	if (Amount <= 0) return;
	AddRewardChatLine(FText::Format(
		NSLOCTEXT("MT2Rewards", "ExperienceReceived", "You received {0} experience."),
		FText::AsNumber(Amount)).ToString());
}

void AMT2PlayerController::ClientSpawnExperienceOrbs_Implementation(
	FVector SourceLocation, int64 ExperienceAmount)
{
	if (APawn* Recipient = GetPawn())
	{
		AMT2ExperienceOrbActor::SpawnOrbs(
			GetWorld(), SourceLocation, Recipient, ExperienceAmount);
	}
}

void AMT2PlayerController::ClientNotifyItemReceived_Implementation(
	int32 Vnum, int32 Count, int32 SkillVnum)
{
	if (Vnum <= 0 || Count <= 0) return;
	UMT2VnumRegistrySubsystem* Registry = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMT2VnumRegistrySubsystem>() : nullptr;
	const TSubclassOf<UMT2ItemTemplate> TemplateClass = Registry
		? Registry->ResolveItemTemplateClass(Vnum) : nullptr;
	const UMT2ItemTemplate* Template = TemplateClass.GetDefaultObject();
	FText ItemName = Template && !Template->DisplayName.IsEmpty()
		? Template->DisplayName : FText::FromString(Template ? Template->InternalName : FString::FromInt(Vnum));
	if (Template && Cast<UMT2ItemSkillBookTemplate>(Template) && SkillVnum > 0)
	{
		if (const UMT2SkillDefinition* Skill = UMT2SkillSet::FindSkillAcrossSets(SkillVnum))
		{
			ItemName = FText::Format(
				NSLOCTEXT("MT2Rewards", "SkillBookName", "{0} Skill Book"),
				Skill->GetGradeDisplayName(0));
		}
	}
	const FText Message = Count > 1
		? FText::Format(NSLOCTEXT("MT2Rewards", "ItemsReceived", "You received {0} x{1}."),
			ItemName, FText::AsNumber(Count))
		: FText::Format(NSLOCTEXT("MT2Rewards", "ItemReceived", "You received {0}."), ItemName);
	AddRewardChatLine(Message.ToString());
}

void AMT2PlayerController::ClientSetNetworkProfilerEnabled_Implementation(bool bEnabled)
{
#if USE_NETWORK_PROFILER
	GNetworkProfiler.Exec(GetWorld(), bEnabled ? TEXT("ENABLE") : TEXT("DISABLE"), *GLog);
	ClientSystemChatMessage_Implementation(FString::Printf(
		TEXT("Client network profiler %s."), bEnabled ? TEXT("enabled") : TEXT("disabled")));
#else
	ClientSystemChatMessage_Implementation(TEXT("Network profiler is unavailable in this build."));
#endif
}

void AMT2PlayerController::SendFullMapNpcSnapshot()
{
	if (!HasAuthority() || !GetWorld())
	{
		return;
	}

	TArray<FMT2FullMapNpcMarker> Markers;
	for (TActorIterator<AMT2Mob> It(GetWorld()); It; ++It)
	{
		const AMT2Mob* Mob = *It;
		const EMT2MobType Type = Mob->GetMobType();
		if (Type != EMT2MobType::NPC && Type != EMT2MobType::Warp && Type != EMT2MobType::Goto)
		{
			continue;
		}
		FMT2FullMapNpcMarker& Marker = Markers.AddDefaulted_GetRef();
		Marker.Location = Mob->GetActorLocation();
		Marker.DisplayName = Mob->GetMobDisplayName();
		Marker.Vnum = Mob->GetMobVnum();
	}
	for (TActorIterator<AMT2Portal> It(GetWorld()); It; ++It)
	{
		const AMT2Portal* Portal = *It;
		FMT2FullMapNpcMarker& Marker = Markers.AddDefaulted_GetRef();
		Marker.Location = Portal->GetActorLocation();
		Marker.DisplayName = Portal->GetPortalDisplayName();
	}
	ClientSetFullMapNpcMarkers(Markers);
}

void AMT2PlayerController::ClientSetFullMapNpcMarkers_Implementation(
	const TArray<FMT2FullMapNpcMarker>& Markers)
{
	FullMapNpcMarkers = Markers;
	OnFullMapNpcMarkersChanged.Broadcast();
}

void AMT2PlayerController::SendSystemChatMessage(const FString& Message)
{
	if (HasAuthority()) ClientSystemChatMessage(Message);
}

void AMT2PlayerController::PrepareForServerMaintenance(const FString& Message)
{
	if (HasAuthority()) ClientServerMaintenanceMessage(Message);
}

void AMT2PlayerController::DisconnectForServerMaintenance(const FString& Message)
{
	if (!HasAuthority()) return;
	ClientServerMaintenanceMessage(Message);
	ClientReturnToMainMenuWithTextReason(FText::FromString(Message));
}

void AMT2PlayerController::HandleAccountSessionRevoked(
	const FString& SessionOrAccountId, const FString& Reason)
{
	if (!HasAuthority() || SessionOrAccountId.IsEmpty()) return;
	UMT2ServerRuntimeSubsystem* Runtime = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMT2ServerRuntimeSubsystem>() : nullptr;
	bool bMatches = Runtime && Runtime->IsGateway() && SessionToken == SessionOrAccountId;
	if (Runtime && Runtime->IsMapServer())
	{
		if (const AMT2PlayerState* State = GetPlayerState<AMT2PlayerState>())
		{
			if (const UMT2PersistenceComponent* Persistence = State->GetPersistenceComponent())
			{
				bMatches = Persistence->GetOwnerId() == SessionOrAccountId;
			}
		}
	}
	if (!bMatches) return;
	AuthenticatedAccountId.Reset();
	SessionToken.Reset();
	ClientSessionRevoked(Reason);
	ClientReturnToMainMenuWithTextReason(FText::FromString(Reason));
}

UMT2InputConfig* AMT2PlayerController::GetInputConfig()
{
	if (InputConfig)
	{
		return InputConfig;
	}

	if (!NativeInputConfig)
	{
		NativeInputConfig = UMT2InputConfig::CreateNativeDefaults(this);
	}

	return NativeInputConfig;
}

void AMT2PlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (!IsLocalPlayerController())
	{
		return;
	}

	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
	DefaultMouseCursor = EMouseCursor::Default;

	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);

	const UMT2InputConfig* ActiveInputConfig = GetInputConfig();
	UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());

	if (InputSubsystem && ActiveInputConfig && ActiveInputConfig->GetDefaultMappingContext())
	{
		InputSubsystem->AddMappingContext(ActiveInputConfig->GetDefaultMappingContext(), 0);
	}

	ApplyKeyBindings();
	if (UMT2KeyBindingSubsystem* Bindings = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMT2KeyBindingSubsystem>() : nullptr)
	{
		Bindings->OnBindingsChanged.AddWeakLambda(this, [this] { ApplyKeyBindings(); });
	}
}

void AMT2PlayerController::ApplyKeyBindings()
{
	UMT2KeyBindingSubsystem* Bindings = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMT2KeyBindingSubsystem>() : nullptr;
	if (!InputComponent || !Bindings)
	{
		return;
	}
	// Every gameplay hotkey below goes through the rebinding subsystem, so a rebind can simply
	// drop and rebuild the whole set.
	InputComponent->KeyBindings.Reset();

	auto Bind = [&](FName ActionId, EInputEvent Event, void (AMT2PlayerController::*Handler)())
	{
		const FKey Key = Bindings->GetKey(ActionId);
		if (Key.IsValid())
		{
			InputComponent->BindKey(Key, Event, this, Handler);
		}
	};
	Bind(UMT2KeyBindingSubsystem::ToggleCharacter, IE_Pressed, &AMT2PlayerController::HandleToggleCharacter);
	// V is proximity-voice push-to-talk by default; the skill window moved to K to free it.
	Bind(UMT2KeyBindingSubsystem::ToggleSkills, IE_Pressed, &AMT2PlayerController::HandleToggleSkill);
	Bind(UMT2KeyBindingSubsystem::VoiceTalk, IE_Pressed, &AMT2PlayerController::HandleVoiceTalkPressed);
	Bind(UMT2KeyBindingSubsystem::VoiceTalk, IE_Released, &AMT2PlayerController::HandleVoiceTalkReleased);
	Bind(UMT2KeyBindingSubsystem::ToggleBelt, IE_Pressed, &AMT2PlayerController::HandleToggleBelt);
	Bind(UMT2KeyBindingSubsystem::ToggleMessenger, IE_Pressed, &AMT2PlayerController::HandleToggleMessenger);
	Bind(UMT2KeyBindingSubsystem::ToggleMap, IE_Pressed, &AMT2PlayerController::HandleToggleMinimap);
	Bind(UMT2KeyBindingSubsystem::ToggleInventory, IE_Pressed, &AMT2PlayerController::HandleToggleInventory);
	Bind(UMT2KeyBindingSubsystem::PickupItems, IE_Pressed, &AMT2PlayerController::HandlePickupItems);
	Bind(UMT2KeyBindingSubsystem::ToggleFirstPerson, IE_Pressed, &AMT2PlayerController::HandleToggleFirstPerson);
	Bind(UMT2KeyBindingSubsystem::ToggleChat, IE_Pressed, &AMT2PlayerController::HandleToggleChat);
	// Old client: ESC opens/closes the system dialog (note: in PIE the editor consumes ESC to end
	// the session; test with -game / standalone).
	Bind(UMT2KeyBindingSubsystem::SystemMenu, IE_Pressed, &AMT2PlayerController::HandleToggleSystemMenu);

	FInputKeyBinding NotificationsBinding(FInputChord(EKeys::Q, false, true, false, false), IE_Pressed);
	NotificationsBinding.KeyDelegate.GetDelegateForManualSet().BindUObject(
		this, &AMT2PlayerController::HandleToggleNotifications);
	InputComponent->KeyBindings.Add(MoveTemp(NotificationsBinding));

	FInputKeyBinding SpecialMountBinding(FInputChord(EKeys::G, false, true, false, false), IE_Pressed);
	SpecialMountBinding.KeyDelegate.GetDelegateForManualSet().BindUObject(
		this, &AMT2PlayerController::HandleToggleSpecialMount);
	InputComponent->KeyBindings.Add(MoveTemp(SpecialMountBinding));

	FInputKeyBinding HorseBinding(FInputChord(EKeys::H, false, true, false, false), IE_Pressed);
	HorseBinding.KeyDelegate.GetDelegateForManualSet().BindUObject(
		this, &AMT2PlayerController::HandleToggleHorse);
	InputComponent->KeyBindings.Add(MoveTemp(HorseBinding));

	for (int32 SlotIndex = 0; SlotIndex < 8; ++SlotIndex)
	{
		const FKey Key = Bindings->GetKey(UMT2KeyBindingSubsystem::QuickSlot(SlotIndex));
		if (!Key.IsValid())
		{
			continue;
		}
		// BindKey has no payload overload; a manual binding carries the slot index.
		FInputKeyBinding SlotBinding(FInputChord(Key), IE_Pressed);
		SlotBinding.KeyDelegate.GetDelegateForManualSet().BindUObject(
			this, &AMT2PlayerController::HandleQuickSlotIndex, SlotIndex);
		InputComponent->KeyBindings.Add(MoveTemp(SlotBinding));
	}
}

void AMT2PlayerController::HandleToggleNotifications()
{
	if (const AMT2HUD* MT2HUD = Cast<AMT2HUD>(GetHUD()))
	{
		if (UMT2NotificationsWidget* Notifications = MT2HUD->GetNotificationsWidget())
		{
			Notifications->ToggleNotifications();
		}
	}
}

void AMT2PlayerController::HandleToggleSpecialMount()
{
	if (AMT2PlayerCharacter* PlayerCharacter = Cast<AMT2PlayerCharacter>(GetPawn()))
	{
		if (UMT2MountComponent* Mounts = PlayerCharacter->GetMountComponent())
		{
			Mounts->ServerToggleSpecialMount();
		}
	}
}

void AMT2PlayerController::HandleToggleHorse()
{
	if (AMT2PlayerCharacter* PlayerCharacter = Cast<AMT2PlayerCharacter>(GetPawn()))
	{
		if (UMT2MountComponent* Mounts = PlayerCharacter->GetMountComponent())
		{
			Mounts->ServerToggleHorse();
		}
	}
}

void AMT2PlayerController::HandleToggleSystemMenu()
{
	if (AMT2HUD* MT2HUD = Cast<AMT2HUD>(GetHUD()))
	{
		MT2HUD->ToggleSystemMenu();
	}
}

void AMT2PlayerController::HandlePickupItems()
{
	if (AMT2PlayerCharacter* PlayerCharacter = Cast<AMT2PlayerCharacter>(GetPawn()))
	{
		PlayerCharacter->RequestPickupNearbyItems();
	}
}

void AMT2PlayerController::HandleToggleFirstPerson()
{
	if (AMT2PlayerCharacter* PlayerCharacter = Cast<AMT2PlayerCharacter>(GetPawn()))
	{
		PlayerCharacter->ToggleFirstPerson();
	}
}

void AMT2PlayerController::HandleVoiceTalkPressed()
{
	if (UMT2VoiceChatClientSubsystem* Voice =
		GetWorld() ? GetWorld()->GetSubsystem<UMT2VoiceChatClientSubsystem>() : nullptr)
	{
		if (FMT2AudioUserSettings::GetVoiceToggleMode() && Voice->IsTalking())
		{
			Voice->StopTalking();
		}
		else
		{
			Voice->StartTalking();
		}
	}
}

void AMT2PlayerController::HandleVoiceTalkReleased()
{
	if (UMT2VoiceChatClientSubsystem* Voice =
		GetWorld() ? GetWorld()->GetSubsystem<UMT2VoiceChatClientSubsystem>() : nullptr)
	{
		if (!FMT2AudioUserSettings::GetVoiceToggleMode())
		{
			Voice->StopTalking();
		}
	}
}

bool AMT2PlayerController::IsPointerOverGameUI() const
{
	if (!FSlateApplication::IsInitialized())
	{
		return false;
	}

	FSlateApplication& SlateApplication = FSlateApplication::Get();
	const FWidgetPath WidgetPath = SlateApplication.LocateWindowUnderMouse(
		SlateApplication.GetCursorPos(), SlateApplication.GetInteractiveTopLevelWindows(), false);

	for (int32 WidgetIndex = 0; WidgetIndex < WidgetPath.Widgets.Num(); ++WidgetIndex)
	{
		const FArrangedWidget& ArrangedWidget = WidgetPath.Widgets[WidgetIndex];
		if (ArrangedWidget.Widget->GetTypeAsString() == TEXT("SObjectWidget"))
		{
			return true;
		}
	}

	return false;
}

void AMT2PlayerController::HandleToggleCharacter()
{
	if (AMT2HUD* MT2HUD = Cast<AMT2HUD>(GetHUD()))
	{
		MT2HUD->ToggleCharacterWindow();
	}
}

void AMT2PlayerController::HandleToggleSkill()
{
	if (AMT2HUD* MT2HUD = Cast<AMT2HUD>(GetHUD()))
	{
		MT2HUD->ToggleSkillWindow();
	}
}

void AMT2PlayerController::HandleToggleBelt()
{
	if (AMT2HUD* MT2HUD = Cast<AMT2HUD>(GetHUD()))
	{
		MT2HUD->ToggleBeltWindow();
	}
}

void AMT2PlayerController::HandleToggleMessenger()
{
	if (AMT2HUD* MT2HUD = Cast<AMT2HUD>(GetHUD()))
	{
		MT2HUD->ToggleMessengerWindow();
	}
}

void AMT2PlayerController::HandleToggleMinimap()
{
	if (AMT2HUD* MT2HUD = Cast<AMT2HUD>(GetHUD()))
	{
		MT2HUD->ToggleMinimapWindow();
	}
}

void AMT2PlayerController::HandleToggleInventory()
{
	if (AMT2HUD* MT2HUD = Cast<AMT2HUD>(GetHUD()))
	{
		MT2HUD->ToggleInventory();
	}
}

void AMT2PlayerController::HandleQuickSlotIndex(int32 SlotIndex)
{
	if (AMT2HUD* MT2HUD = Cast<AMT2HUD>(GetHUD()))
	{
		MT2HUD->ActivateQuickSlot(SlotIndex);
	}
}

void AMT2PlayerController::HandleToggleChat()
{
	if (!EnsureChatWidget())
	{
		return;
	}

	if (ChatWidget->IsInputOpen())
	{
		return;
	}

	// UI-only input keeps WASD/attack from firing while typing. Focus the text box itself (not the
	// whole widget) so keystrokes actually land in the input field.
	ChatWidget->OpenForInput();
	FInputModeUIOnly InputMode;
	if (UWidget* InputWidget = ChatWidget->GetInputBoxWidget())
	{
		InputMode.SetWidgetToFocus(InputWidget->TakeWidget());
	}
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
	ChatWidget->FocusInput();
	GetWorldTimerManager().SetTimerForNextTick(
		FTimerDelegate::CreateUObject(this, &AMT2PlayerController::FocusChatInput));
}

bool AMT2PlayerController::EnsureChatWidget()
{
	if (ChatWidget)
	{
		return true;
	}
	const TSubclassOf<UMT2ChatWidget> ChatWidgetClass =
		LoadClass<UMT2ChatWidget>(nullptr, UMT2PathSettings::Path(TEXT("UI_MT2Chat")));
	if (!ChatWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("Required UI asset /Game/UI/MT2Chat is missing or invalid."));
		return false;
	}
	ChatWidget = CreateWidget<UMT2ChatWidget>(this, ChatWidgetClass);
	if (!ChatWidget)
	{
		return false;
	}
	ChatWidget->AddToViewport(150);
	ChatWidget->OnChatClosed.AddDynamic(this, &AMT2PlayerController::HandleChatClosed);
	ChatWidget->OnCommandSubmitted.AddDynamic(this, &AMT2PlayerController::HandleChatCommandSubmitted);
	ChatWidget->OnMessageSubmitted.AddDynamic(this, &AMT2PlayerController::HandleChatMessageSubmitted);
	return true;
}

void AMT2PlayerController::FocusChatInput()
{
	if (ChatWidget && ChatWidget->IsInputOpen())
	{
		ChatWidget->FocusInput();
	}
}

void AMT2PlayerController::HandleChatClosed()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::Cleared);
	}
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
	GetWorldTimerManager().SetTimerForNextTick(
		FTimerDelegate::CreateUObject(this, &AMT2PlayerController::RestoreGameFocusAfterChat));
}

void AMT2PlayerController::RestoreGameFocusAfterChat()
{
	// A fast second Enter may reopen chat before this next-tick callback runs. Never steal focus back
	// from the newly opened text box in that case.
	if (ChatWidget && ChatWidget->IsInputOpen())
	{
		ChatWidget->FocusInput();
		return;
	}
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport(EFocusCause::SetDirectly);
	}
}

void AMT2PlayerController::HandleChatCommandSubmitted(const FString& Command)
{
	if (AMT2PlayerCharacter* MT2Character = Cast<AMT2PlayerCharacter>(GetPawn()))
	{
		MT2Character->ServerExecuteChatCommand(Command);
	}
}

void AMT2PlayerController::HandleChatMessageSubmitted(const FString& Message)
{
	ServerSubmitGlobalChat(Message);
}

void AMT2PlayerController::ServerSubmitGlobalChat_Implementation(const FString& Message)
{
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (Now - LastGlobalChatSubmitTime < 0.35)
	{
		return;
	}
	LastGlobalChatSubmitTime = Now;
	FString ChatText = Message.TrimStartAndEnd();
	if (ChatText.RemoveFromStart(TEXT("%")))
	{
		ChatText.TrimStartAndEndInline();
		if (AMT2PlayerState* State = GetPlayerState<AMT2PlayerState>())
		{
			if (UMT2GuildComponent* Guild = State->GetGuildComponent()) Guild->SendGuildChat(ChatText);
		}
		return;
	}
	const bool bBroadcastToAllServers = ChatText.RemoveFromStart(TEXT("!"));
	ChatText.TrimStartAndEndInline();
	if (ChatText.IsEmpty())
	{
		return;
	}

	const AMT2PlayerState* MT2PlayerState = GetPlayerState<AMT2PlayerState>();
	const FString SenderName = MT2PlayerState ? MT2PlayerState->GetCharacterName() : GetName();
	const EMT2Empire Empire = MT2PlayerState ? MT2PlayerState->GetEmpire() : EMT2Empire::None;
	if (UMT2ServerRuntimeSubsystem* Runtime = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMT2ServerRuntimeSubsystem>() : nullptr)
	{
		Runtime->PublishChat(Empire, SenderName, ChatText, bBroadcastToAllServers);
	}
}

void AMT2PlayerController::HandleChatReceived(
	EMT2Empire Empire, const FString& SenderName, const FString& Message, bool bWorldBroadcast)
{
	ClientReceiveGlobalChat(Empire, SenderName, Message, bWorldBroadcast);
}

void AMT2PlayerController::ClientReceiveGlobalChat_Implementation(
	EMT2Empire Empire, const FString& SenderName, const FString& Message, bool bWorldBroadcast)
{
	if (EnsureChatWidget())
	{
		ChatWidget->AddGlobalLine(Empire, SenderName, Message, bWorldBroadcast);
	}
	if (GetWorld())
	{
		for (TActorIterator<AMT2PlayerCharacter> It(GetWorld()); It; ++It)
		{
			AMT2PlayerCharacter* Sender = *It;
			const AMT2PlayerState* SenderState = Sender
				? Sender->GetPlayerState<AMT2PlayerState>() : nullptr;
			if (SenderState && SenderState->GetCharacterName() == SenderName)
			{
				if (UMT2NameplateComponent* Nameplate = Sender->GetNameplateComponent())
				{
					Nameplate->ShowChatMessage(Message, bWorldBroadcast);
				}
				break;
			}
		}
	}
}

void AMT2PlayerController::AddInfoChatLine(const FString& Message)
{
	if (EnsureChatWidget())
	{
		ChatWidget->AddLine(Message);
	}
}

void AMT2PlayerController::AddRewardChatLine(const FString& Message)
{
	if (EnsureChatWidget())
	{
		ChatWidget->AddRewardLine(Message);
	}
}
