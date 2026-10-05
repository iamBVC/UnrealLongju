/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "MT2AssetScanner.h"
#include "MT2MapTerrainBuilder.h"
#include "MT2PropertyResolver.h"

enum class EMT2ImportDomain : uint8
{
	Textures,
	StaticMeshes,
	SkeletalMeshes,
	StaticObjects,
	MapTerrains,
	MapObjects,
	LandscapeMaterials,
	Characters,
	Skeletons,
	Animations,
	Effects,
	Audio,
	Scripts,
	Archives
};

enum class EMT2ImportSeverity : uint8
{
	Info,
	Warning,
	Error
};

enum class EMT2MeshUVTransform : uint8
{
	Original,
	FlipU,
	FlipV,
	FlipUV,
	Swap,
	SwapFlipU,
	SwapFlipV,
	SwapFlipUV
};

struct FMT2ImportMessage
{
	EMT2ImportSeverity Severity = EMT2ImportSeverity::Info;
	FString Text;
	FString SourcePath;
};

struct FMT2ImportContext
{
	FString SourceRoot;
	FString DestinationRoot = TEXT("/Game");
	FString WorkingDirectory;
	FString MapNameFilter;
	int32 MaxStaticMeshImports = 0;
	int32 MaxMapObjectPlacements = 0;
	const FMT2AssetScanResult* ScanResult = nullptr;
	const FMT2ResolvedWorldResult* ResolvedWorld = nullptr;
	bool bDryRun = false;
	bool bReplaceExisting = false;
	bool bImportReferencedTextures = true;
	bool bCreateLandscapeActors = true;
	bool bImportStaticObjectsWithMaps = true;
	bool bImportStaticObjectTextures = true;
	bool bEnableDebugLogs = false;
	EMT2MeshUVTransform MeshUVTransform = EMT2MeshUVTransform::Original;
	TFunction<bool()> ShouldCancel;

	bool IsStopRequested() const
	{
		return ShouldCancel && ShouldCancel();
	}
};

struct FMT2ImportSelection
{
	EMT2ImportDomain Domain = EMT2ImportDomain::Textures;
	TArray<FMT2AssetRecord> AssetRecords;
	TArray<FMT2MapTerrainInfo> MapTerrains;
	TArray<FMT2MapObjectPlacement> MapObjectPlacements;
	TArray<FString> MapNames;
};

struct FMT2ImportRequest
{
	FMT2ImportContext Context;
	FMT2ImportSelection Selection;
	int32 MaxItems = 0;
};

struct FMT2ImportResult
{
	bool bSucceeded = false;
	int32 ItemsDiscovered = 0;
	int32 ItemsImported = 0;
	int32 ItemsSkipped = 0;
	TArray<FString> CreatedPackages;
	TArray<FString> CreatedFiles;
	TArray<FMT2ImportMessage> Messages;

	void AddInfo(const FString& Text, const FString& SourcePath = FString());
	void AddWarning(const FString& Text, const FString& SourcePath = FString());
	void AddError(const FString& Text, const FString& SourcePath = FString());
	bool HasErrors() const;
};

struct FMT2ImportDiscovery
{
	EMT2ImportDomain Domain = EMT2ImportDomain::Textures;
	int32 ItemsDiscovered = 0;
	TArray<FMT2AssetRecord> AssetRecords;
	TArray<FMT2MapTerrainInfo> MapTerrains;
	TArray<FString> EntryNames;
	TMap<FString, int32> EntryCountsByName;
	TArray<FMT2ImportMessage> Messages;
};

FString LexToString(EMT2ImportDomain Domain);
