/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "World/MT2WorldSimulationSubsystem.h"

#include "AIController.h"
#include "CollisionQueryParams.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "FramePro/FramePro.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Mobs/MT2MetinStone.h"
#include "Mobs/MT2Mob.h"
#include "Mobs/MT2MobAIComponent.h"
#include "Mobs/MT2MobLifecycleComponent.h"
#include "Mobs/MT2MobRuntimeSettings.h"
#include "Mobs/MT2MobSpawnComponent.h"
#include "UI/MT2NameplateComponent.h"
#include "World/MT2PlayerSpatialGridSubsystem.h"

namespace
{
	double GetMobSimulationInterval(
		float DistanceSquared,
		const UMT2MobRuntimeSettings& Settings)
	{
		if (DistanceSquared <= FMath::Square(Settings.NearSimulationDistance))
		{
			return 1.0 / FMath::Max(Settings.NearSimulationRate, 0.1f);
		}
		if (DistanceSquared <= FMath::Square(Settings.MediumSimulationDistance))
		{
			return 1.0 / FMath::Max(Settings.MediumSimulationRate, 0.1f);
		}
		if (DistanceSquared <= FMath::Square(Settings.FarSimulationDistance))
		{
			return 1.0 / FMath::Max(Settings.FarSimulationRate, 0.1f);
		}
		return 1.0 / FMath::Max(Settings.DistantSimulationRate, 0.1f);
	}
}

template <typename T>
void UMT2WorldSimulationSubsystem::RemoveInvalid(TSet<TWeakObjectPtr<T>>& Objects)
{
	for (auto It = Objects.CreateIterator(); It; ++It)
	{
		if (!It->IsValid()) It.RemoveCurrent();
	}
}

void UMT2WorldSimulationSubsystem::Deinitialize()
{
	Mobs.Reset();
	Nameplates.Reset();
	RegenerationComponents.Reset();
	Spawners.Reset();
	MetinStones.Reset();
	NextMobUpdateTimes.Reset();
	Super::Deinitialize();
}

void UMT2WorldSimulationSubsystem::Tick(float DeltaTime)
{
	FRAMEPRO_NAMED_SCOPE("MT2.WorldSimulation.Tick");
	TRACE_CPUPROFILER_EVENT_SCOPE(MT2_WorldSimulation_Tick);
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld()) return;

	const double Now = World->GetTimeSeconds();
	const UMT2MobRuntimeSettings* Settings = GetDefault<UMT2MobRuntimeSettings>();
	if (World->GetNetMode() != NM_Client) TickServerSimulation(Now);
	if (World->GetNetMode() != NM_DedicatedServer)
	{
		if (Now >= NextMobAnimationUpdateTime)
		{
			NextMobAnimationUpdateTime =
				Now + FMath::Max(Settings->AnimationSchedulerInterval, 0.01f);
			TickClientMobAnimation();
		}
		const bool bRefreshNameplates = Now >= NextNameplateUpdateTime;
		const bool bRefreshMobOcclusion = Now >= NextNameplateOcclusionUpdateTime;
		if (bRefreshNameplates || bRefreshMobOcclusion)
		{
			if (bRefreshNameplates)
			{
				NextNameplateUpdateTime =
					Now + FMath::Max(Settings->NameplateInterval, 0.01f);
			}
			if (bRefreshMobOcclusion)
			{
				NextNameplateOcclusionUpdateTime =
					Now + FMath::Max(Settings->MobNameplateOcclusionInterval, 0.05f);
			}
			TickClientPresentation(bRefreshMobOcclusion);
		}
	}
}

TStatId UMT2WorldSimulationSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMT2WorldSimulationSubsystem, STATGROUP_Tickables);
}

void UMT2WorldSimulationSubsystem::RegisterMob(AMT2Mob* Mob)
{
	if (Mob) Mobs.Add(Mob);
}

void UMT2WorldSimulationSubsystem::UnregisterMob(AMT2Mob* Mob)
{
	Mobs.Remove(Mob);
	NextMobUpdateTimes.Remove(Mob);
}

void UMT2WorldSimulationSubsystem::RegisterNameplate(UMT2NameplateComponent* Nameplate)
{
	if (Nameplate) Nameplates.Add(Nameplate);
}

void UMT2WorldSimulationSubsystem::UnregisterNameplate(UMT2NameplateComponent* Nameplate)
{
	Nameplates.Remove(Nameplate);
}

void UMT2WorldSimulationSubsystem::RegisterRegeneration(UMT2MobLifecycleComponent* Lifecycle)
{
	if (Lifecycle) RegenerationComponents.Add(Lifecycle);
}

void UMT2WorldSimulationSubsystem::UnregisterRegeneration(UMT2MobLifecycleComponent* Lifecycle)
{
	RegenerationComponents.Remove(Lifecycle);
}

void UMT2WorldSimulationSubsystem::RegisterSpawner(UMT2MobSpawnComponent* Spawner)
{
	if (Spawner) Spawners.Add(Spawner);
}

void UMT2WorldSimulationSubsystem::UnregisterSpawner(UMT2MobSpawnComponent* Spawner)
{
	Spawners.Remove(Spawner);
}

void UMT2WorldSimulationSubsystem::RegisterMetinStone(AMT2MetinStone* Stone)
{
	if (Stone) MetinStones.Add(Stone);
}

void UMT2WorldSimulationSubsystem::UnregisterMetinStone(AMT2MetinStone* Stone)
{
	MetinStones.Remove(Stone);
}

void UMT2WorldSimulationSubsystem::ClearMobTargetsFor(const AActor* TargetActor)
{
	if (!TargetActor || !GetWorld() || GetWorld()->GetNetMode() == NM_Client)
	{
		return;
	}
	RemoveInvalid(Mobs);
	for (const TWeakObjectPtr<AMT2Mob>& WeakMob : Mobs)
	{
		AMT2Mob* Mob = WeakMob.Get();
		UMT2MobAIComponent* AI = Mob ? Mob->GetMobAIComponent() : nullptr;
		if (AI && AI->GetTargetActor() == TargetActor)
		{
			AI->SetTargetActor(nullptr);
		}
	}
}

void UMT2WorldSimulationSubsystem::TickServerSimulation(double Now)
{
	FRAMEPRO_NAMED_SCOPE("MT2.WorldSimulation.Server");
	TRACE_CPUPROFILER_EVENT_SCOPE(MT2_WorldSimulation_Server);
	UMT2PlayerSpatialGridSubsystem* Grid = GetWorld()->GetSubsystem<UMT2PlayerSpatialGridSubsystem>();
	const UMT2MobRuntimeSettings* Settings = GetDefault<UMT2MobRuntimeSettings>();
	if (Now >= NextServerUpdateTime)
	{
		FRAMEPRO_NAMED_SCOPE("MT2.WorldSimulation.MobScheduler");
		TRACE_CPUPROFILER_EVENT_SCOPE(MT2_WorldSimulation_MobScheduler);
		NextServerUpdateTime = Now + 1.0 / FMath::Max(Settings->SchedulerRate, 1.0f);
		RemoveInvalid(Mobs);
		for (const TWeakObjectPtr<AMT2Mob>& WeakMob : Mobs)
		{
			FRAMEPRO_NAMED_SCOPE("MT2.WorldSimulation.Mob");
			AMT2Mob* Mob = WeakMob.Get();
			if (!Mob || Mob->IsActorBeingDestroyed()) continue;
			const EMT2MobType MobType = Mob->GetMobType();
			if (MobType == EMT2MobType::NPC || MobType == EMT2MobType::Warp ||
				MobType == EMT2MobType::Goto)
			{
				continue;
			}
			float DistanceSquared = 0.0f;
			{
				FRAMEPRO_NAMED_SCOPE("MT2.WorldSimulation.PlayerDistance");
				TRACE_CPUPROFILER_EVENT_SCOPE(MT2_WorldSimulation_PlayerDistance);
				DistanceSquared = Grid ? Grid->GetClosestPlayerDistanceSquared(Mob->GetActorLocation(), FMath::Sqrt(Mob->GetNetCullDistanceSquared())) : 0.0f;
			}
			const double Interval = GetMobSimulationInterval(DistanceSquared, *Settings);
			double& NextUpdate = NextMobUpdateTimes.FindOrAdd(Mob);
			// Promote a mob immediately when a player enters a closer simulation band. Otherwise an
			// old one-second sleep deadline can survive after the mob becomes nearby.
			if (NextUpdate - Now > Interval) NextUpdate = Now;
			if (Now < NextUpdate) continue;
			NextUpdate = Now + Interval;

			UCharacterMovementComponent* Movement = Mob->GetCharacterMovement();
			const bool bInSimulationRange =
				DistanceSquared <= FMath::Square(Settings->ActiveSimulationDistance);
			if (!bInSimulationRange)
			{
				FRAMEPRO_NAMED_SCOPE("MT2.WorldSimulation.StopDistantMob");
				if (Movement) Movement->StopMovementImmediately();
				Mob->SetNetUpdateFrequency(Settings->MinimumReplicationRate);
				continue;
			}
			if (Movement)
			{
				// AI decisions scale by distance, while physical movement stays continuous. Coupling
				// these intervals made distant mobs advance in visible half/one-second steps.
				Movement->SetComponentTickInterval(
					1.0f / FMath::Max(Settings->MovementSimulationRate, 1.0f));
			}
			if (AAIController* Controller = Cast<AAIController>(Mob->GetController()))
			{
				FRAMEPRO_NAMED_SCOPE("MT2.WorldSimulation.MobAI");
				TRACE_CPUPROFILER_EVENT_SCOPE(MT2_WorldSimulation_MobAI);
				Mob->GetMobAIComponent()->TickBehavior(Controller, static_cast<float>(Interval));
			}

			const EMT2MobAIState State = Mob->GetMobAIComponent()->GetState();
			const bool bMoving = State == EMT2MobAIState::Wandering
				|| State == EMT2MobAIState::Chasing
				|| State == EMT2MobAIState::Fleeing
				|| State == EMT2MobAIState::ReturningHome;
			// Replication frequency follows AI significance. Physical movement remains continuous,
			// and simulated proxies extrapolate the authoritative velocity between snapshots.
			const float DesiredNetFrequency = bMoving
				? static_cast<float>(1.0 / Interval)
				: State == EMT2MobAIState::Attacking
					? Settings->AttackingReplicationRate
					: Settings->IdleReplicationRate;
			if (!FMath::IsNearlyEqual(Mob->GetNetUpdateFrequency(), DesiredNetFrequency))
			{
				Mob->SetNetUpdateFrequency(DesiredNetFrequency);
				Mob->SetMinNetUpdateFrequency(Settings->MinimumReplicationRate);
			}

		}
	}

	if (Now >= NextRegenerationUpdateTime)
	{
		FRAMEPRO_NAMED_SCOPE("MT2.WorldSimulation.Regeneration");
		TRACE_CPUPROFILER_EVENT_SCOPE(MT2_WorldSimulation_Regeneration);
		NextRegenerationUpdateTime = Now + Settings->RegenerationInterval;
		RemoveInvalid(RegenerationComponents);
		for (const TWeakObjectPtr<UMT2MobLifecycleComponent>& Entry : RegenerationComponents)
		{
			if (UMT2MobLifecycleComponent* Lifecycle = Entry.Get()) Lifecycle->ProcessRegeneration(Now);
		}
	}
	if (Now >= NextSpawnerUpdateTime)
	{
		FRAMEPRO_NAMED_SCOPE("MT2.WorldSimulation.Spawners");
		TRACE_CPUPROFILER_EVENT_SCOPE(MT2_WorldSimulation_Spawners);
		NextSpawnerUpdateTime = Now + Settings->SpawnerInterval;
		RemoveInvalid(Spawners);
		for (const TWeakObjectPtr<UMT2MobSpawnComponent>& Entry : Spawners)
		{
			if (UMT2MobSpawnComponent* Spawner = Entry.Get()) Spawner->ProcessSpawns();
		}
	}
	if (Now >= NextStoneUpdateTime)
	{
		FRAMEPRO_NAMED_SCOPE("MT2.WorldSimulation.MetinStones");
		TRACE_CPUPROFILER_EVENT_SCOPE(MT2_WorldSimulation_MetinStones);
		NextStoneUpdateTime = Now + Settings->MetinStoneInterval;
		RemoveInvalid(MetinStones);
		for (const TWeakObjectPtr<AMT2MetinStone>& Entry : MetinStones)
		{
			if (AMT2MetinStone* Stone = Entry.Get()) Stone->ProcessStoneBehavior();
		}
	}
}

void UMT2WorldSimulationSubsystem::TickClientPresentation(bool bRefreshMobOcclusion)
{
	FRAMEPRO_NAMED_SCOPE("MT2.WorldSimulation.Nameplates");
	TRACE_CPUPROFILER_EVENT_SCOPE(MT2_WorldSimulation_Nameplates);
	RemoveInvalid(Nameplates);

	APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	FVector ViewLocation = FVector::ZeroVector;
	bool bHasViewLocation = false;
	if (Controller)
	{
		FRotator ViewRotation;
		Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
		bHasViewLocation = true;
	}

	if (bRefreshMobOcclusion && bHasViewLocation)
	{
		FRAMEPRO_NAMED_SCOPE("MT2.WorldSimulation.NameplateOcclusion");
		TRACE_CPUPROFILER_EVENT_SCOPE(MT2_WorldSimulation_NameplateOcclusion);
		FCollisionQueryParams QueryParams(
			SCENE_QUERY_STAT(MT2MobNameplateOcclusion),
			false);

		// Actors with nameplates, including every mob, must not occlude one another.
		// The trace should react only to terrain and other world geometry.
		for (const TWeakObjectPtr<UMT2NameplateComponent>& Entry : Nameplates)
		{
			if (const UMT2NameplateComponent* Nameplate = Entry.Get())
			{
				QueryParams.AddIgnoredActor(Nameplate->GetOwner());
			}
		}

		for (const TWeakObjectPtr<UMT2NameplateComponent>& Entry : Nameplates)
		{
			if (UMT2NameplateComponent* Nameplate = Entry.Get())
			{
				Nameplate->RefreshMobOcclusionFromSimulationManager(
					ViewLocation,
					QueryParams);
			}
		}
	}

	for (const TWeakObjectPtr<UMT2NameplateComponent>& Entry : Nameplates)
	{
		if (UMT2NameplateComponent* Nameplate = Entry.Get())
		{
			Nameplate->RefreshFromSimulationManager(ViewLocation, bHasViewLocation);
		}
	}
}

void UMT2WorldSimulationSubsystem::TickClientMobAnimation()
{
	FRAMEPRO_NAMED_SCOPE("MT2.WorldSimulation.MobAnimation");
	TRACE_CPUPROFILER_EVENT_SCOPE(MT2_WorldSimulation_MobAnimation);

	UMT2PlayerSpatialGridSubsystem* Grid =
		GetWorld()->GetSubsystem<UMT2PlayerSpatialGridSubsystem>();
	if (!Grid || !Grid->HasTrackedPlayers())
	{
		return;
	}

	RemoveInvalid(Mobs);
	const UMT2MobRuntimeSettings* Settings = GetDefault<UMT2MobRuntimeSettings>();
	const float FullDistance = FMath::Max(Settings->FullAnimationDistance, 0.0f);
	const float ReducedDistance = FMath::Max(
		FullDistance, Settings->ReducedAnimationDistance);
	const float FarDistance = FMath::Max(
		ReducedDistance, Settings->FarAnimationDistance);
	const float FullDistanceSquared = FMath::Square(FullDistance);
	const float ReducedDistanceSquared = FMath::Square(ReducedDistance);
	const float FarDistanceSquared = FMath::Square(FarDistance);
	const float ReducedTickInterval = 1.0f / FMath::Max(Settings->ReducedAnimationRate, 0.1f);
	const float FarTickInterval = 1.0f / FMath::Max(Settings->FarAnimationRate, 0.1f);

	for (const TWeakObjectPtr<AMT2Mob>& WeakMob : Mobs)
	{
		AMT2Mob* Mob = WeakMob.Get();
		USkeletalMeshComponent* Mesh = Mob ? Mob->GetMesh() : nullptr;
		if (!Mob || !Mesh || Mob->IsActorBeingDestroyed())
		{
			continue;
		}

		const float DistanceSquared = Grid->GetClosestPlayerDistanceSquared(
			Mob->GetActorLocation(), FarDistance);
		if (DistanceSquared > FarDistanceSquared)
		{
			Mesh->bNoSkeletonUpdate = true;
			Mesh->SetComponentTickEnabled(false);
			continue;
		}

		Mesh->bNoSkeletonUpdate = false;
		Mesh->SetComponentTickEnabled(true);
		if (DistanceSquared <= FullDistanceSquared)
		{
			Mesh->SetComponentTickInterval(0.0f);
		}
		else if (DistanceSquared <= ReducedDistanceSquared)
		{
			Mesh->SetComponentTickInterval(ReducedTickInterval);
		}
		else
		{
			Mesh->SetComponentTickInterval(FarTickInterval);
		}
	}
}
