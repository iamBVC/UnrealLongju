/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Items/MT2Item.h"
#include "Items/MT2ItemTypes.h"
#include "MT2LootTable.generated.h"

UENUM(BlueprintType)
enum class EMT2LootRollMode : uint8
{
	Guaranteed,
	IndependentChance,
	WeightedGroup
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2LootTableEntry
{
	GENERATED_BODY()

	// Direct reference to the imported item Blueprint. This intentionally creates a hard reference
	// so the editor shows the exact item asset instead of an opaque VNUM.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loot")
	TSubclassOf<UMT2ItemTemplate> ItemTemplate;

	// Exact reward VNUM. It may select a variant inside ItemTemplate's VnumRange.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "0"))
	int32 ItemVnum = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loot")
	FMT2ItemInstanceData ItemData;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loot")
	EMT2LootCrateRewardKind Kind = EMT2LootCrateRewardKind::Item;

	// Item count, Yang, or experience. Item rewards retain their authored bonuses and sockets.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "0"))
	int64 MinimumAmount = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "0"))
	int64 MaximumAmount = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loot")
	EMT2LootRollMode RollMode = EMT2LootRollMode::Guaranteed;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float ChancePercent = 100.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "0"))
	int32 Weight = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "0"))
	int32 RollGroup = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "0"))
	int32 KillAverage = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loot", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float RareAttributeChancePercent = 0.0f;
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2LootTableResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Loot")
	EMT2LootCrateRewardKind Kind = EMT2LootCrateRewardKind::Item;

	UPROPERTY(BlueprintReadOnly, Category = "Loot")
	FMT2ItemSlot Item;

	UPROPERTY(BlueprintReadOnly, Category = "Loot")
	int64 Amount = 0;
};

UCLASS(Blueprintable)
class METIN2_API UMT2LootTable : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loot")
	TArray<FMT2LootTableEntry> Rewards;

	UFUNCTION(BlueprintPure, Category = "Loot")
	bool HasValidRewards() const;

	// Mob tables pass false so stale imported Guaranteed item entries cannot force a drop. Loot
	// crates keep the default because guaranteed crate rewards are intentional.
	TArray<FMT2LootTableResult> Roll(
		bool bAllowGuaranteedItems = true, float DropChanceMultiplier = 1.0f) const;
	int64 GetGuaranteedAmount(EMT2LootCrateRewardKind Kind) const;
};
