/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Mobs/MT2MobMovementComponent.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "FramePro/FramePro.h"
#include "GameFramework/Character.h"
#include "Mobs/MT2MobRuntimeSettings.h"
#include "Mobs/MT2Mob.h"
#include "Mobs/MT2MobAIComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"

UMT2MobMovementComponent::UMT2MobMovementComponent()
{
	bEnablePhysicsInteraction = false;
	bUseRVOAvoidance = false;
	bAlwaysCheckFloor = false;
	bUseFlatBaseForFloorChecks = true;
	bCanWalkOffLedges = false;
	bEnableScopedMovementUpdates = true;
	NetworkSmoothingMode = ENetworkSmoothingMode::Linear;
	SetIsReplicatedByDefault(true);
	const UMT2MobRuntimeSettings* Settings = GetDefault<UMT2MobRuntimeSettings>();
	NetworkMaxSmoothUpdateDistance = Settings->MaximumSmoothCorrectionDistance;
	NetworkNoSmoothUpdateDistance = Settings->NoSmoothCorrectionDistance;
}

void UMT2MobMovementComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMT2MobMovementComponent, MoveSegment);
}

double UMT2MobMovementComponent::GetMovementServerTime() const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* State = World ? World->GetGameState() : nullptr;
	return State ? State->GetServerWorldTimeSeconds() : World ? World->GetTimeSeconds() : 0.;
}

void UMT2MobMovementComponent::StartMoveSegment(const FVector& Destination, float AcceptanceRadius)
{
	if (!CharacterOwner || !CharacterOwner->HasAuthority() || !HasValidData() || HasRootMotionSources() ||
		Destination.ContainsNaN() || GetMaxSpeed() <= UE_SMALL_NUMBER) { return; }
	const float RetargetDistance = GetDefault<UMT2MobRuntimeSettings>()->MovementRetargetDistance;
	if (MoveSegment.bMoving && FVector::DistSquared2D(RequestedDestination, Destination) <= FMath::Square(RetargetDistance) &&
		FMath::IsNearlyEqual(SegmentSpeed, GetMaxSpeed()) && FMath::IsNearlyEqual(RequestedAcceptanceRadius, AcceptanceRadius)) { return; }
	const FVector Start = UpdatedComponent->GetComponentLocation();
	const FVector Delta = (Destination - Start) * FVector(1,1,0);
	const double Length = Delta.Size2D();
	if (Length <= AcceptanceRadius) { StopMovementImmediately(); return; }
	// Stop at the requested attack/wander acceptance distance, rather than entering the target's body.
	const FVector End = Start + Delta.GetSafeNormal2D() * (Length - FMath::Max(AcceptanceRadius, 0.f));
	RequestedDestination = Destination; RequestedAcceptanceRadius = AcceptanceRadius; SegmentSpeed = GetMaxSpeed();
	MoveSegment.Start = Start; MoveSegment.Destination = End;
	MoveSegment.ServerStartTime = GetMovementServerTime();
	MoveSegment.Duration = FVector::Dist2D(Start, End) / GetMaxSpeed();
	MoveSegment.bMoving = true; MoveSegment.bExternalMotion = false; ++MoveSegment.Serial;
	CharacterOwner->SetReplicateMovement(false);
	SetComponentTickInterval(1.f / FMath::Max(GetDefault<UMT2MobRuntimeSettings>()->MovementSimulationRate, 1.f));
	SetComponentTickEnabled(true);
	CharacterOwner->SetActorRotation(Delta.Rotation());
	CharacterOwner->ForceNetUpdate();
}

void UMT2MobMovementComponent::PublishStoppedSegment()
{
	if (!CharacterOwner || !CharacterOwner->HasAuthority()) { return; }
	MoveSegment.Start = MoveSegment.Destination = CharacterOwner->GetActorLocation();
	MoveSegment.ServerStartTime = GetMovementServerTime(); MoveSegment.Duration = 0.f;
	MoveSegment.bMoving = false; MoveSegment.bExternalMotion = false; ++MoveSegment.Serial;
	CharacterOwner->SetReplicateMovement(false);
	CharacterOwner->ForceNetUpdate();
}

void UMT2MobMovementComponent::StopMovementImmediately()
{
	Super::StopMovementImmediately();
	if (MoveSegment.bMoving && CharacterOwner && CharacterOwner->HasAuthority())
	{
		PublishStoppedSegment();
		if (!HasRootMotionSources()) { SetComponentTickEnabled(false); }
	}
}

void UMT2MobMovementComponent::BeginExternalKnockback()
{
	if (!CharacterOwner) { return; }
	if (!CharacterOwner->HasAuthority()) { bClientExternalMotion = true; }
	if (CharacterOwner->HasAuthority())
	{
		PublishStoppedSegment();
		MoveSegment.bExternalMotion = true; ++MoveSegment.Serial;
		CharacterOwner->SetReplicateMovement(true);
		CharacterOwner->ForceNetUpdate();
	}
	SetComponentTickInterval(0.f); SetComponentTickEnabled(true);
}

void UMT2MobMovementComponent::OnRep_MoveSegment()
{
	if (!CharacterOwner || CharacterOwner->HasAuthority()) { return; }
	if (bClientExternalMotion && !MoveSegment.bExternalMotion)
	{
		ResetPredictionData_Client();
		if (USkeletalMeshComponent* Mesh = CharacterOwner->GetMesh())
		{
			Mesh->SetRelativeLocationAndRotation(CharacterOwner->GetBaseTranslationOffset(), CharacterOwner->GetBaseRotationOffset());
		}
	}
	bClientExternalMotion = MoveSegment.bExternalMotion;
	SetComponentTickInterval(0.f);
	SetComponentTickEnabled(MoveSegment.bMoving || MoveSegment.bExternalMotion);
	if (!MoveSegment.bMoving && !MoveSegment.bExternalMotion)
	{
		Super::StopMovementImmediately();
		CharacterOwner->SetActorLocation(MoveSegment.Destination, false);
	}
}

void UMT2MobMovementComponent::FinishExternalKnockback()
{
	Super::StopMovementImmediately();
	PublishStoppedSegment();
}

void UMT2MobMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	if (!HasValidData() || !CharacterOwner) { return; }
	if (HasRootMotionSources() || (!CharacterOwner->HasAuthority() && (MoveSegment.bExternalMotion || bClientExternalMotion)))
	{
		Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
		if (CharacterOwner->HasAuthority() && MoveSegment.bExternalMotion && !HasRootMotionSources())
		{
			FinishExternalKnockback();
			SetComponentTickInterval(1.f / FMath::Max(GetDefault<UMT2MobRuntimeSettings>()->MovementSimulationRate, 1.f));
			if (auto* Mob = Cast<AMT2Mob>(CharacterOwner)) { Mob->GetMobAIComponent()->ApplyTickPolicy(); }
		}
		return;
	}
	if (CharacterOwner->HasAuthority() && MoveSegment.bExternalMotion)
	{
		PublishStoppedSegment();
		SetComponentTickInterval(1.f / FMath::Max(GetDefault<UMT2MobRuntimeSettings>()->MovementSimulationRate, 1.f));
		if (auto* Mob = Cast<AMT2Mob>(CharacterOwner)) { Mob->GetMobAIComponent()->ApplyTickPolicy(); }
	}
	if (!MoveSegment.bMoving) { return; }
	// Normal NPC locomotion does not need the full CharacterMovement tick/prediction pipeline.
	PhysWalking(DeltaTime, 0);
}

void UMT2MobMovementComponent::PhysWalking(float DeltaSeconds, int32 Iterations)
{
	FRAMEPRO_NAMED_SCOPE("MT2.MobMovement.PhysWalking");
	TRACE_CPUPROFILER_EVENT_SCOPE(MT2_MobMovement_PhysWalking);
	if (!HasValidData() || DeltaSeconds < MIN_TICK_TIME)
	{
		return;
	}

	if (HasRootMotionSources())
	{
		bHadKnockbackMovement = true;
		// Use the full swept walking solver during a shove so AI steering cannot replace its velocity.
		Super::PhysWalking(DeltaSeconds, Iterations);
		return;
	}
	if (bHadKnockbackMovement)
	{
		bHadKnockbackMovement = false;
		if (auto* Mob = Cast<AMT2Mob>(CharacterOwner)) { Mob->GetMobAIComponent()->ApplyTickPolicy(); }
		if (!IsComponentTickEnabled()) { return; }
	}
	// Mobs only need direct planar steering. Avoid CharacterMovement's floor cache, step solver,
	// based movement and repeated floor sweeps, while retaining a swept capsule against obstacles.
	// Server steps sample the timed segment; clients independently render its continuous trajectory.
	const FVector Start = UpdatedComponent->GetComponentLocation();
	if (!MoveSegment.bMoving || MoveSegment.Duration <= 0.f) { return; }
	const float Alpha = FMath::Clamp(float((GetMovementServerTime() - MoveSegment.ServerStartTime) / MoveSegment.Duration), 0.f, 1.f);
	FVector Target = FMath::Lerp(FVector(MoveSegment.Start), FVector(MoveSegment.Destination), Alpha);

	const UCapsuleComponent* Capsule = CharacterOwner ? CharacterOwner->GetCapsuleComponent() : nullptr;
	const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.0f;
	const UMT2MobRuntimeSettings* Settings = GetDefault<UMT2MobRuntimeSettings>();
	const float GroundProbeUp =
		FMath::Max(MaxStepHeight, Settings->GroundProbeMinimumUp) + Settings->GroundProbeExtraUp;
	const float GroundProbeDown = FMath::Max(
		MaxStepHeight * Settings->GroundProbeDownStepMultiplier,
		Settings->GroundProbeMinimumDown);

	FHitResult GroundHit;
	FCollisionQueryParams QueryParams(
		SCENE_QUERY_STAT(MT2MobGroundProbe), false, CharacterOwner.Get());
	const FVector ProbeTop(Target.X, Target.Y, Start.Z + GroundProbeUp);
	const FVector ProbeBottom(Target.X, Target.Y, Start.Z - HalfHeight - GroundProbeDown);
	if (!GetWorld()->LineTraceSingleByObjectType(
		GroundHit, ProbeTop, ProbeBottom,
		FCollisionObjectQueryParams(ECC_WorldStatic), QueryParams))
	{
		StopMovementImmediately();
		return;
	}

	Target.Z = GroundHit.ImpactPoint.Z + HalfHeight + Settings->GroundClearance;
	FVector Delta = Target - Start;
	FHitResult MoveHit;
	const bool bAuthority = CharacterOwner->HasAuthority();
	const FQuat Facing = (FVector(MoveSegment.Destination) - FVector(MoveSegment.Start)).Rotation().Quaternion();
	SafeMoveUpdatedComponent(Delta, Facing, bAuthority, MoveHit);
	if (MoveHit.IsValidBlockingHit())
	{
		Delta.Z = 0.0f;
		SlideAlongSurface(Delta, 1.0f - MoveHit.Time, MoveHit.Normal, MoveHit, true);
	}

	const FVector ActualDelta = UpdatedComponent->GetComponentLocation() - Start;
	Velocity = FVector(ActualDelta.X, ActualDelta.Y, 0.0f) / DeltaSeconds;
	UpdateComponentVelocity();
	if (bAuthority && (Alpha >= 1.f || MoveHit.IsValidBlockingHit()))
	{
		StopMovementImmediately();
	}
}

void UMT2MobMovementComponent::SimulatedTick(float DeltaSeconds)
{
	FRAMEPRO_NAMED_SCOPE("MT2.MobMovement.SimulatedTick");
	// Keep UE's simulated-proxy prediction and mesh smoothing. The previous snapshot-only path left
	// the capsule parked between updates, producing visible steps at low simulation frequencies.
	Super::SimulatedTick(DeltaSeconds);
}

void UMT2MobMovementComponent::SmoothCorrection(
	const FVector& OldLocation,
	const FQuat& OldRotation,
	const FVector& NewLocation,
	const FQuat& NewRotation)
{
	if (FNetworkPredictionData_Client_Character* ClientData =
		GetPredictionData_Client_Character())
	{
		const UMT2MobRuntimeSettings* Settings = GetDefault<UMT2MobRuntimeSettings>();
		ClientData->MaxClientSmoothingDeltaTime = Settings->MaximumSnapshotInterpolationTime;
		ClientData->MaxSmoothNetUpdateDist = Settings->MaximumSmoothCorrectionDistance;
		ClientData->NoSmoothNetUpdateDist = Settings->NoSmoothCorrectionDistance;
	}

	Super::SmoothCorrection(OldLocation, OldRotation, NewLocation, NewRotation);
}
