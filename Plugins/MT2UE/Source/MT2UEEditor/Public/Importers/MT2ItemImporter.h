/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Items/MT2ItemTypes.h"

struct FMT2ImportedLootCrateReward
{
	EMT2LootCrateRewardKind Kind = EMT2LootCrateRewardKind::Item;
	int32 Vnum = 0;
	int64 Count = 0;
	int32 Weight = 0;
	int32 RarePercent = 0;
};

struct FMT2ItemImportRecord
{
	FMT2ItemDefinition Definition;
	TArray<FMT2ImportedLootCrateReward> LootCrateRewards;
	bool bLootCrateIndependentRolls = false;
	int32 MountVnum = 0;
	int32 MountDurationSeconds = 0;
	int32 MountMinimumPlayerLevel = 0;
	bool bConsumeMountItem = false;
};

struct FMT2ItemImportResult
{
	int32 ItemBlueprintsCreated = 0;
	int32 ItemBlueprintsUpdated = 0;
	int32 Skipped = 0;
	TArray<FString> Errors;

	FString BuildSummary() const;
};

class FMT2ItemImporter
{
public:
	static bool Discover(
		const FString& SourceRoot,
		const FString& DestinationRoot,
		TArray<FMT2ItemImportRecord>& OutRecords,
		TArray<FString>& OutWarnings,
		FString& OutError);

	static bool Import(
		const TArray<FMT2ItemImportRecord>& Records,
		const FString& DestinationRoot,
		TFunctionRef<bool()> ShouldCancel,
		FMT2ItemImportResult& OutResult);

	static FString BuildBlueprintObjectPath(const FString& DestinationRoot, const FMT2ItemDefinition& Definition);
};
