/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "MT2MobRuntimeSettings.generated.h"

/**
 * Central tuning for mob simulation, movement, networking and presentation.
 * Available under Project Settings > Game > Mob Runtime.
 */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Mob Runtime"))
class METIN2_API UMT2MobRuntimeSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	UPROPERTY(Config, EditAnywhere, Category="Legacy Scheduling", meta=(ClampMin="1", ClampMax="100"))
	int32 LegacyPulseRate = 25;
	UPROPERTY(Config, EditAnywhere, Category="Legacy Scheduling", meta=(ClampMin="1"))
	int32 LegacyMovePulses = 4;
	UPROPERTY(Config, EditAnywhere, Category="Legacy Scheduling", meta=(ClampMin="1"))
	int32 LegacyAggressiveIdleMinSeconds = 1;
	UPROPERTY(Config, EditAnywhere, Category="Legacy Scheduling", meta=(ClampMin="1"))
	int32 LegacyAggressiveIdleMaxSeconds = 3;
	UPROPERTY(Config, EditAnywhere, Category="Legacy Scheduling", meta=(ClampMin="1"))
	int32 LegacyPassiveIdleMinSeconds = 3;
	UPROPERTY(Config, EditAnywhere, Category="Legacy Scheduling", meta=(ClampMin="1"))
	int32 LegacyPassiveIdleMaxSeconds = 5;
	UPROPERTY(Config, EditAnywhere, Category="Legacy Scheduling", meta=(ClampMin="1"))
	int32 LegacyPostMoveIdleMinSeconds = 1;
	UPROPERTY(Config, EditAnywhere, Category="Legacy Scheduling", meta=(ClampMin="1"))
	int32 LegacyPostMoveIdleMaxSeconds = 3;
	UPROPERTY(Config, EditAnywhere, Category="Legacy Scheduling", meta=(ClampMin="1"))
	int32 LegacyWanderOneIn = 7;
	UPROPERTY(Config, EditAnywhere, Category="Legacy Scheduling", meta=(ClampMin="1"))
	int32 LegacyBossRetargetOneIn = 4;
	UPROPERTY(Config, EditAnywhere, Category="Legacy Scheduling", meta=(ClampMin="0", Units="cm"))
	float LegacyWanderMinimumDistance = 300.f;
	UPROPERTY(Config, EditAnywhere, Category="Legacy Scheduling", meta=(ClampMin="0", Units="cm"))
	float LegacyWanderMaximumDistance = 700.f;

	// UE fallback polling, not a legacy packet rate. Changed gameplay state forces an immediate update.
	UPROPERTY(Config, EditAnywhere, Category="Replication", meta=(ClampMin="0.1", Units="Hz"))
	float BaselineMobReplicationRate = 1.f;
	UPROPERTY(Config, EditAnywhere, Category="Replication", meta=(ClampMin="1", Units="Hz"))
	float KnockbackReplicationRate = 15.f;
	UPROPERTY(Config, EditAnywhere, Category="Replication", meta=(ClampMin="100", Units="cm"))
	float ReplicationGridCellSize = 6400.f;

	float GetMovementInterval() const
	{
		return float(FMath::Max(LegacyMovePulses, 1)) / FMath::Clamp(LegacyPulseRate, 1, 100);
	}

	// Simulation and replication
	UPROPERTY(Config, EditAnywhere, Category="Simulation", meta=(ClampMin="1.0"))
	float SchedulerRate = 30.0f;

	UPROPERTY(Config, EditAnywhere, Category="Simulation", meta=(ClampMin="0.0", Units="cm"))
	float MovementRetargetDistance = 50.f;

	UPROPERTY(Config, EditAnywhere, Category="Simulation", meta=(ClampMin="0.0", Units="cm"))
	float ActiveSimulationDistance = 12000.0f;

	UPROPERTY(Config, EditAnywhere, Category="Simulation", meta=(ClampMin="0.1", Units="Hz"))
	// Retry cadence for active-region entries outside the exact simulation distance.
	float DistantSimulationRate = 1.0f;

	UPROPERTY(Config, EditAnywhere, Category="Simulation", meta=(ClampMin="0.1", Units="Hz"))
	float IdleReplicationRate = 2.0f;

	UPROPERTY(Config, EditAnywhere, Category="Simulation", meta=(ClampMin="0.1", Units="Hz"))
	float MinimumReplicationRate = 1.0f;

	UPROPERTY(Config, EditAnywhere, Category="Simulation", meta=(ClampMin="0.1", Units="Hz"))
	float InitialReplicationRate = 15.0f;

	UPROPERTY(Config, EditAnywhere, Category="Simulation", meta=(ClampMin="0.0", Units="cm"))
	// Legacy VIEW_RANGE (5000) + VIEW_BONUS_RANGE (500), in the imported map's centimetres.
	float NetCullDistance = 5500.0f;

	UPROPERTY(Config, EditAnywhere, Category="Schedulers", meta=(ClampMin="0.01", Units="s"))
	float RegenerationInterval = 5.0f;

	UPROPERTY(Config, EditAnywhere, Category="Schedulers", meta=(ClampMin="0.01", Units="s"))
	float SpawnerInterval = 5.0f;

	UPROPERTY(Config, EditAnywhere, Category="Schedulers", meta=(ClampMin="0.01", Units="s"))
	float MetinStoneInterval = 5.0f;

	UPROPERTY(Config, EditAnywhere, Category="Schedulers", meta=(ClampMin="0.01", Units="s"))
	float NameplateInterval = 0.5f;

	UPROPERTY(Config, EditAnywhere, Category="Schedulers", meta=(ClampMin="0.05", Units="s"))
	float MobNameplateOcclusionInterval = 1.0f;

	// Snapshot interpolation. Clients deliberately render one snapshot behind.
	UPROPERTY(Config, EditAnywhere, Category="Network Smoothing", meta=(ClampMin="0.05", Units="s"))
	float MaximumSnapshotInterpolationTime = 1.5f;

	UPROPERTY(Config, EditAnywhere, Category="Network Smoothing", meta=(ClampMin="0.0", Units="cm"))
	float MaximumSmoothCorrectionDistance = 3000.0f;

	UPROPERTY(Config, EditAnywhere, Category="Network Smoothing", meta=(ClampMin="0.0", Units="cm"))
	float NoSmoothCorrectionDistance = 6000.0f;

	// Cheap authoritative movement
	UPROPERTY(Config, EditAnywhere, Category="Movement", meta=(ClampMin="0.0", Units="cm"))
	float GroundProbeMinimumUp = 45.0f;

	UPROPERTY(Config, EditAnywhere, Category="Movement", meta=(ClampMin="0.0", Units="cm"))
	float GroundProbeExtraUp = 10.0f;

	UPROPERTY(Config, EditAnywhere, Category="Movement", meta=(ClampMin="0.0"))
	float GroundProbeDownStepMultiplier = 2.0f;

	UPROPERTY(Config, EditAnywhere, Category="Movement", meta=(ClampMin="0.0", Units="cm"))
	float GroundProbeMinimumDown = 100.0f;

	UPROPERTY(Config, EditAnywhere, Category="Movement", meta=(ClampMin="0.0", Units="cm"))
	float GroundClearance = 1.0f;

	UPROPERTY(Config, EditAnywhere, Category="Movement", meta=(ClampMin="0.0", Units="deg/s"))
	float RotationRate = 540.0f;

	UPROPERTY(Config, EditAnywhere, Category="Movement", meta=(ClampMin="0.0"))
	float AttackFacingInterpolationSpeed = 12.0f;

	UPROPERTY(Config, EditAnywhere, Category="Movement|Collision", meta=(ClampMin="0.0", Units="cm"))
	float SmallCapsuleRadius = 34.0f;

	UPROPERTY(Config, EditAnywhere, Category="Movement|Collision", meta=(ClampMin="0.0", Units="cm"))
	float SmallCapsuleHalfHeight = 70.0f;

	UPROPERTY(Config, EditAnywhere, Category="Movement|Collision", meta=(ClampMin="0.0", Units="cm"))
	float MediumCapsuleRadius = 42.0f;

	UPROPERTY(Config, EditAnywhere, Category="Movement|Collision", meta=(ClampMin="0.0", Units="cm"))
	float MediumCapsuleHalfHeight = 96.0f;

	UPROPERTY(Config, EditAnywhere, Category="Movement|Collision", meta=(ClampMin="0.0", Units="cm"))
	float LargeCapsuleRadius = 85.0f;

	UPROPERTY(Config, EditAnywhere, Category="Movement|Collision", meta=(ClampMin="0.0", Units="cm"))
	float LargeCapsuleHalfHeight = 160.0f;

	// Fits each mob capsule to its imported skeletal-mesh bounds. The Small/Medium/Large values
	// above remain the fallback when a Blueprint has no valid skeletal mesh.
	UPROPERTY(Config, EditAnywhere, Category="Movement|Collision")
	bool bFitCapsuleToSkeletalMesh = true;

	UPROPERTY(Config, EditAnywhere, Category="Movement|Collision", meta=(ClampMin="0.01"))
	float MeshCapsuleRadiusScale = 1.0f;

	UPROPERTY(Config, EditAnywhere, Category="Movement|Collision", meta=(ClampMin="0.01"))
	float MeshCapsuleHeightScale = 1.0f;

	UPROPERTY(Config, EditAnywhere, Category="Movement|Collision", meta=(ClampMin="0.0", Units="cm"))
	float MeshCapsuleRadiusPadding = 2.0f;

	UPROPERTY(Config, EditAnywhere, Category="Movement|Collision", meta=(ClampMin="0.0", Units="cm"))
	float MeshCapsuleHalfHeightPadding = 2.0f;

	UPROPERTY(Config, EditAnywhere, Category="Movement|Collision", meta=(ClampMin="1.0", Units="cm"))
	float MinimumMeshCapsuleRadius = 10.0f;

	UPROPERTY(Config, EditAnywhere, Category="Movement|Collision", meta=(ClampMin="1.0", Units="cm"))
	float MinimumMeshCapsuleHalfHeight = 20.0f;

	// AI
	/** Radius used when an aggressive mob searches the player grid for a new target. */
	UPROPERTY(Config, EditAnywhere, Category="AI", meta=(ClampMin="0.0", Units="cm"))
	float AggressiveDetectionRadius = 3000.0f;

	/** Multiplies the mob's imported sight radius to calculate its chase leash from its spawn point. */
	UPROPERTY(Config, EditAnywhere, Category="AI", meta=(ClampMin="0.0"))
	float LeashRadiusMultiplier = 2.5f;

	/** Lower bound for the chase leash. Final leash = max(imported sight radius * multiplier, this value). */
	UPROPERTY(Config, EditAnywhere, Category="AI", meta=(ClampMin="0.0", Units="cm"))
	float MinimumLeashRadius = 3000.0f;

	/** Fraction of the chase leash at which an idle mob is forced to return home. Example: 0.35 = 35%. */
	UPROPERTY(Config, EditAnywhere, Category="AI", meta=(ClampMin="0.0"))
	float ReturnHomeLeashFraction = 0.35f;

	/** Multiplies attack range for chase movement acceptance. Lower values move the mob closer before stopping. */
	UPROPERTY(Config, EditAnywhere, Category="AI", meta=(ClampMin="0.0"))
	float ChaseAcceptanceRangeFraction = 0.85f;

	/** Minimum allowed imported sight radius. This affects leash calculation, not automatic target detection. */
	UPROPERTY(Config, EditAnywhere, Category="AI", meta=(ClampMin="0.0", Units="cm"))
	float MinimumSightRadius = 100.0f;

	/** Minimum allowed imported attack range, preventing mobs from requiring zero-distance contact. */
	UPROPERTY(Config, EditAnywhere, Category="AI", meta=(ClampMin="0.0", Units="cm"))
	float MinimumAttackRange = 50.0f;

	/** Distance from a wander/flee destination considered close enough to stop moving. */
	UPROPERTY(Config, EditAnywhere, Category="AI", meta=(ClampMin="0.0", Units="cm"))
	float WanderAcceptanceRadius = 30.0f;

	/** Distance from the exact spawn point considered close enough to finish returning home. */
	UPROPERTY(Config, EditAnywhere, Category="AI", meta=(ClampMin="0.0", Units="cm"))
	float ReturnHomeAcceptanceRadius = 50.0f;

	/** Distance a coward mob tries to move directly away from its current target per flee destination. */
	UPROPERTY(Config, EditAnywhere, Category="AI", meta=(ClampMin="0.0", Units="cm"))
	float FleeDistance = 700.0f;

	// Spawn placement
	UPROPERTY(Config, EditAnywhere, Category="Spawning", meta=(ClampMin="1"))
	int32 PlacementRingCount = 8;

	UPROPERTY(Config, EditAnywhere, Category="Spawning", meta=(ClampMin="1"))
	int32 PlacementStepsPerRing = 12;

	UPROPERTY(Config, EditAnywhere, Category="Spawning", meta=(ClampMin="0.0", Units="cm"))
	float PlacementRingSpacing = 150.0f;

	UPROPERTY(Config, EditAnywhere, Category="Spawning", meta=(ClampMin="0.0", Units="deg"))
	float PlacementRingAngleOffset = 15.0f;

	UPROPERTY(Config, EditAnywhere, Category="Spawning", meta=(ClampMin="0.0", Units="cm"))
	float PlacementGroundTraceUp = 500.0f;

	UPROPERTY(Config, EditAnywhere, Category="Spawning", meta=(ClampMin="0.0", Units="cm"))
	float PlacementGroundTraceDown = 2000.0f;

	UPROPERTY(Config, EditAnywhere, Category="Spawning", meta=(ClampMin="0.0", Units="cm"))
	float PlacementGroundClearance = 2.0f;

	UPROPERTY(Config, EditAnywhere, Category="Spawning", meta=(ClampMin="0.0", Units="cm"))
	float MapGroundTraceDistance = 100000.0f;

	UPROPERTY(Config, EditAnywhere, Category="Spawning", meta=(ClampMin="0.0", Units="cm"))
	float MapGroundClearance = 5.0f;

	UPROPERTY(Config, EditAnywhere, Category="Spawning", meta=(ClampMin="0.0", Units="cm"))
	float GroupMemberMinimumDistance = 300.0f;

	UPROPERTY(Config, EditAnywhere, Category="Spawning", meta=(ClampMin="0.0", Units="cm"))
	float GroupMemberMaximumDistance = 500.0f;

	UPROPERTY(Config, EditAnywhere, Category="Spawning", meta=(ClampMin="1"))
	int32 GroundLocationAttempts = 16;

	UPROPERTY(Config, EditAnywhere, Category="Loot", meta=(ClampMin="0.0", Units="cm"))
	float RewardRange = 5000.0f;

	// Animation and combat presentation
	UPROPERTY(Config, EditAnywhere, Category="Animation", meta=(ClampMin="0.0", Units="s"))
	float MontageBlendTime = 0.2f;

	UPROPERTY(Config, EditAnywhere, Category="Animation", meta=(ClampMin="0.0", Units="s"))
	float AttackBlendOverlap = 0.2f;

	UPROPERTY(Config, EditAnywhere, Category="Animation", meta=(ClampMin="0.0", Units="s"))
	float DeathFreezeLeadTime = 0.1f;

	UPROPERTY(Config, EditAnywhere, Category="Animation", meta=(ClampMin="0.0", Units="s"))
	float DeathPoseFrameOffset = 0.033f;

	UPROPERTY(Config, EditAnywhere, Category="Animation", meta=(ClampMin="0.001", Units="s"))
	float MinimumTimerDuration = 0.01f;

	UPROPERTY(Config, EditAnywhere, Category="Animation", meta=(ClampMin="0.001", Units="s"))
	float MinimumCombatLockDuration = 0.05f;

	// Centralized client animation significance. No mob owns a distance timer.
	UPROPERTY(Config, EditAnywhere, Category="Animation|Distance", meta=(ClampMin="0.01", Units="s"))
	float AnimationSchedulerInterval = 0.25f;

	UPROPERTY(Config, EditAnywhere, Category="Animation|Distance", meta=(ClampMin="0.0", Units="cm"))
	float FullAnimationDistance = 5000.0f;

	UPROPERTY(Config, EditAnywhere, Category="Animation|Distance", meta=(ClampMin="0.0", Units="cm"))
	float ReducedAnimationDistance = 7500.0f;

	UPROPERTY(Config, EditAnywhere, Category="Animation|Distance", meta=(ClampMin="0.0", Units="cm"))
	float FarAnimationDistance = 10000.0f;

	UPROPERTY(Config, EditAnywhere, Category="Animation|Distance", meta=(ClampMin="0.1", Units="Hz"))
	float ReducedAnimationRate = 15.0f;

	UPROPERTY(Config, EditAnywhere, Category="Animation|Distance", meta=(ClampMin="0.1", Units="Hz"))
	float FarAnimationRate = 5.0f;
};
