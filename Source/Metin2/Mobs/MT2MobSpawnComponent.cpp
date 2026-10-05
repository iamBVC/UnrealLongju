/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Mobs/MT2MobSpawnComponent.h"

#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "FramePro/FramePro.h"
#include "Core/MT2VnumRegistrySubsystem.h"
#include "Mobs/MT2Mob.h"
#include "Mobs/MT2MobAIComponent.h"
#include "Mobs/MT2MobRuntimeSettings.h"
#include "World/MT2PlayerSpatialGridSubsystem.h"
#include "World/MT2WorldSimulationSubsystem.h"

UMT2MobSpawnComponent::UMT2MobSpawnComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	ActiveSpawnRadius = GetDefault<UMT2MobRuntimeSettings>()->ActiveSimulationDistance;
}

void UMT2MobSpawnComponent::SetSpawnEntries(const TArray<FMT2MobSpawnEntry>& NewEntries)
{
	SpawnEntries = NewEntries;
	RuntimeStates.SetNum(SpawnEntries.Num());
	NextEntryCursor = 0;
	DisabledEntryIndices.Reset();
}

void UMT2MobSpawnComponent::SetSpawnExclusions(const TArray<FMT2MobSpawnExclusion>& NewExclusions)
{
	SpawnExclusions = NewExclusions;
}

void UMT2MobSpawnComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	RuntimeStates.SetNum(SpawnEntries.Num());
	ProcessSpawns();
	if (UMT2WorldSimulationSubsystem* Simulation = GetWorld()->GetSubsystem<UMT2WorldSimulationSubsystem>())
	{
		Simulation->RegisterSpawner(this);
	}
}

void UMT2MobSpawnComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UMT2WorldSimulationSubsystem* Simulation = World->GetSubsystem<UMT2WorldSimulationSubsystem>())
		{
			Simulation->UnregisterSpawner(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void UMT2MobSpawnComponent::RespawnMissingMobs()
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		DisabledEntryIndices.Reset();
		ReportedMissingVnums.Reset();
		ProcessSpawns();
	}
}

void UMT2MobSpawnComponent::ProcessSpawns()
{
	FRAMEPRO_NAMED_SCOPE("MT2.MobSpawner.ProcessSpawns");
	TRACE_CPUPROFILER_EVENT_SCOPE(MT2_MobSpawner_ProcessSpawns);
	UWorld* World = GetWorld();
	if (!World || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	if (RuntimeStates.Num() != SpawnEntries.Num())
	{
		RuntimeStates.SetNum(SpawnEntries.Num());
	}
	const double CurrentTime = World->GetTimeSeconds();
	RemoveDestroyedGroups(CurrentTime);
	const UMT2PlayerSpatialGridSubsystem* PlayerGrid =
		World->GetSubsystem<UMT2PlayerSpatialGridSubsystem>();

	int32 SpawnedThisUpdate = 0;
	int32 LastProcessedEntry = INDEX_NONE;
	for (int32 Iteration = 0; Iteration < SpawnEntries.Num(); ++Iteration)
	{
		const int32 EntryIndex = (NextEntryCursor + Iteration) % SpawnEntries.Num();
		LastProcessedEntry = EntryIndex;
		if (DisabledEntryIndices.Contains(EntryIndex))
		{
			continue;
		}
		const FMT2MobSpawnEntry& Entry = SpawnEntries[EntryIndex];
		if (Entry.Source != EMT2MobSpawnSource::Npc)
		{
			const float AreaRadius = ActiveSpawnRadius
				+ FMath::Max(Entry.HorizontalExtent.X, Entry.HorizontalExtent.Y);
			if (!PlayerGrid || PlayerGrid->EstimatePlayersInRadius(Entry.Center, AreaRadius) <= 0)
			{
				continue;
			}
		}
		FEntryRuntimeState& State = RuntimeStates[EntryIndex];
		while (State.ActiveGroups.Num() < FMath::Max(Entry.DesiredGroupCount, 0) &&
			CurrentTime >= State.NextSpawnTime && SpawnedThisUpdate < MaxGroupsSpawnedPerUpdate)
		{
			const bool bPassedChance = Entry.SpawnChancePercent >= 100.0f
				|| (Entry.SpawnChancePercent > 0.0f
					&& FMath::FRandRange(0.0f, 100.0f) < Entry.SpawnChancePercent);
			if (bPassedChance && SpawnGroup(EntryIndex))
			{
				++SpawnedThisUpdate;
			}
			State.NextSpawnTime = Entry.RespawnDelay > 0.0f
				? CurrentTime + Entry.RespawnDelay
				: TNumericLimits<double>::Max();
			break;
		}
		if (SpawnedThisUpdate >= MaxGroupsSpawnedPerUpdate)
		{
			break;
		}
	}
	if (LastProcessedEntry != INDEX_NONE && !SpawnEntries.IsEmpty())
	{
		NextEntryCursor = (LastProcessedEntry + 1) % SpawnEntries.Num();
	}
}

bool UMT2MobSpawnComponent::SpawnGroup(int32 EntryIndex)
{
	FRAMEPRO_NAMED_SCOPE("MT2.MobSpawner.SpawnGroup");
	TRACE_CPUPROFILER_EVENT_SCOPE(MT2_MobSpawner_SpawnGroup);
	if (!SpawnEntries.IsValidIndex(EntryIndex) || !RuntimeStates.IsValidIndex(EntryIndex))
	{
		return false;
	}
	const FMT2MobSpawnEntry& Entry = SpawnEntries[EntryIndex];
	const FMT2MobSpawnVariant* Variant = ChooseVariant(Entry);
	if (!Variant)
	{
		return false;
	}

	FActiveGroup ActiveGroup;
	FVector GroupAnchor = FVector::ZeroVector;
	bool bHasGroupAnchor = false;
	for (const FMT2MobSpawnMember& Member : Variant->Members)
	{
		UClass* MobClass = ResolveMobClass(Member.MobVnum);
		if (!MobClass)
		{
			continue;
		}

		const FVector Location = bHasGroupAnchor
			? FindGroupMemberLocation(Entry, MobClass, GroupAnchor)
			: AMT2Mob::FindGroundSpawnLocation(
				GetWorld(), TSubclassOf<AMT2Mob>(MobClass), FindGroundLocation(Entry), 0.0f, GetOwner());
		const float SourceYaw = Entry.Yaw < 0.0f ? FMath::FRandRange(0.0f, 360.0f) : Entry.Yaw;
		const float Yaw = FMath::UnwindDegrees(SourceYaw + 90.0f);
		FActorSpawnParameters Parameters;
		Parameters.Owner = GetOwner();
		Parameters.OverrideLevel = GetOwner()->GetLevel();
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		if (AMT2Mob* Mob = GetWorld()->SpawnActor<AMT2Mob>(MobClass, Location, FRotator(0.0f, Yaw, 0.0f), Parameters))
		{
			if (Entry.bForceAggressive) Mob->GetMobAIComponent()->SetForceAggressive(true);
			Mob->OnDestroyed.AddUniqueDynamic(this, &UMT2MobSpawnComponent::HandleSpawnedMobDestroyed);
			ActiveGroup.Members.Add(Mob);
			if (!ActiveGroup.Leader.IsValid() || Member.bLeader)
			{
				ActiveGroup.Leader = Mob;
			}
			if (!bHasGroupAnchor)
			{
				GroupAnchor = Mob->GetActorLocation();
				bHasGroupAnchor = true;
			}
		}
	}

	if (ActiveGroup.Members.IsEmpty())
	{
		DisabledEntryIndices.Add(EntryIndex);
		return false;
	}
	TArray<AMT2Mob*> GroupMembers;
	GroupMembers.Reserve(ActiveGroup.Members.Num());
	for (const TWeakObjectPtr<AMT2Mob>& Member : ActiveGroup.Members)
	{
		if (AMT2Mob* Mob = Member.Get())
		{
			GroupMembers.Add(Mob);
		}
	}
	for (AMT2Mob* Mob : GroupMembers)
	{
		Mob->SetSpawnGroupMembers(GroupMembers);
	}
	RuntimeStates[EntryIndex].ActiveGroups.Add(MoveTemp(ActiveGroup));
	return true;
}

const FMT2MobSpawnVariant* UMT2MobSpawnComponent::ChooseVariant(const FMT2MobSpawnEntry& Entry) const
{
	int32 TotalWeight = 0;
	for (const FMT2MobSpawnVariant& Variant : Entry.Variants)
	{
		if (!Variant.Members.IsEmpty())
		{
			TotalWeight += FMath::Max(Variant.Weight, 1);
		}
	}
	if (TotalWeight <= 0)
	{
		return nullptr;
	}

	int32 Roll = FMath::RandRange(1, TotalWeight);
	for (const FMT2MobSpawnVariant& Variant : Entry.Variants)
	{
		if (Variant.Members.IsEmpty())
		{
			continue;
		}
		Roll -= FMath::Max(Variant.Weight, 1);
		if (Roll <= 0)
		{
			return &Variant;
		}
	}
	return nullptr;
}

FVector UMT2MobSpawnComponent::FindGroundLocation(const FMT2MobSpawnEntry& Entry) const
{
	FRAMEPRO_NAMED_SCOPE("MT2.MobSpawner.FindGroundLocation");
	const UMT2MobRuntimeSettings* Settings = GetDefault<UMT2MobRuntimeSettings>();
	FVector Location = Entry.Center;
	for (int32 Attempt = 0; Attempt < Settings->GroundLocationAttempts; ++Attempt)
	{
		Location = Entry.Center;
		Location.X += FMath::FRandRange(-Entry.HorizontalExtent.X, Entry.HorizontalExtent.X);
		Location.Y += FMath::FRandRange(-Entry.HorizontalExtent.Y, Entry.HorizontalExtent.Y);
		if (!IsExcluded(Location))
		{
			break;
		}
	}

	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MT2MobSpawnGround), false, GetOwner());
	const FVector TraceStart(
		Location.X, Location.Y, Entry.Center.Z + Settings->MapGroundTraceDistance);
	const FVector TraceEnd(
		Location.X, Location.Y, Entry.Center.Z - Settings->MapGroundTraceDistance);
	if (GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_WorldStatic, QueryParams))
	{
		Location.Z = Hit.ImpactPoint.Z + Settings->MapGroundClearance;
	}
	return Location;
}

FVector UMT2MobSpawnComponent::FindGroupMemberLocation(
	const FMT2MobSpawnEntry& Entry, UClass* MobClass, const FVector& Anchor) const
{
	FVector Candidate = Anchor;
	if (Entry.Variants.Num() > 0)
	{
		const UMT2MobRuntimeSettings* Settings = GetDefault<UMT2MobRuntimeSettings>();
		const float Distance = FMath::FRandRange(
			Settings->GroupMemberMinimumDistance, Settings->GroupMemberMaximumDistance);
		const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
		Candidate += FVector(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance, 0.0f);
	}
	if (IsExcluded(Candidate))
	{
		Candidate = FindGroundLocation(Entry);
	}
	return AMT2Mob::FindGroundSpawnLocation(
		GetWorld(), TSubclassOf<AMT2Mob>(MobClass), Candidate, 0.0f, GetOwner());
}

bool UMT2MobSpawnComponent::IsExcluded(const FVector& Location) const
{
	return SpawnExclusions.ContainsByPredicate(
		[&Location](const FMT2MobSpawnExclusion& Exclusion) { return Exclusion.Contains(Location); });
}

UClass* UMT2MobSpawnComponent::ResolveMobClass(int32 MobVnum)
{
	FRAMEPRO_NAMED_SCOPE("MT2.MobSpawner.ResolveMobClass");
	if (const TWeakObjectPtr<UClass>* CachedClass = ResolvedMobClasses.Find(MobVnum))
	{
		if (CachedClass->IsValid())
		{
			return CachedClass->Get();
		}
		ResolvedMobClasses.Remove(MobVnum);
	}
	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UMT2VnumRegistrySubsystem* Registry = GameInstance
		? GameInstance->GetSubsystem<UMT2VnumRegistrySubsystem>() : nullptr;
	UClass* MobClass = Registry ? Registry->ResolveMobClass(MobVnum).Get() : nullptr;
	if (!MobClass)
	{
		if (!ReportedMissingVnums.Contains(MobVnum))
		{
			ReportedMissingVnums.Add(MobVnum);
			UE_LOG(LogTemp, Warning, TEXT("MT2 mob spawn skipped unregistered VNUM: %d"), MobVnum);
		}
		return nullptr;
	}
	ResolvedMobClasses.Add(MobVnum, MobClass);
	return MobClass;
}

void UMT2MobSpawnComponent::RemoveDestroyedGroups(double CurrentTime)
{
	FRAMEPRO_NAMED_SCOPE("MT2.MobSpawner.RemoveDestroyedGroups");
	for (int32 EntryIndex = 0; EntryIndex < RuntimeStates.Num(); ++EntryIndex)
	{
		FEntryRuntimeState& State = RuntimeStates[EntryIndex];
		for (int32 GroupIndex = State.ActiveGroups.Num() - 1; GroupIndex >= 0; --GroupIndex)
		{
			FActiveGroup& Group = State.ActiveGroups[GroupIndex];
			Group.Members.RemoveAll([](const TWeakObjectPtr<AMT2Mob>& Mob) { return !Mob.IsValid(); });
			if (!Group.Leader.IsValid())
			{
				State.ActiveGroups.RemoveAtSwap(GroupIndex);
				if (SpawnEntries.IsValidIndex(EntryIndex))
				{
					const float Delay = SpawnEntries[EntryIndex].RespawnDelay;
					State.NextSpawnTime = Delay > 0.0f
						? FMath::Max(State.NextSpawnTime, CurrentTime + Delay)
						: TNumericLimits<double>::Max();
				}
			}
		}
	}
}

void UMT2MobSpawnComponent::HandleSpawnedMobDestroyed(AActor* DestroyedActor)
{
	if (!DestroyedActor || !GetWorld())
	{
		return;
	}
	const double CurrentTime = GetWorld()->GetTimeSeconds();
	for (int32 EntryIndex = 0; EntryIndex < RuntimeStates.Num(); ++EntryIndex)
	{
		FEntryRuntimeState& State = RuntimeStates[EntryIndex];
		for (int32 GroupIndex = State.ActiveGroups.Num() - 1; GroupIndex >= 0; --GroupIndex)
		{
			FActiveGroup& Group = State.ActiveGroups[GroupIndex];
			Group.Members.RemoveAll([DestroyedActor](const TWeakObjectPtr<AMT2Mob>& Mob)
				{ return !Mob.IsValid() || Mob.Get() == DestroyedActor; });
			if (Group.Leader.Get() != DestroyedActor)
			{
				continue;
			}
			State.ActiveGroups.RemoveAtSwap(GroupIndex);
			const float Delay = SpawnEntries.IsValidIndex(EntryIndex)
				? SpawnEntries[EntryIndex].RespawnDelay : 0.0f;
			State.NextSpawnTime = Delay > 0.0f
				? CurrentTime + Delay : TNumericLimits<double>::Max();
			return;
		}
	}
}
