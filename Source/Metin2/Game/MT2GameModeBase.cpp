/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Game/MT2GameModeBase.h"

#include "Characters/MT2PlayerCharacter.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Persistence/MT2PersistenceComponent.h"
#include "Game/MT2GameStateBase.h"
#include "Player/MT2PlayerController.h"
#include "Player/MT2PlayerState.h"
#include "Duel/MT2DuelComponent.h"
#include "Quests/MT2QuestManagerComponent.h"
#include "Server/MT2ServerRuntimeSubsystem.h"
#include "TimerManager.h"
#include "UI/MT2HUD.h"
#include "World/MT2MapPresentationActor.h"

#if WITH_EDITOR
namespace
{
	EMT2Empire ResolvePIEEmpire(UWorld* World)
	{
		for (TActorIterator<AMT2MapPresentationActor> It(World); It; ++It)
		{
			const FString MapId = It->MapId.ToLower();
			if (MapId.StartsWith(TEXT("metin2_map_b")))
			{
				return EMT2Empire::Chunjo;
			}
			if (MapId.StartsWith(TEXT("metin2_map_c")))
			{
				return EMT2Empire::Jinno;
			}
			if (MapId.StartsWith(TEXT("metin2_map_a")))
			{
				return EMT2Empire::Shinsoo;
			}
		}
		return EMT2Empire::Shinsoo;
	}
}
#endif

AMT2GameModeBase::AMT2GameModeBase()
{
	GameStateClass = AMT2GameStateBase::StaticClass();
	PlayerStateClass = AMT2PlayerState::StaticClass();
	PlayerControllerClass = AMT2PlayerController::StaticClass();
	DefaultPawnClass = AMT2PlayerCharacter::StaticClass();
	HUDClass = AMT2HUD::StaticClass();
}

void AMT2GameModeBase::BeginPlay()
{
	Super::BeginPlay();
#if WITH_EDITOR
	// Editor-selected starts can intentionally lie outside imported map bounds.
	if (GetWorld()->WorldType == EWorldType::PIE) { return; }
#endif
	GetWorldTimerManager().SetTimer(
		FallRecoveryTimer, this, &AMT2GameModeBase::RecoverPlayersOutsideMap, 1.0f, true, 1.0f);
}

void AMT2GameModeBase::PreLogin(
	const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId,
	FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	if (!ErrorMessage.IsEmpty()) return;
	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		if (const UMT2ServerRuntimeSubsystem* Runtime = GameInstance->GetSubsystem<UMT2ServerRuntimeSubsystem>();
			Runtime && Runtime->IsCoordinator())
		{
			ErrorMessage = TEXT("Coordinator instances do not accept game clients.");
		}
	}
}

void AMT2GameModeBase::PreLoginAsync(
	const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId,
	const FOnPreLoginCompleteDelegate& OnComplete)
{
	FString Error;
	PreLogin(Options, Address, UniqueId, Error);
	if (!Error.IsEmpty())
	{
		OnComplete.ExecuteIfBound(Error);
		return;
	}
	UMT2ServerRuntimeSubsystem* Runtime = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMT2ServerRuntimeSubsystem>() : nullptr;
	if (!Runtime || !Runtime->IsMapServer())
	{
		OnComplete.ExecuteIfBound(FString());
		return;
	}
	RemoveExpiredAdmissions(FPlatformTime::Seconds());
	const FString Ticket = UGameplayStatics::ParseOption(Options, TEXT("ticket"));
	if (Ticket.IsEmpty() || PendingAdmissions.Contains(Ticket))
	{
		OnComplete.ExecuteIfBound(TEXT("A valid, unused map admission ticket is required."));
		return;
	}
	TWeakObjectPtr<AMT2GameModeBase> WeakThis(this);
	Runtime->ClaimTransferTicket(Ticket,
		[WeakThis, Ticket, OnComplete](FMT2TransferClaimResult&& Result)
		{
			if (!WeakThis.IsValid())
			{
				OnComplete.ExecuteIfBound(TEXT("Map server stopped during admission."));
				return;
			}
			if (!Result.bSucceeded)
			{
				OnComplete.ExecuteIfBound(Result.Error.IsEmpty() ? TEXT("Admission ticket was rejected.") : Result.Error);
				return;
			}
			FPendingAdmission Admission;
			Admission.Result = MoveTemp(Result);
			const UMT2ServerRuntimeSubsystem* ActiveRuntime = WeakThis->GetGameInstance()
				? WeakThis->GetGameInstance()->GetSubsystem<UMT2ServerRuntimeSubsystem>() : nullptr;
			Admission.ExpiresAtSeconds = FPlatformTime::Seconds() + (ActiveRuntime
				? ActiveRuntime->GetConfig().TransferTicketLifetimeSeconds : 30.0);
			WeakThis->PendingAdmissions.Add(Ticket, MoveTemp(Admission));
			OnComplete.ExecuteIfBound(FString());
		});
}

FString AMT2GameModeBase::InitNewPlayer(
	APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId,
	const FString& Options, const FString& Portal)
{
	FString Error = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);

#if WITH_EDITOR
	if (Error.IsEmpty() && GetWorld() && GetWorld()->WorldType == EWorldType::PIE && NewPlayerController)
	{
		const FString ForceTown = UGameplayStatics::ParseOption(Options, TEXT("pie_portal_town"));
		if (ForceTown == TEXT("1"))
		{
			PendingPIETownSpawns.Add(NewPlayerController);
		}
		else
		{
			const FString XText = UGameplayStatics::ParseOption(Options, TEXT("pie_portal_x"));
			const FString YText = UGameplayStatics::ParseOption(Options, TEXT("pie_portal_y"));
			if (!XText.IsEmpty() && !YText.IsEmpty())
			{
				if (AMT2PlayerState* State = NewPlayerController->GetPlayerState<AMT2PlayerState>())
				{
					State->SetPendingSpawnLocation(FVector2D(FCString::Atof(*XText), FCString::Atof(*YText)));
				}
			}
		}
	}
#endif

	UMT2ServerRuntimeSubsystem* Runtime = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMT2ServerRuntimeSubsystem>() : nullptr;
	if (!Error.IsEmpty() || !Runtime || !Runtime->IsMapServer()) return Error;

	const FString Ticket = UGameplayStatics::ParseOption(Options, TEXT("ticket"));
	FPendingAdmission PendingAdmission;
	if (!PendingAdmissions.RemoveAndCopyValue(Ticket, PendingAdmission) ||
		FPlatformTime::Seconds() >= PendingAdmission.ExpiresAtSeconds)
	{
		return TEXT("Validated admission data was not found.");
	}
	FMT2TransferClaimResult& Admission = PendingAdmission.Result;
	AMT2PlayerState* PlayerState = NewPlayerController
		? NewPlayerController->GetPlayerState<AMT2PlayerState>() : nullptr;
	UMT2PersistenceComponent* Persistence = PlayerState ? PlayerState->GetPersistenceComponent() : nullptr;
	if (!Persistence)
	{
		return TEXT("Player persistence component is unavailable.");
	}

	TWeakObjectPtr<AMT2GameModeBase> WeakThis(this);
	TWeakObjectPtr<APlayerController> WeakPlayer(NewPlayerController);
	TWeakObjectPtr<AMT2PlayerState> WeakPlayerState(PlayerState);
	Persistence->OnLoadFinishedNative.AddLambda(
		[WeakThis, WeakPlayer, WeakPlayerState, Admission, Ticket]
		(bool bSucceeded, bool bFound, bool bHasWorldPosition, const FString& LoadError)
		{
			if (!WeakThis.IsValid() || !WeakPlayer.IsValid()) return;
			UMT2ServerRuntimeSubsystem* ActiveRuntime = WeakThis->GetGameInstance()
				? WeakThis->GetGameInstance()->GetSubsystem<UMT2ServerRuntimeSubsystem>() : nullptr;
			if (!bSucceeded)
			{
				if (ActiveRuntime)
				{
					ActiveRuntime->CompleteTransferTicket(Ticket, false, [](FMT2TransferCompleteResult&&) {});
				}
				WeakPlayer->ClientReturnToMainMenuWithTextReason(FText::FromString(
					LoadError.IsEmpty() ? TEXT("Character loading failed.") : LoadError));
				return;
			}
			if (!bFound && WeakPlayerState.IsValid() && !Admission.Character.CharacterId.IsEmpty())
			{
				WeakPlayerState->SetCharacterName(Admission.Character.CharacterName);
				WeakPlayerState->SetCharacterLevel(Admission.Character.Level);
				WeakPlayerState->SetCharacterAppearance(Admission.Character.Appearance);
				WeakPlayerState->SetEmpire(Admission.Character.Empire);
			}
			if (!ActiveRuntime)
			{
				WeakPlayer->ClientReturnToMainMenuWithTextReason(
					FText::FromString(TEXT("Map admission service became unavailable.")));
				return;
			}
			const bool bForceTownSpawn = Admission.bForceTownSpawn || !bFound || !bHasWorldPosition;
			ActiveRuntime->CompleteTransferTicket(Ticket, true,
				[WeakThis, WeakPlayer, bForceTownSpawn](FMT2TransferCompleteResult&& Result)
				{
					if (!WeakThis.IsValid() || !WeakPlayer.IsValid()) return;
					if (!Result.bSucceeded)
					{
						WeakPlayer->ClientReturnToMainMenuWithTextReason(FText::FromString(
							Result.Error.IsEmpty() ? TEXT("Map admission could not be completed.") : Result.Error));
						return;
					}
					WeakThis->RestartPlayer(WeakPlayer.Get());
					WeakThis->RepairPlayerSpawn(WeakPlayer.Get(), bForceTownSpawn);
					if (AMT2PlayerController* MT2Controller = Cast<AMT2PlayerController>(WeakPlayer.Get()))
					{
						MT2Controller->SendFullMapNpcSnapshot();
						MT2Controller->ClientWorldReady();
					}

					// Quest entry events (login/enter/letter) are fired by the quest manager itself once
					// the pawn exists - this admission path only runs on a real map server, and PIE has
					// to work the same way.
				});
		});
	PlayerState->SetPersistenceIdentity(Admission.CharacterId, Admission.AccountId);
	return FString();
}

bool AMT2GameModeBase::RepairPlayerSpawn(APlayerController* PlayerController, bool bForceTownSpawn) const
{
	APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!Pawn || !GetWorld()) return false;

	const AMT2MapPresentationActor* Presentation = nullptr;
	for (TActorIterator<AMT2MapPresentationActor> It(GetWorld()); It; ++It)
	{
		Presentation = *It;
		break;
	}
	if (!Presentation)
	{
		return false;
	}

	AMT2PlayerState* PlayerState = PlayerController->GetPlayerState<AMT2PlayerState>();

	// Where to land: the empire city (town spawn), a portal's pending destination, or keep the persisted
	// position when it's already valid on this map.
	FVector2D TargetXY;
	FVector2D PendingXY;
	if (bForceTownSpawn)
	{
		TargetXY = Presentation->GetTownSpawn(PlayerState ? PlayerState->GetEmpire() : EMT2Empire::None);
	}
	else if (PlayerState && PlayerState->ConsumePendingSpawnLocation(PendingXY))
	{
		TargetXY = PendingXY;
	}
	else if (Presentation->ContainsWorldLocation(Pawn->GetActorLocation()))
	{
		return true;
	}
	else
	{
		TargetXY = Presentation->GetTownSpawn(PlayerState ? PlayerState->GetEmpire() : EMT2Empire::None);
	}

	const FVector TraceStart(TargetXY.X, TargetXY.Y, 1000000.0f);
	const FVector TraceEnd(TargetXY.X, TargetXY.Y, -1000000.0f);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MT2TownSpawnGround), false, Pawn);
	FHitResult GroundHit;
	if (!GetWorld()->LineTraceSingleByObjectType(
		GroundHit, TraceStart, TraceEnd, FCollisionObjectQueryParams(ECC_WorldStatic), QueryParams))
	{
		UE_LOG(LogGameMode, Warning, TEXT("No ground found at spawn %.0f, %.0f for map %s."),
			TargetXY.X, TargetXY.Y, *Presentation->MapId);
		return false;
	}

	const UCapsuleComponent* Capsule = Pawn->FindComponentByClass<UCapsuleComponent>();
	const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.0f;
	const FVector SpawnLocation = GroundHit.ImpactPoint + FVector(0.0f, 0.0f, HalfHeight + 2.0f);
	Pawn->SetActorLocation(SpawnLocation, false, nullptr, ETeleportType::ResetPhysics);
	if (ACharacter* Character = Cast<ACharacter>(Pawn))
	{
		Character->GetCharacterMovement()->StopMovementImmediately();
		Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	}
	UE_LOG(LogGameMode, Log, TEXT("Placed player at %s town spawn %s."),
		*Presentation->MapId, *SpawnLocation.ToCompactString());
	return true;
}

void AMT2GameModeBase::RemoveExpiredAdmissions(double NowSeconds)
{
	for (auto It = PendingAdmissions.CreateIterator(); It; ++It)
	{
		if (NowSeconds >= It.Value().ExpiresAtSeconds) It.RemoveCurrent();
	}
}

void AMT2GameModeBase::RecoverPlayersOutsideMap()
{
	UWorld* World = GetWorld();
	if (!World) return;

	const AMT2MapPresentationActor* Presentation = nullptr;
	for (TActorIterator<AMT2MapPresentationActor> It(World); It; ++It)
	{
		Presentation = *It;
		break;
	}
	if (!Presentation) return;

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PlayerController = It->Get();
		APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
		if (!Pawn) continue;

		const FVector Location = Pawn->GetActorLocation();
		bool bOutsideMap = Location.ContainsNaN() || !Presentation->ContainsWorldLocation(Location);
		if (!bOutsideMap)
		{
			const UCapsuleComponent* Capsule = Pawn->FindComponentByClass<UCapsuleComponent>();
			const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.0f;
			const FVector TraceStart = Location + FVector(0.0f, 0.0f, HalfHeight + 50.0f);
			const FVector TraceEnd = Location - FVector(0.0f, 0.0f, 1000000.0f);
			FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MT2PlayerFallRecovery), false, Pawn);
			FHitResult GroundHit;
			bOutsideMap = !World->LineTraceSingleByObjectType(
				GroundHit, TraceStart, TraceEnd,
				FCollisionObjectQueryParams(ECC_WorldStatic), QueryParams);
		}
		if (bOutsideMap)
		{
			RepairPlayerSpawn(PlayerController, true);
		}
	}
}

void AMT2GameModeBase::PostLogin(APlayerController* NewPlayer)
{
#if WITH_EDITOR
	if (GetWorld() && GetWorld()->WorldType == EWorldType::PIE && NewPlayer)
	{
		if (AMT2PlayerState* MT2PlayerState = NewPlayer->GetPlayerState<AMT2PlayerState>())
		{
			FMT2CharacterAppearance RandomAppearance;
			RandomAppearance.Race = static_cast<EMT2CharacterRace>(FMath::RandHelper(4));
			RandomAppearance.Sex = static_cast<EMT2CharacterSex>(FMath::RandHelper(2));
			RandomAppearance.Style = static_cast<EMT2CharacterStyle>(FMath::RandHelper(2));
			MT2PlayerState->SetCharacterAppearance(RandomAppearance);
			if (MT2PlayerState->GetEmpire() == EMT2Empire::None)
			{
				MT2PlayerState->SetEmpire(ResolvePIEEmpire(GetWorld()));
			}
		}
	}
#endif

	Super::PostLogin(NewPlayer);
#if WITH_EDITOR
	if (GetWorld() && GetWorld()->WorldType == EWorldType::PIE)
	{
		if (AMT2PlayerController* Controller = Cast<AMT2PlayerController>(NewPlayer))
		{
			Controller->SendFullMapNpcSnapshot();
		}
	}
#endif
}

void AMT2GameModeBase::Logout(AController* Exiting)
{
	// AController destroys and clears its PlayerState immediately after this callback returns.
	// Capture the final character snapshot here; PlayerController::EndPlay is too late for a
	// normal network disconnect and remains only as a fallback for other world teardown paths.
	if (AMT2PlayerState* PlayerState = Exiting
		? Exiting->GetPlayerState<AMT2PlayerState>() : nullptr)
	{
		PlayerState->GetDuelComponent()->CancelAllDuels();
		if (UMT2PersistenceComponent* Persistence = PlayerState->GetPersistenceComponent())
		{
			if (UMT2ServerRuntimeSubsystem* Runtime = GetGameInstance()
				? GetGameInstance()->GetSubsystem<UMT2ServerRuntimeSubsystem>() : nullptr;
				Runtime && Runtime->IsMapServer())
			{
				Persistence->RequestSave(true);
				Runtime->NotifyCharacterLogout(
					Persistence->GetEntityId(), Persistence->GetOwnerId());
			}
		}
	}

	Super::Logout(Exiting);
}

void AMT2GameModeBase::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	const UMT2ServerRuntimeSubsystem* Runtime = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMT2ServerRuntimeSubsystem>() : nullptr;
	if (Runtime && (Runtime->IsGateway() || Runtime->IsMapServer()))
	{
		return;
	}
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);

#if WITH_EDITOR
	if (GetWorld() && GetWorld()->WorldType == EWorldType::PIE && NewPlayer)
	{
		bool bForceTownSpawn = PendingPIETownSpawns.Remove(NewPlayer) > 0;
		bForceTownSpawn |= UGameplayStatics::ParseOption(
			OptionsString, TEXT("pie_portal_town")) == TEXT("1");
		if (!bForceTownSpawn)
		{
			const FString XText = UGameplayStatics::ParseOption(OptionsString, TEXT("pie_portal_x"));
			const FString YText = UGameplayStatics::ParseOption(OptionsString, TEXT("pie_portal_y"));
			if (!XText.IsEmpty() && !YText.IsEmpty())
			{
				if (AMT2PlayerState* State = NewPlayer->GetPlayerState<AMT2PlayerState>())
				{
					State->SetPendingSpawnLocation(
						FVector2D(FCString::Atof(*XText), FCString::Atof(*YText)));
				}
			}
		}
		// Only explicit map travel overrides Unreal's Player Start/current-camera placement.
		const AMT2PlayerState* State = NewPlayer->GetPlayerState<AMT2PlayerState>();
		if (bForceTownSpawn || (State && State->HasPendingSpawnLocation()))
		{
			RepairPlayerSpawn(NewPlayer, bForceTownSpawn);
		}
	}
#endif
}
