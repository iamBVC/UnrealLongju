/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "World/MT2Portal.h"

#include "Characters/MT2PlayerCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "Game/MT2GameStateBase.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/PackageName.h"
#include "Player/MT2PlayerController.h"
#include "Player/MT2PlayerState.h"
#include "Quests/MT2QuestTableAsset.h"
#include "World/MT2MapUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogMT2Portal, Log, All);

AMT2Portal::AMT2Portal()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickInterval = 0.2f;
	// Placed level actor with no dynamic state: it exists on server and clients from the level, and the
	// overlap/teleport runs server-side, so it needs no replication.
	bReplicates = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	PortalMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PortalMesh"));
	PortalMesh->SetupAttachment(SceneRoot);
	PortalMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PortalMesh->SetGenerateOverlapEvents(false);

	PortalSkeletalMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("PortalSkeletalMesh"));
	PortalSkeletalMesh->SetupAttachment(SceneRoot);
	PortalSkeletalMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PortalSkeletalMesh->SetGenerateOverlapEvents(false);

	PortalTrigger = CreateDefaultSubobject<UBoxComponent>(TEXT("PortalTrigger"));
	PortalTrigger->SetupAttachment(SceneRoot);
	PortalTrigger->SetBoxExtent(FVector(100.0f, 100.0f, 150.0f));
	PortalTrigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PortalTrigger->SetCollisionObjectType(ECC_WorldDynamic);
	PortalTrigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	PortalTrigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	PortalTrigger->SetGenerateOverlapEvents(true);
}

void AMT2Portal::ConfigureSkeletalVisual(
	USkeletalMesh* SkeletalMesh, TSubclassOf<UAnimInstance> AnimClass, const FTransform& RelativeTransform)
{
	if (!PortalSkeletalMesh)
	{
		return;
	}

	PortalSkeletalMesh->SetSkeletalMesh(SkeletalMesh);
	PortalSkeletalMesh->SetRelativeTransform(RelativeTransform);
	PortalSkeletalMesh->SetAnimInstanceClass(AnimClass);
	PortalSkeletalMesh->SetVisibility(SkeletalMesh != nullptr, true);
	if (PortalMesh)
	{
		PortalMesh->SetVisibility(SkeletalMesh == nullptr, true);
	}
}

void AMT2Portal::BeginPlay()
{
	Super::BeginPlay();
	// Overlaps are server-authoritative; the client never triggers the transfer.
	if (HasAuthority() && PortalTrigger)
	{
		PortalTrigger->OnComponentBeginOverlap.AddDynamic(this, &AMT2Portal::HandleBeginOverlap);
		PortalTrigger->OnComponentEndOverlap.AddDynamic(this, &AMT2Portal::HandleEndOverlap);
		PortalTrigger->UpdateOverlaps();
		UE_LOG(LogMT2Portal, Display,
			TEXT("Portal %s active at %s, extent %s, destination %s (overlaps=%s)."),
			*GetName(), *PortalTrigger->GetComponentLocation().ToCompactString(),
			*PortalTrigger->GetScaledBoxExtent().ToCompactString(), *MapName,
			PortalTrigger->GetGenerateOverlapEvents() ? TEXT("enabled") : TEXT("disabled"));
	}
	else if (PortalTrigger)
	{
		// Level actors exist on clients for visuals, but clients do not need overlap broadphase work.
		PortalTrigger->SetGenerateOverlapEvents(false);
		PortalTrigger->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	SetActorTickEnabled(HasAuthority());
}

void AMT2Portal::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	PollPortalVolume();
}

void AMT2Portal::PollPortalVolume()
{
	if (!HasAuthority() || !PortalTrigger ||
		PortalTrigger->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
	{
		return;
	}

	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MT2PortalVolumePoll), false, this);
	const bool bFoundPawn = GetWorld()->OverlapMultiByObjectType(
		Overlaps, PortalTrigger->GetComponentLocation(), PortalTrigger->GetComponentQuat(),
		FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeBox(PortalTrigger->GetScaledBoxExtent()), QueryParams);
	if (!bFoundPawn)
	{
		return;
	}

	TSet<AMT2PlayerCharacter*> Players;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (AMT2PlayerCharacter* Player = Cast<AMT2PlayerCharacter>(Overlap.GetActor()))
		{
			Players.Add(Player);
		}
	}
	for (AMT2PlayerCharacter* Player : Players)
	{
		TeleportPlayer(Player);
	}
}

void AMT2Portal::HandleBeginOverlap(
	UPrimitiveComponent* /*OverlappedComponent*/, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	if (AMT2PlayerCharacter* Player = Cast<AMT2PlayerCharacter>(OtherActor);
		Player && OtherComp == Player->GetCapsuleComponent())
	{
		UE_LOG(LogMT2Portal, Display, TEXT("Player %s entered portal %s -> %s."),
			*Player->GetName(), *GetName(), *MapName);
		TeleportPlayer(Player);
	}
}

void AMT2Portal::HandleEndOverlap(
	UPrimitiveComponent* /*OverlappedComponent*/, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 /*OtherBodyIndex*/)
{
	const AMT2PlayerCharacter* Player = Cast<AMT2PlayerCharacter>(OtherActor);
	if (Player && OtherComp == Player->GetCapsuleComponent())
	{
		PlayersInTransit.Remove(OtherActor);
	}
}

void AMT2Portal::TeleportPlayer(AMT2PlayerCharacter* Player)
{
	if (!HasAuthority() || !Player || MapName.TrimStartAndEnd().IsEmpty() ||
		(!bUseCity && DestinationLocation.ContainsNaN()) || PlayersInTransit.Contains(Player))
	{
		return;
	}

#if WITH_EDITOR
	if (GetWorld() && GetWorld()->WorldType == EWorldType::PIE)
	{
		PlayersInTransit.Add(Player);
		if (!TeleportPlayerInPIE(Player))
		{
			PlayersInTransit.Remove(Player);
		}
		return;
	}
#endif

	AMT2PlayerController* Controller = Cast<AMT2PlayerController>(Player->GetController());
	AMT2PlayerState* State = Player->GetPlayerState<AMT2PlayerState>();
	if (!Controller || !State)
	{
		return;
	}
	PlayersInTransit.Add(Player);

	// Stash the destination so the arrival map places the player there (unless we're sending them to the
	// city, which the transfer's town-spawn path handles).
	if (bUseCity)
	{
		State->ClearPendingSpawnLocation();
	}
	else
	{
		State->SetPendingSpawnLocation(DestinationLocation);
	}

	int32 Channel = 0;
	if (const AMT2GameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState<AMT2GameStateBase>() : nullptr)
	{
		Channel = GameState->GetServerChannel();
	}
	// The coordinator resolves which server hosts MapName and issues the transfer ticket; bUseCity maps
	// to the transfer's force-town-spawn flag.
	if (!Controller->RequestMapTransferFromServer(MapName, Channel, bUseCity, !bUseCity))
	{
		if (!bUseCity)
		{
			State->ClearPendingSpawnLocation();
		}
		PlayersInTransit.Remove(Player);
	}
}

bool AMT2Portal::TeleportPlayerInPIE(AMT2PlayerCharacter* Player)
{
#if WITH_EDITOR
	UWorld* World = GetWorld();
	if (!World || World->WorldType != EWorldType::PIE || !Player)
	{
		return false;
	}
	if (MT2MapUtils::HasPendingPIETravel(*World))
	{
		UE_LOG(LogMT2Portal, Display, TEXT("PIE portal %s ignored while another map travel is pending."), *GetName());
		return true;
	}

	const FString DestinationMapId = FPackageName::GetShortName(MapName.TrimStartAndEnd());
	TArray<FString> MapAliases{MapName, DestinationMapId};
	if (const UMT2QuestTableAsset* Tables = LoadObject<UMT2QuestTableAsset>(
		nullptr, UMT2QuestTableAsset::GetAssetPath()))
	{
		if (const FMT2QuestMapDefinition* Map = Tables->FindMapById(DestinationMapId))
		{
			MapAliases.AddUnique(Map->MapId);
			if (!Map->WorldPackagePath.IsEmpty())
			{
				MapAliases.AddUnique(Map->WorldPackagePath);
			}
		}
	}

	const FString TargetPackage = MT2MapUtils::ResolvePIEWorldPackage(MapAliases);
	if (TargetPackage.IsEmpty())
	{
		UE_LOG(LogMT2Portal, Error,
			TEXT("PIE portal %s cannot find imported world for destination map %s."),
			*GetName(), *MapName);
		return false;
	}

	const FString CurrentPackage = UWorld::StripPIEPrefixFromPackageName(
		World->GetOutermost()->GetName(), World->StreamingLevelsPrefix);
	if (CurrentPackage.Equals(TargetPackage, ESearchCase::IgnoreCase) && !bUseCity)
	{
		const FVector TraceStart(DestinationLocation.X, DestinationLocation.Y, 1000000.0f);
		const FVector TraceEnd(DestinationLocation.X, DestinationLocation.Y, -1000000.0f);
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MT2PIEPortalGround), false, Player);
		FHitResult GroundHit;
		if (!World->LineTraceSingleByObjectType(
			GroundHit, TraceStart, TraceEnd,
			FCollisionObjectQueryParams(ECC_WorldStatic), QueryParams))
		{
			UE_LOG(LogMT2Portal, Warning,
				TEXT("PIE portal %s found no ground at destination %.0f, %.0f."),
				*GetName(), DestinationLocation.X, DestinationLocation.Y);
			return false;
		}

		const UCapsuleComponent* Capsule = Player->GetCapsuleComponent();
		const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.0f;
		const FVector Target = GroundHit.ImpactPoint + FVector(0.0f, 0.0f, HalfHeight + 2.0f);
		Player->GetCharacterMovement()->StopMovementImmediately();
		Player->SetActorLocation(Target, false, nullptr, ETeleportType::ResetPhysics);
		PlayersInTransit.Remove(Player);
		UE_LOG(LogMT2Portal, Display, TEXT("PIE portal %s teleported %s within %s to %s."),
			*GetName(), *Player->GetName(), *DestinationMapId, *Target.ToCompactString());
		return true;
	}

	FString TravelUrl = TargetPackage;
	if (bUseCity)
	{
		TravelUrl += TEXT("?pie_portal_town=1");
	}
	else
	{
		TravelUrl += FString::Printf(TEXT("?pie_portal_x=%.9g?pie_portal_y=%.9g"),
			DestinationLocation.X, DestinationLocation.Y);
	}
	TravelUrl += TEXT("?SeamlessTravel");

	// A hard PIE server travel disconnects the client before the destination world starts accepting
	// connections. Keep the PIE connection and its player state alive across the map switch instead.
	if (IConsoleVariable* AllowPIESeamlessTravel =
		IConsoleManager::Get().FindConsoleVariable(TEXT("net.AllowPIESeamlessTravel")))
	{
		AllowPIESeamlessTravel->Set(1, ECVF_SetByCode);
	}
	if (AGameModeBase* GameMode = World->GetAuthGameMode())
	{
		GameMode->bUseSeamlessTravel = true;
	}

	UE_LOG(LogMT2Portal, Display,
		TEXT("PIE portal %s traveling to %s. PIE server travel moves every connected PIE player."),
		*GetName(), *TravelUrl);
	if (!World->ServerTravel(TravelUrl, true))
	{
		UE_LOG(LogMT2Portal, Error, TEXT("PIE portal %s failed to start server travel to %s."),
			*GetName(), *TravelUrl);
		return false;
	}
	return true;
#else
	return false;
#endif
}
