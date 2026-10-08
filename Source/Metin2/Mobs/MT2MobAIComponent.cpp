/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Mobs/MT2MobAIComponent.h"

#include "AIController.h"
#include "Characters/MT2PlayerCharacter.h"
#include "Components/MT2HealthComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "FramePro/FramePro.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Mobs/MT2Mob.h"
#include "Mobs/MT2MobRuntimeSettings.h"
#include "Mobs/MT2MobTypes.h"
#include "Net/UnrealNetwork.h"
#include "Player/MT2PlayerState.h"
#include "World/MT2PlayerSpatialGridSubsystem.h"

namespace
{
	bool HasAIFlag(int32 Flags, EMT2MobAIFlag Flag)
	{
		return (Flags & (1 << static_cast<uint8>(Flag))) != 0;
	}
}

UMT2MobAIComponent::UMT2MobAIComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	const UMT2MobRuntimeSettings* Settings = GetDefault<UMT2MobRuntimeSettings>();
	SightRadius = Settings->AggressiveDetectionRadius;
	AggressiveDetectionRadius = Settings->AggressiveDetectionRadius;
	LeashRadius = Settings->MinimumLeashRadius;
}

void UMT2MobAIComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMT2MobAIComponent, State);
	DOREPLIFETIME(UMT2MobAIComponent, TargetActor);
}

void UMT2MobAIComponent::Configure(const FMT2MobDefinition& Definition)
{
	AIFlags = Definition.AIFlags;
	// Stones are stationary game objects. Some client mob_proto builds omit NOMOVE,
	// so keep the authoritative type rule as a runtime safeguard too.
	if (Definition.Type == EMT2MobType::Stone)
	{
		AIFlags |= 1 << static_cast<uint8>(EMT2MobAIFlag::NoMove);
	}
	ExtendedAIFlags = Definition.ExtendedAIFlags;
	Empire = Definition.Empire;
	const UMT2MobRuntimeSettings* Settings = GetDefault<UMT2MobRuntimeSettings>();
	SightRadius = FMath::Max(Definition.AggressiveSight, Settings->MinimumSightRadius);
	AggressiveDetectionRadius = Settings->AggressiveDetectionRadius;
	AggressiveHealthPercent = Definition.AggressiveHealthPercent;
	AttackRange = FMath::Max(Definition.AttackRange, Settings->MinimumAttackRange);
	LeashRadius = FMath::Max(
		SightRadius * Settings->LeashRadiusMultiplier,
		Settings->MinimumLeashRadius);
	ApplyMovementConstraints();
}

void UMT2MobAIComponent::ApplyMovementConstraints()
{
	if (!HasAIFlag(AIFlags, EMT2MobAIFlag::NoMove))
	{
		return;
	}
	if (AMT2Mob* Mob = Cast<AMT2Mob>(GetOwner()))
	{
		if (UCharacterMovementComponent* Movement = Mob->GetCharacterMovement())
		{
			// Runs both at import time on the Blueprint CDO (bakes the constraints into the class)
			// and at runtime spawn; only a live game-world mob has movement to stop.
			if (Mob->GetWorld() && Mob->GetWorld()->IsGameWorld())
			{
				Movement->StopMovementImmediately();
			}
			Movement->MaxWalkSpeed = 0.0f;
			Movement->bOrientRotationToMovement = false;
			Movement->bUseControllerDesiredRotation = false;
		}
	}
}

void UMT2MobAIComponent::InitializeHome(const FVector& Location)
{
	HomeLocation = Location;
	ApplyTickPolicy();
}

void UMT2MobAIComponent::ApplyTickPolicy()
{
	AMT2Mob* Mob = Cast<AMT2Mob>(GetOwner());
	if (!Mob)
	{
		return;
	}

	const bool bMovementState = State == EMT2MobAIState::Wandering
		|| State == EMT2MobAIState::Chasing
		|| State == EMT2MobAIState::Fleeing
		|| State == EMT2MobAIState::ReturningHome;
	const bool bCanMove = ((bMovementState && !Mob->IsCombatMotionLocked()) ||
		Mob->GetCharacterMovement()->HasRootMotionSources()) && !HasAIFlag(AIFlags, EMT2MobAIFlag::NoMove);
	if (UCharacterMovementComponent* Movement = Mob->GetCharacterMovement())
	{
		if (!bCanMove)
		{
			Movement->StopMovementImmediately();
		}
		Movement->SetComponentTickEnabled(bCanMove);
	}

}

void UMT2MobAIComponent::SetForceAggressive(bool bForceAggressive)
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	const int32 AggressiveMask = 1 << static_cast<uint8>(EMT2MobAIFlag::Aggressive);
	if (bForceAggressive) AIFlags |= AggressiveMask;
	else AIFlags &= ~AggressiveMask;
}

void UMT2MobAIComponent::TickBehavior(AAIController* Controller, float DeltaSeconds)
{
	FRAMEPRO_NAMED_SCOPE("MT2.MobAI.TickBehavior");
	TRACE_CPUPROFILER_EVENT_SCOPE(MT2_MobAI_TickBehavior);
	AMT2Mob* Mob = Cast<AMT2Mob>(GetOwner());
	UWorld* World = GetWorld();
	if (!Mob || !Controller || !World || !Mob->HasAuthority())
	{
		return;
	}

	// Old char_state.cpp: "Stone must not use battle state" - stones never chase, aggro or swing.
	// Their whole behavior is the once-per-second HP-threshold pulse on AMT2Mob.
	if (Mob->IsMetinStone())
	{
		Controller->StopMovement();
		Mob->GetCharacterMovement()->StopMovementImmediately();
		return;
	}

	if (Mob->GetHealthComponent()->IsDead())
	{
		NotifyDeath(Controller);
		return;
	}
	if (Mob->IsCombatMotionLocked())
	{
		Controller->StopMovement();
		Mob->GetCharacterMovement()->StopMovementImmediately();
		SetState(EMT2MobAIState::Attacking);
		return;
	}

	const float Now = World->GetTimeSeconds();
	const UMT2MobRuntimeSettings* Settings = GetDefault<UMT2MobRuntimeSettings>();
	if (!IsValidTarget(TargetActor))
	{
		SetTargetActor(nullptr);
	}
	const bool bCannotMove = HasAIFlag(AIFlags, EMT2MobAIFlag::NoMove);
	if (bCannotMove)
	{
		Controller->StopMovement();
		Mob->GetCharacterMovement()->StopMovementImmediately();
	}

	if (!TargetActor && HasAIFlag(AIFlags, EMT2MobAIFlag::Aggressive) && Now >= NextTargetSearchTime)
	{
		NextTargetSearchTime = Now + Settings->TargetSearchInterval;
		SetTargetActor(FindTarget());
	}

	if (TargetActor)
	{
		bHasWanderTarget = false;

		if (!bCannotMove &&
			FVector::DistSquared2D(Mob->GetActorLocation(), HomeLocation) > FMath::Square(LeashRadius))
		{
			SetTargetActor(nullptr);
			ReturnHome(Mob);
			return;
		}

		if (!bCannotMove && HasAIFlag(AIFlags, EMT2MobAIFlag::Coward))
		{
			FleeFromTarget(Mob);
			return;
		}

		const float Distance = FVector::Dist2D(Mob->GetActorLocation(), TargetActor->GetActorLocation());
		if (Distance > AttackRange)
		{
			if (!bCannotMove)
			{
				Mob->SetWalkRequested(false);
				SetState(EMT2MobAIState::Chasing);
				MoveTowards(
					Mob,
					TargetActor->GetActorLocation(),
					AttackRange * Settings->ChaseAcceptanceRangeFraction);
			}
			return;
		}

		Mob->GetCharacterMovement()->StopMovementImmediately();
		if (!bCannotMove)
		{
			// Yaw only - a character capsule should never pitch/roll toward a target above or below it.
			FRotator Facing = Mob->GetActorRotation();
			Facing.Yaw = (TargetActor->GetActorLocation() - Mob->GetActorLocation()).Rotation().Yaw;
			Mob->SetActorRotation(Facing);
		}
		SetState(EMT2MobAIState::Attacking);
		Mob->TryAttackTarget(TargetActor);
		return;
	}

	if (bCannotMove)
	{
		Controller->StopMovement();
		Mob->GetCharacterMovement()->StopMovementImmediately();
		SetState(EMT2MobAIState::Idle);
		return;
	}

	if (FVector::DistSquared2D(Mob->GetActorLocation(), HomeLocation) >
		FMath::Square(LeashRadius * Settings->ReturnHomeLeashFraction))
	{
		bHasWanderTarget = false;
		ReturnHome(Mob);
		return;
	}

	if (bHasWanderTarget)
	{
		if (MoveTowards(Mob, WanderTarget, Settings->WanderAcceptanceRadius))
		{
			bHasWanderTarget = false;
			// Start the idle pause now that the mob has actually arrived, not back when it started
			// walking - otherwise the wait is spent mid-travel and it immediately picks a new point.
			NextDecisionTime = Now + FMath::FRandRange(
				Settings->WanderIdleMinimumTime,
				Settings->WanderIdleMaximumTime);
			SetState(EMT2MobAIState::Idle);
		}
	}
	else if (Now >= NextDecisionTime)
	{
		Wander(Mob);
	}
}

void UMT2MobAIComponent::NotifyDeath(AAIController* Controller)
{
	TargetActor = nullptr;
	bHasWanderTarget = false;
	SetState(EMT2MobAIState::Dead);
	if (AMT2Mob* Mob = Controller ? Cast<AMT2Mob>(Controller->GetPawn()) : nullptr)
	{
		Mob->GetCharacterMovement()->StopMovementImmediately();
	}
}

void UMT2MobAIComponent::SetTargetActor(AActor* NewTarget)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		AActor* ValidTarget = IsValidTarget(NewTarget) ? NewTarget : nullptr;
		if (TargetActor != ValidTarget)
		{
			TargetActor = ValidTarget;
			GetOwner()->ForceNetUpdate();
		}
	}
}

void UMT2MobAIComponent::SetState(EMT2MobAIState NewState)
{
	if (State == NewState)
	{
		return;
	}
	const EMT2MobAIState OldState = State;
	State = NewState;
	ApplyTickPolicy();
	OnStateChanged.Broadcast(OldState, State);
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		GetOwner()->ForceNetUpdate();
	}
}

void UMT2MobAIComponent::OnRep_State(EMT2MobAIState OldState)
{
	ApplyTickPolicy();
	OnStateChanged.Broadcast(OldState, State);
}

AActor* UMT2MobAIComponent::FindTarget() const
{
	FRAMEPRO_NAMED_SCOPE("MT2.MobAI.FindTarget");
	TRACE_CPUPROFILER_EVENT_SCOPE(MT2_MobAI_FindTarget);
	const AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return nullptr;
	}

	if (const UMT2PlayerSpatialGridSubsystem* Grid =
		World->GetSubsystem<UMT2PlayerSpatialGridSubsystem>())
	{
		if (APawn* Player = Grid->FindClosestPlayerPawn(Owner->GetActorLocation(), AggressiveDetectionRadius))
		{
			if (IsValidTarget(Player)) return Player;
		}
	}

	if (!HasAIFlag(AIFlags, EMT2MobAIFlag::AttackMobs)) return nullptr;

	FCollisionObjectQueryParams ObjectQuery;
	// Mobs use the "Mob" collision profile/object type, not Pawn, so an EMT2MobAIFlag::AttackMobs
	// mob needs this to see other mobs as candidates too.
	ObjectQuery.AddObjectTypesToQuery(ECC_GameTraceChannel1);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MT2MobTargetSearch), false, Owner);
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(
		Overlaps, Owner->GetActorLocation(), FQuat::Identity, ObjectQuery,
		FCollisionShape::MakeSphere(AggressiveDetectionRadius), QueryParams);

	AActor* BestTarget = nullptr;
	float BestDistanceSquared = TNumericLimits<float>::Max();
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Candidate = Overlap.GetActor();
		if (!IsValidTarget(Candidate))
		{
			continue;
		}
		const float DistanceSquared = FVector::DistSquared2D(Owner->GetActorLocation(), Candidate->GetActorLocation());
		if (DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			BestTarget = Candidate;
		}
	}
	return BestTarget;
}

bool UMT2MobAIComponent::IsValidTarget(const AActor* Candidate) const
{
	if (!Candidate || Candidate == GetOwner() || Candidate->IsActorBeingDestroyed())
	{
		return false;
	}

	if (const AMT2PlayerCharacter* Player = Cast<AMT2PlayerCharacter>(Candidate))
	{
		const AMT2PlayerState* PlayerState = Player->GetPlayerState<AMT2PlayerState>();
		if (PlayerState)
		{
			const int32 PlayerEmpire = static_cast<int32>(PlayerState->GetEmpire());
			if ((PlayerEmpire == 1 && HasAIFlag(AIFlags, EMT2MobAIFlag::NoAttackShinsoo)) ||
				(PlayerEmpire == 2 && HasAIFlag(AIFlags, EMT2MobAIFlag::NoAttackChunjo)) ||
				(PlayerEmpire == 3 && HasAIFlag(AIFlags, EMT2MobAIFlag::NoAttackJinno)))
			{
				return false;
			}
		}
		return !Player->GetHealthComponent()->IsDead();
	}

	if (HasAIFlag(AIFlags, EMT2MobAIFlag::AttackMobs))
	{
		const AMT2Mob* OtherMob = Cast<AMT2Mob>(Candidate);
		return OtherMob && OtherMob->GetEmpire() != Empire && !OtherMob->GetHealthComponent()->IsDead();
	}
	return false;
}

void UMT2MobAIComponent::Wander(AMT2Mob* Mob)
{
	const UMT2MobRuntimeSettings* Settings = GetDefault<UMT2MobRuntimeSettings>();
	// No navmesh reachability query - just pick a random point around home and walk straight at it.
	const float Angle = FMath::FRandRange(0.0f, 2.0f * UE_PI);
	const float Radius = FMath::FRandRange(
		Settings->WanderMinimumRadius,
		Settings->WanderMaximumRadius);
	WanderTarget = HomeLocation + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * Radius;
	bHasWanderTarget = true;

	Mob->SetWalkRequested(true);
	SetState(EMT2MobAIState::Wandering);
	MoveTowards(Mob, WanderTarget, Settings->WanderAcceptanceRadius);
}

void UMT2MobAIComponent::ReturnHome(AMT2Mob* Mob)
{
	const UMT2MobRuntimeSettings* Settings = GetDefault<UMT2MobRuntimeSettings>();
	Mob->SetWalkRequested(false);
	SetState(EMT2MobAIState::ReturningHome);
	MoveTowards(Mob, HomeLocation, Settings->ReturnHomeAcceptanceRadius);
}

void UMT2MobAIComponent::FleeFromTarget(AMT2Mob* Mob)
{
	if (!TargetActor || HasAIFlag(AIFlags, EMT2MobAIFlag::NoMove))
	{
		return;
	}
	const UMT2MobRuntimeSettings* Settings = GetDefault<UMT2MobRuntimeSettings>();
	const FVector Away = (Mob->GetActorLocation() - TargetActor->GetActorLocation()).GetSafeNormal2D();
	SetState(EMT2MobAIState::Fleeing);
	MoveTowards(
		Mob,
		Mob->GetActorLocation() + Away * Settings->FleeDistance,
		Settings->WanderAcceptanceRadius);
}

bool UMT2MobAIComponent::MoveTowards(AMT2Mob* Mob, const FVector& Destination, float AcceptanceRadius) const
{
	FRAMEPRO_NAMED_SCOPE("MT2.MobAI.MoveTowards");
	const FVector ToTarget2D = FVector(Destination - Mob->GetActorLocation()) * FVector(1.0f, 1.0f, 0.0f);
	if (ToTarget2D.SizeSquared() <= FMath::Square(AcceptanceRadius))
	{
		return true;
	}

	// Straight-line steering only; CharacterMovementComponent's own capsule sweep against the
	// world is the "simple collision check" - no pathfinding around obstacles.
	Mob->AddMovementInput(ToTarget2D.GetSafeNormal(), 1.0f);
	return false;
}
