/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "MT2AssetScanner.h"

struct FMT2PropertyRecord
{
	uint32 Crc = 0;
	FString AbsolutePath;
	FString VirtualPath;
	FString PropertyType;
	FString PropertyName;
	FString ReferencedAssetPath;
	FString ReferencedAssetVirtualPath;
	const FMT2AssetRecord* ReferencedAsset = nullptr;
	TMap<FString, TArray<FString>> Tokens;
};

struct FMT2MapObjectPlacement
{
	FString MapName;
	FString CellName;
	FString AreaDataPath;
	FVector Position = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	float HeightBias = 0.0f;
	// Map width in UE units (MapSizeX*128*CellScale), filled by the importer so the location can be
	// X-mirrored to match the east-west flipped landscape. 0 = unknown/no mirror.
	float WorldSizeX = 0.0f;
	uint32 PropertyCrc = 0;
	const FMT2PropertyRecord* Property = nullptr;
	const FMT2AssetRecord* ReferencedAsset = nullptr;
};

struct FMT2ResolvedWorldResult
{
	TMap<uint32, FMT2PropertyRecord> PropertiesByCrc;
	TArray<FMT2MapObjectPlacement> MapObjects;
	int32 PropertyFilesRead = 0;
	int32 InvalidPropertyFiles = 0;
	int32 DuplicatePropertyCrcs = 0;
	int32 AreaDataFilesRead = 0;
	int32 ObjectsWithProperty = 0;
	int32 ObjectsWithResolvedAsset = 0;
	int32 StaticGrannyObjects = 0;
};

class FMT2PropertyResolver
{
public:
	bool Resolve(const FMT2AssetScanResult& ScanResult, FMT2ResolvedWorldResult& OutResult, FString& OutError) const;

	static bool ParsePropertyFile(const FMT2AssetRecord& Record, FMT2PropertyRecord& OutProperty);
	static bool ParseAreaDataFile(const FMT2AssetRecord& Record, TArray<FMT2MapObjectPlacement>& OutPlacements);

private:
	static void BuildAssetLookup(const FMT2AssetScanResult& ScanResult, TMap<FString, const FMT2AssetRecord*>& OutLookup);
	static void ResolvePropertyAsset(FMT2PropertyRecord& Property, const TMap<FString, const FMT2AssetRecord*>& AssetLookup);
	static void ResolveMapMetadata(const FMT2AssetRecord& AreaDataRecord, FString& OutMapName, FString& OutCellName);
	static FString NormalizeReferencePath(const FString& RawPath);
	static bool ParseTokenLine(const FString& Line, FString& OutKey, TArray<FString>& OutValues);
	static bool ParseFloatTriple(const FString& Line, FVector& OutVector);
	static bool ParseRotation(const FString& Line, FRotator& OutRotation);
};
