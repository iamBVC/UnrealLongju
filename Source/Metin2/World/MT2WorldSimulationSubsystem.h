/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MT2WorldSimulationSubsystem.generated.h"

class AMT2MetinStone;
class AMT2Mob;
class UMT2MobLifecycleComponent;
class UMT2MobSpawnComponent;
class UMT2NameplateComponent;

UCLASS()
class METIN2_API UMT2WorldSimulationSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	void RegisterMob(AMT2Mob* Mob);
	void UnregisterMob(AMT2Mob* Mob);
	void SetMobSimulationActive(AMT2Mob* Mob, bool bActive);
	void RegisterNameplate(UMT2NameplateComponent* Nameplate);
	void UnregisterNameplate(UMT2NameplateComponent* Nameplate);
	void RegisterRegeneration(UMT2MobLifecycleComponent* Lifecycle);
	void UnregisterRegeneration(UMT2MobLifecycleComponent* Lifecycle);
	void RegisterSpawner(UMT2MobSpawnComponent* Spawner);
	void UnregisterSpawner(UMT2MobSpawnComponent* Spawner);
	void RegisterMetinStone(AMT2MetinStone* Stone);
	void UnregisterMetinStone(AMT2MetinStone* Stone);
	void ClearMobTargetsFor(const AActor* TargetActor);

private:
	friend class FMT2MobMoveSegmentsTest;
	template <typename T>
	static void RemoveInvalid(TSet<TWeakObjectPtr<T>>& Objects);

	void TickServerSimulation(double Now);
	void TickClientPresentation(bool bRefreshMobOcclusion);
	void TickClientMobAnimation();

	TSet<TWeakObjectPtr<AMT2Mob>> Mobs;
	TSet<TWeakObjectPtr<AMT2Mob>> ActiveMobs;
	TArray<TWeakObjectPtr<AMT2Mob>> MobSchedulerSnapshot;
	TSet<TWeakObjectPtr<UMT2NameplateComponent>> Nameplates;
	TSet<TWeakObjectPtr<UMT2MobLifecycleComponent>> RegenerationComponents;
	TSet<TWeakObjectPtr<UMT2MobSpawnComponent>> Spawners;
	TSet<TWeakObjectPtr<AMT2MetinStone>> MetinStones;
	TMap<TWeakObjectPtr<AMT2Mob>, double> NextMobUpdateTimes;

	double NextServerUpdateTime = 0.0;
	double NextRegenerationUpdateTime = 0.0;
	double NextSpawnerUpdateTime = 0.0;
	double NextStoneUpdateTime = 0.0;
	double NextNameplateUpdateTime = 0.0;
	double NextNameplateOcclusionUpdateTime = 0.0;
	double NextMobAnimationUpdateTime = 0.0;
};
