/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Mobs/MT2MobSpawnTypes.h"
#include "MT2MobSpawnComponent.generated.h"

class AMT2Mob;

UCLASS(BlueprintType, ClassGroup = "MT2", meta = (BlueprintSpawnableComponent))
class METIN2_API UMT2MobSpawnComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMT2MobSpawnComponent();

	UFUNCTION(BlueprintCallable, Category = "Mob Spawning")
	void SetSpawnEntries(const TArray<FMT2MobSpawnEntry>& NewEntries);
	void SetSpawnExclusions(const TArray<FMT2MobSpawnExclusion>& NewExclusions);

	UFUNCTION(BlueprintPure, Category = "Mob Spawning")
	const TArray<FMT2MobSpawnEntry>& GetSpawnEntries() const { return SpawnEntries; }

	UFUNCTION(BlueprintPure, Category = "Mob Spawning")
	const TArray<FMT2MobSpawnExclusion>& GetSpawnExclusions() const { return SpawnExclusions; }

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Mob Spawning")
	void RespawnMissingMobs();
	void ProcessSpawns();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	struct FActiveGroup
	{
		TWeakObjectPtr<AMT2Mob> Leader;
		TArray<TWeakObjectPtr<AMT2Mob>> Members;
	};

	struct FEntryRuntimeState
	{
		TArray<FActiveGroup> ActiveGroups;
		double NextSpawnTime = 0.0;
	};

	bool SpawnGroup(int32 EntryIndex);
	const FMT2MobSpawnVariant* ChooseVariant(const FMT2MobSpawnEntry& Entry) const;
	FVector FindGroundLocation(const FMT2MobSpawnEntry& Entry) const;
	FVector FindGroupMemberLocation(const FMT2MobSpawnEntry& Entry, UClass* MobClass, const FVector& Anchor) const;
	bool IsExcluded(const FVector& Location) const;
	UClass* ResolveMobClass(int32 MobVnum);
	void RemoveDestroyedGroups(double CurrentTime);

	UFUNCTION()
	void HandleSpawnedMobDestroyed(AActor* DestroyedActor);

	UPROPERTY(EditAnywhere, Category = "Mob Spawning")
	TArray<FMT2MobSpawnEntry> SpawnEntries;

	UPROPERTY(EditAnywhere, Category = "Mob Spawning")
	TArray<FMT2MobSpawnExclusion> SpawnExclusions;

	UPROPERTY(EditAnywhere, Category = "Mob Spawning", meta = (ClampMin = "1"))
	int32 MaxGroupsSpawnedPerUpdate = 128;

	UPROPERTY(EditAnywhere, Category = "Mob Spawning", meta = (ClampMin = "0.0", Units = "cm"))
	float ActiveSpawnRadius = 0.0f;

	TArray<FEntryRuntimeState> RuntimeStates;
	TMap<int32, TWeakObjectPtr<UClass>> ResolvedMobClasses;
	TSet<int32> ReportedMissingVnums;
	TSet<int32> DisabledEntryIndices;
	int32 NextEntryCursor = 0;
};
