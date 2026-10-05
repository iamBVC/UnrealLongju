/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Mobs/MT2LootTypes.h"
#include "MT2MobLootComponent.generated.h"

struct FMT2MobDefinition;
class UMT2LootTable;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FMT2MobRewardsReadySignature, const FMT2MobRewardBundle&, Rewards);

UCLASS(BlueprintType, ClassGroup = "MT2", meta = (BlueprintSpawnableComponent))
class METIN2_API UMT2MobLootComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMT2MobLootComponent();

	// Writes the imported mob_proto reward fields onto this component. Called by the mob importer on
	// the Blueprint CDO; the values then persist as editable Blueprint defaults.
	void Configure(const FMT2MobDefinition& Definition);
	void SetLootTable(TSubclassOf<UMT2LootTable> InLootTable);

	// Rolls the drops and pays out this mob's death rewards across everyone who damaged it, the way
	// the old CHARACTER::Reward + DistributeExp do. See Docs/OldGameResearch/DamageDistributionAndAoE.md.
	void GenerateRewards(const TArray<FMT2DamageShare>& DamageShares, bool bIncludeDrops = true);

	// Exp banked on top of the proto's value (old SetExp): a metin stone collects half the exp of
	// every mob it summoned and pays it out when broken.
	void AddBonusExperience(int64 Amount);

	UFUNCTION(BlueprintPure, Category = "Mob|Rewards")
	int64 GetExperiencePool() const;

	UPROPERTY(BlueprintAssignable, Category = "Mob|Rewards")
	FMT2MobRewardsReadySignature OnRewardsReady;

	UFUNCTION(BlueprintPure, Category = "Mob|Rewards")
	int64 GetExperienceReward() const;

	UFUNCTION(BlueprintPure, Category = "Mob|Rewards")
	int64 GetGoldMin() const;

	UFUNCTION(BlueprintPure, Category = "Mob|Rewards")
	int64 GetGoldMax() const;

	UFUNCTION(BlueprintPure, Category = "Mob|Rewards")
	TSubclassOf<UMT2LootTable> GetLootTable() const { return LootTable; }

private:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mob|Rewards", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UMT2LootTable> LootTable;

	// Kept beside the loot table as a runtime fallback and importer validation source. Some client
	// mob_proto layouts do not carry server-side Yang values.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mob|Rewards", meta = (AllowPrivateAccess = "true"))
	int64 ImportedGoldMin = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mob|Rewards", meta = (AllowPrivateAccess = "true"))
	int64 ImportedGoldMax = 0;

	// Runtime-only exp handed over by stone-spawned mobs; never part of the imported proto data.
	int64 BonusExperience = 0;
	bool bRewardsGenerated = false;
};
