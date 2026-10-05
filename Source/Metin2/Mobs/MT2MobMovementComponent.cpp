/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Mobs/MT2MobMovementComponent.h"

#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "FramePro/FramePro.h"
#include "GameFramework/Character.h"
#include "Mobs/MT2MobRuntimeSettings.h"

UMT2MobMovementComponent::UMT2MobMovementComponent()
{
	bEnablePhysicsInteraction = false;
	bUseRVOAvoidance = false;
	bAlwaysCheckFloor = false;
	bUseFlatBaseForFloorChecks = true;
	bCanWalkOffLedges = false;
	bEnableScopedMovementUpdates = true;
	NetworkSmoothingMode = ENetworkSmoothingMode::Linear;
	const UMT2MobRuntimeSettings* Settings = GetDefault<UMT2MobRuntimeSettings>();
	NetworkMaxSmoothUpdateDistance = Settings->MaximumSmoothCorrectionDistance;
	NetworkNoSmoothUpdateDistance = Settings->NoSmoothCorrectionDistance;
}

void UMT2MobMovementComponent::PhysWalking(float DeltaSeconds, int32 Iterations)
{
	FRAMEPRO_NAMED_SCOPE("MT2.MobMovement.PhysWalking");
	TRACE_CPUPROFILER_EVENT_SCOPE(MT2_MobMovement_PhysWalking);
	if (!HasValidData() || DeltaSeconds < MIN_TICK_TIME)
	{
		return;
	}

	// Mobs only need direct planar steering. Avoid CharacterMovement's floor cache, step solver,
	// based movement and repeated floor sweeps, while retaining a swept capsule against obstacles.
	// AI decisions may be distance-throttled, but movement must remain continuous. Preserve the last
	// steering direction between AI updates; StopMovementImmediately clears Velocity on state changes.
	const FVector Direction = !Acceleration.IsNearlyZero()
		? Acceleration.GetSafeNormal2D()
		: Velocity.GetSafeNormal2D();
	Velocity = Direction * GetMaxSpeed();
	if (Velocity.IsNearlyZero())
	{
		StopMovementImmediately();
		return;
	}

	const FVector Start = UpdatedComponent->GetComponentLocation();
	FVector Target = Start + Velocity * DeltaSeconds;

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
	SafeMoveUpdatedComponent(Delta, UpdatedComponent->GetComponentQuat(), true, MoveHit);
	if (MoveHit.IsValidBlockingHit())
	{
		Delta.Z = 0.0f;
		SlideAlongSurface(Delta, 1.0f - MoveHit.Time, MoveHit.Normal, MoveHit, true);
	}

	const FVector ActualDelta = UpdatedComponent->GetComponentLocation() - Start;
	Velocity = FVector(ActualDelta.X, ActualDelta.Y, 0.0f) / DeltaSeconds;
	UpdateComponentVelocity();
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
