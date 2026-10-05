/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Mobs/MT2MobTypes.h"

struct FMT2MobMaterialOverrideRecord
{
	FString SourceTextureObjectPath;
	FString TargetTextureObjectPath;
};

struct FMT2MobImportRecord
{
	FMT2MobDefinition Definition;
	FString SourceDirectory;
	FString SourceRelativeDirectory;
	FString MeshObjectPath;
	FString BlueprintObjectPath;
	TArray<FMT2MobMaterialOverrideRecord> MaterialOverrides;
};

struct FMT2MobImportResult
{
	// Already-imported mobs whose Blueprint class defaults (stats from mob_proto, spread across the
	// actor and its components) were refreshed in place without regenerating meshes/AnimBPs/motions -
	// keeps proto fixes cheap to roll out.
	int32 DefinitionsRefreshed = 0;
	int32 AnimationBlueprintsCreated = 0;
	int32 AnimationBlueprintsUpdated = 0;
	int32 MobBlueprintsCreated = 0;
	int32 Skipped = 0;
	TArray<FString> Errors;

	FString BuildSummary() const;
};

class FMT2MobImporter
{
public:
	// bIncludeExisting: also list mobs whose Blueprint already exists (the importer widget's
	// "Show Imported" checkbox) so they can be re-selected - importing them refreshes their
	// DataAsset stats from the freshly decoded mob_proto.
	static bool Discover(
		const FString& SourceRoot,
		const FString& DestinationRoot,
		TArray<FMT2MobImportRecord>& OutRecords,
		TArray<FString>& OutWarnings,
		FString& OutError,
		bool bIncludeExisting = false);

	static bool Import(
		const TArray<FMT2MobImportRecord>& Records,
		const FString& SourceRoot,
		const FString& DestinationRoot,
		TFunctionRef<bool()> ShouldCancel,
		FMT2MobImportResult& OutResult);

	static FString BuildBlueprintObjectPath(
		const FString& DestinationRoot, const FMT2MobDefinition& Definition);

	static int32 ImportMobLootEntries(
		const FString& SourceRoot,
		const FString& DestinationRoot,
		TArray<FString>& OutWarnings);

	// Parses db.sql shop/shop_item tables and writes each shop's stock into the matching NPC
	// Blueprint's shop component defaults (and sets its OnClickType to Shop). Returns the number
	// of NPC shops configured.
	static int32 ImportNpcShops(
		const FString& DatabasePath,
		const FString& DestinationRoot,
		TArray<FString>& OutWarnings);
};
