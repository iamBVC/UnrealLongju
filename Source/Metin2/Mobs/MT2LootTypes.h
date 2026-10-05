/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Items/MT2ItemTypes.h"
#include "MT2LootTypes.generated.h"

UENUM(BlueprintType)
enum class EMT2LootRollType : uint8
{
	IndependentChance,
	WeightedKill
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2LootEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	int32 ItemVnum = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "1"))
	int32 MinCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "1"))
	int32 MaxCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float ChancePercent = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "1"))
	int32 Weight = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	EMT2LootRollType RollType = EMT2LootRollType::IndependentChance;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "0"))
	int32 KillAverage = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	int32 RollGroup = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float RareAttributeChancePercent = 0.0f;

	bool operator==(const FMT2LootEntry& Other) const
	{
		return ItemVnum == Other.ItemVnum && MinCount == Other.MinCount &&
			MaxCount == Other.MaxCount && FMath::IsNearlyEqual(ChancePercent, Other.ChancePercent) &&
			Weight == Other.Weight && RollType == Other.RollType && KillAverage == Other.KillAverage &&
			RollGroup == Other.RollGroup &&
			FMath::IsNearlyEqual(RareAttributeChancePercent, Other.RareAttributeChancePercent);
	}
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2GeneratedLootItem
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Loot")
	int32 ItemVnum = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Loot")
	int32 Count = 0;

	// Full rolled instance data (skill-book identity, bonuses and sockets). ItemVnum/Count remain for
	// Blueprint compatibility with existing reward listeners.
	UPROPERTY(BlueprintReadOnly, Category = "Loot")
	FMT2ItemInstanceData ItemData;
};

// One attacker's entry in a victim's damage map (old TDamageMap: VID -> TBattleInfo). Filled with
// the final, post-defense damage, which is what the old CHARACTER::Damage records.
USTRUCT(BlueprintType)
struct METIN2_API FMT2DamageShare
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Rewards")
	TWeakObjectPtr<AActor> Attacker;

	UPROPERTY(BlueprintReadOnly, Category = "Rewards")
	float TotalDamage = 0.0f;
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2MobRewardBundle
{
	GENERATED_BODY()

	// The top damager, i.e. what the old DistributeExp returns as pkAttacker - not necessarily
	// whoever landed the final blow.
	UPROPERTY(BlueprintReadOnly, Category = "Rewards")
	TObjectPtr<AActor> Killer;

	UPROPERTY(BlueprintReadOnly, Category = "Rewards")
	int64 Experience = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Rewards")
	int64 Gold = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Rewards")
	TArray<FMT2GeneratedLootItem> Items;
};

namespace MT2Loot
{
	METIN2_API TArray<FMT2GeneratedLootItem> Roll(const TArray<FMT2LootEntry>& Entries);
}
