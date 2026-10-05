/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Mobs/MT2Mob.h"
#include "MT2MetinStone.generated.h"

// A metin stone is a mob_proto row with bType = CHAR_TYPE_STONE. It never fights like a mob (the
// old char_state.cpp errors outright: "Stone must not use battle state") - instead it sits still
// and, every 10% of HP it loses, plays its attack motion in place and summons a wave of mobs that
// go after whoever is breaking it (old __StateIdle_Stone).
// The importer generates BP_Metin_* Blueprints from this class and fills SpawnGroups from
// group.txt. See Docs/OldGameResearch/MetinStones.md.
UCLASS(Blueprintable)
class METIN2_API AMT2MetinStone : public AMT2Mob
{
	GENERATED_BODY()

public:
	AMT2MetinStone();

	virtual void ConfigureFromDefinition(const FMT2MobDefinition& Definition) override;
	virtual void RecordDamage(AActor* Attacker, float Amount) override;
	// Stones are anchored: a CRUSH never slides them (they carry AIFLAG_NOMOVE in the old data).
	virtual bool CanBeKnockedBack() const override { return false; }

	UFUNCTION(BlueprintPure, Category = "Metin")
	const TArray<FMT2MetinSpawnGroup>& GetSpawnGroups() const { return SpawnGroups; }

	void SetSpawnGroups(const TArray<FMT2MetinSpawnGroup>& NewGroups) { SpawnGroups = NewGroups; }
	void ProcessStoneBehavior();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// Candidate waves, imported from the proto's ATTACK_SPEED..MOVING_SPEED group-vnum range.
	// Editable on the generated Blueprint like every other imported mob field.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Metin")
	TArray<FMT2MetinSpawnGroup> SpawnGroups;

	// Each of these HP percentages triggers one wave, once (old ladder: 70/60/50/40/30/20/10).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Metin")
	TArray<int32> HealthStepPercents = {70, 60, 50, 40, 30, 20, 10};

	// Old SpawnGroup boxes sit +-500..1500 units around the stone.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Metin", meta = (Units = "cm"))
	float SpawnRadiusMin = 500.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Metin", meta = (Units = "cm"))
	float SpawnRadiusMax = 1500.0f;

private:
	void SpawnWave();
	void SpawnGroupMember(TSubclassOf<AMT2Mob> MobClass, AActor* Attacker);
	UFUNCTION() void HandleHealthChanged(float OldHealth, float NewHealth);
	UFUNCTION() void HandleStoneDeath();

	// Old m_set_pkChrSpawnedBy: breaking the stone kills everything still standing from its waves.
	TArray<TWeakObjectPtr<AMT2Mob>> SpawnedMobs;

	// Lowest step already fired (the old game abused MaxSP as this marker).
	int32 LastFiredStep = 100;
};
