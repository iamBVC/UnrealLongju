/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "MT2AssetScanner.h"

struct FMT2TerrainTextureInfo
{
	FString Reference;
	float UScale = 4.0f;
	float VScale = 4.0f;
	float UOffset = 0.0f;
	float VOffset = 0.0f;
	bool bSplat = true;
	uint16 BeginHeight = 0;
	uint16 EndHeight = 0;
};

struct FMT2MapTerrainInfo
{
	FString MapName;
	FString MapDirectory;
	FString SettingPath;
	FString TextureSetReference;
	FString TextureSetPath;
	FString EnvironmentReference;
	FString EnvironmentPath;
	TArray<FString> TextureSetTextures;
	TArray<FMT2TerrainTextureInfo> TextureSetEntries;
	int32 MapSizeX = 0;
	int32 MapSizeY = 0;
	int32 CellScale = 200;
	float HeightScale = 0.5f;
	FIntPoint BasePosition = FIntPoint::ZeroValue;
};

struct FMT2PreparedMapTerrain
{
	FString OutputDirectory;
	FString HeightmapPath;
	FString CenteredHeightmapPath;
	FString LandscapeHeightmapPath;
	FString TileIndexPath;
	FString ManifestPath;
	TArray<FString> WeightmapPaths;
	TArray<FString> LandscapeWeightmapPaths;
	TMap<uint8, FString> LandscapeWeightmapPathByTile;
	TArray<uint16> LandscapeHeightData;
	TMap<uint8, TArray<uint8>> LandscapeWeightDataByTile;
	int32 HeightmapWidth = 0;
	int32 HeightmapHeight = 0;
	int32 LandscapeWidth = 0;
	int32 LandscapeHeight = 0;
	int32 LandscapeComponentCountX = 0;
	int32 LandscapeComponentCountY = 0;
	int32 LandscapeComponentSizeQuads = 127;
	int32 LandscapeNumSubsections = 1;
	float LandscapeXYScale = 200.0f;
	float LandscapeZScale = 128.0f;
	int32 TilemapWidth = 0;
	int32 TilemapHeight = 0;
};

class FMT2MapTerrainBuilder
{
public:
	static bool DiscoverMaps(const FMT2AssetScanResult& ScanResult, TArray<FMT2MapTerrainInfo>& OutMaps, FString& OutError);
	static bool PrepareLandscapeSources(const FMT2MapTerrainInfo& MapInfo, const FString& OutputRoot, FMT2PreparedMapTerrain& OutPrepared, FString& OutError);

private:
	static bool ParseSettingFile(const FMT2AssetRecord& SettingRecord, FMT2MapTerrainInfo& OutMap);
	static bool ParseTextureSet(const FString& TextureSetPath, TArray<FString>& OutTextures,
		TArray<FMT2TerrainTextureInfo>& OutEntries);
	static bool LoadCellHeightmap(const FString& CellDirectory, TArray<uint16>& OutHeights);
	static bool LoadCellTilemap(const FString& CellDirectory, TArray<uint8>& OutTiles);
	static bool SaveRaw16(const TArray<uint16>& Data, const FString& Path);
	static bool SaveRaw8(const TArray<uint8>& Data, const FString& Path);
	static FString BuildCellName(int32 CellX, int32 CellY);
	static FString SanitizeFileName(const FString& Value);
};
