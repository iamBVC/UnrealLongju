/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "MT2MapTerrainBuilder.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	constexpr int32 Metin2TerrainSize = 128;
	constexpr int32 Metin2HeightRawSize = 131;
	constexpr int32 Metin2TileRawSize = 258;
	constexpr int32 Metin2TileUsableSize = 256;
	constexpr int32 UnrealLandscapeQuadsPerMetin2Cell = 127;
	constexpr int32 LandscapeWeightSmoothingRadius = 3;

	void BuildSmoothedLandscapeWeightmap(
		const TArray<uint8>& TileIndexMap, const int32 TilemapWidth, const int32 TilemapHeight,
		const uint8 TileIndex, const int32 LandscapeWidth, const int32 LandscapeHeight,
		TArray<uint8>& OutWeightmap)
	{
		// Summed-area filtering avoids aliasing when Metin2's 256x256 tile mask is reduced
		// to Unreal's roughly 128x128 landscape vertices. It also gives painted borders a
		// short, smooth transition instead of exposing the source mask's square pixels.
		const int32 IntegralWidth = TilemapWidth + 1;
		TArray<int32> Integral;
		Integral.Init(0, IntegralWidth * (TilemapHeight + 1));
		for (int32 Y = 0; Y < TilemapHeight; ++Y)
		{
			int32 RowSum = 0;
			for (int32 X = 0; X < TilemapWidth; ++X)
			{
				RowSum += TileIndexMap[Y * TilemapWidth + X] == TileIndex ? 1 : 0;
				Integral[(Y + 1) * IntegralWidth + X + 1] =
					Integral[Y * IntegralWidth + X + 1] + RowSum;
			}
		}

		OutWeightmap.Init(0, LandscapeWidth * LandscapeHeight);
		for (int32 DestY = 0; DestY < LandscapeHeight; ++DestY)
		{
			const int32 SourceY = FMath::Clamp(FMath::RoundToInt(
				static_cast<float>(DestY) * static_cast<float>(TilemapHeight - 1) /
				static_cast<float>(LandscapeHeight - 1)), 0, TilemapHeight - 1);
			const int32 MinY = FMath::Max(0, SourceY - LandscapeWeightSmoothingRadius);
			const int32 MaxY = FMath::Min(TilemapHeight - 1, SourceY + LandscapeWeightSmoothingRadius);
			const int32 ReversedDestY = LandscapeHeight - 1 - DestY;
			for (int32 DestX = 0; DestX < LandscapeWidth; ++DestX)
			{
				const int32 SourceX = FMath::Clamp(FMath::RoundToInt(
					static_cast<float>(DestX) * static_cast<float>(TilemapWidth - 1) /
					static_cast<float>(LandscapeWidth - 1)), 0, TilemapWidth - 1);
				const int32 MinX = FMath::Max(0, SourceX - LandscapeWeightSmoothingRadius);
				const int32 MaxX = FMath::Min(TilemapWidth - 1, SourceX + LandscapeWeightSmoothingRadius);
				const int32 SampleCount = (MaxX - MinX + 1) * (MaxY - MinY + 1);
				const int32 WeightCount =
					Integral[(MaxY + 1) * IntegralWidth + MaxX + 1]
					- Integral[MinY * IntegralWidth + MaxX + 1]
					- Integral[(MaxY + 1) * IntegralWidth + MinX]
					+ Integral[MinY * IntegralWidth + MinX];

				// Same east-west reversal as the heightmap so the painted textures line up.
				const int32 ReversedDestX = LandscapeWidth - 1 - DestX;
				OutWeightmap[ReversedDestY * LandscapeWidth + ReversedDestX] =
					static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(
						255.0f * static_cast<float>(WeightCount) / static_cast<float>(SampleCount)), 0, 255));
			}
		}
	}

	bool ParseIntToken(const TArray<FString>& Tokens, int32 Index, int32& OutValue)
	{
		if (!Tokens.IsValidIndex(Index))
		{
			return false;
		}

		OutValue = FCString::Atoi(*Tokens[Index]);
		return true;
	}

	bool ParseFloatToken(const TArray<FString>& Tokens, int32 Index, float& OutValue)
	{
		if (!Tokens.IsValidIndex(Index))
		{
			return false;
		}

		OutValue = FCString::Atof(*Tokens[Index]);
		return true;
	}

	FString NormalizeContentReference(FString Value)
	{
		Value.TrimStartAndEndInline();
		Value.RemoveFromStart(TEXT("\""));
		Value.RemoveFromEnd(TEXT("\""));
		Value.ReplaceInline(TEXT("\\"), TEXT("/"));
		return Value;
	}
}

bool FMT2MapTerrainBuilder::DiscoverMaps(const FMT2AssetScanResult& ScanResult, TArray<FMT2MapTerrainInfo>& OutMaps, FString& OutError)
{
	OutMaps.Reset();

	TMap<FString, FString> ContentPathToAbsolute;
	for (const FMT2AssetRecord& Record : ScanResult.Records)
	{
		ContentPathToAbsolute.Add(Record.ContentPath.ToLower(), Record.AbsolutePath);
	}

	for (const FMT2AssetRecord& Record : ScanResult.Records)
	{
		if (!FPaths::GetCleanFilename(Record.AbsolutePath).Equals(TEXT("setting.txt"), ESearchCase::IgnoreCase))
		{
			continue;
		}

		FMT2MapTerrainInfo MapInfo;
		if (!ParseSettingFile(Record, MapInfo))
		{
			continue;
		}

		const FString TextureSetReferenceLower = MapInfo.TextureSetReference.ToLower();
		if (!TextureSetReferenceLower.IsEmpty())
		{
			if (const FString* TextureSetPath = ContentPathToAbsolute.Find(TextureSetReferenceLower))
			{
				MapInfo.TextureSetPath = *TextureSetPath;
			}
			else if (const FString* NestedTextureSetPath = ContentPathToAbsolute.Find((TEXT("textureset/") + TextureSetReferenceLower).ToLower()))
			{
				MapInfo.TextureSetPath = *NestedTextureSetPath;
			}
		}

		if (!MapInfo.TextureSetPath.IsEmpty())
		{
			ParseTextureSet(MapInfo.TextureSetPath, MapInfo.TextureSetTextures, MapInfo.TextureSetEntries);
		}

		if (!MapInfo.EnvironmentReference.IsEmpty())
		{
			FString EnvironmentReference = MapInfo.EnvironmentReference.ToLower();
			EnvironmentReference.ReplaceInline(TEXT("\\"), TEXT("/"));
			const TArray<FString> EnvironmentCandidates = {
				EnvironmentReference,
				TEXT("ymir work/environment/") + FPaths::GetCleanFilename(EnvironmentReference),
				TEXT("environment/") + FPaths::GetCleanFilename(EnvironmentReference)
			};
			for (const FString& Candidate : EnvironmentCandidates)
			{
				if (const FString* EnvironmentPath = ContentPathToAbsolute.Find(Candidate))
				{
					MapInfo.EnvironmentPath = *EnvironmentPath;
					break;
				}
			}

			if (MapInfo.EnvironmentPath.IsEmpty())
			{
				const FString LocalEnvironmentPath = MapInfo.MapDirectory / MapInfo.EnvironmentReference;
				if (FPaths::FileExists(LocalEnvironmentPath))
				{
					MapInfo.EnvironmentPath = LocalEnvironmentPath;
				}
			}
		}

		OutMaps.Add(MoveTemp(MapInfo));
	}

	OutMaps.Sort([](const FMT2MapTerrainInfo& Left, const FMT2MapTerrainInfo& Right)
	{
		return Left.MapName < Right.MapName;
	});

	if (OutMaps.Num() == 0)
	{
		OutError = TEXT("No map setting.txt files were found.");
		return false;
	}

	return true;
}

bool FMT2MapTerrainBuilder::PrepareLandscapeSources(const FMT2MapTerrainInfo& MapInfo, const FString& OutputRoot, FMT2PreparedMapTerrain& OutPrepared, FString& OutError)
{
	if (MapInfo.MapSizeX <= 0 || MapInfo.MapSizeY <= 0)
	{
		OutError = FString::Printf(TEXT("Invalid map size for %s."), *MapInfo.MapName);
		return false;
	}

	OutPrepared = FMT2PreparedMapTerrain();
	OutPrepared.HeightmapWidth = MapInfo.MapSizeX * Metin2TerrainSize + 1;
	OutPrepared.HeightmapHeight = MapInfo.MapSizeY * Metin2TerrainSize + 1;
	OutPrepared.LandscapeComponentCountX = MapInfo.MapSizeX;
	OutPrepared.LandscapeComponentCountY = MapInfo.MapSizeY;
	OutPrepared.LandscapeComponentSizeQuads = UnrealLandscapeQuadsPerMetin2Cell;
	OutPrepared.LandscapeNumSubsections = 1;
	OutPrepared.LandscapeWidth = OutPrepared.LandscapeComponentCountX * OutPrepared.LandscapeComponentSizeQuads + 1;
	OutPrepared.LandscapeHeight = OutPrepared.LandscapeComponentCountY * OutPrepared.LandscapeComponentSizeQuads + 1;
	OutPrepared.LandscapeXYScale = static_cast<float>(MapInfo.CellScale) * static_cast<float>(Metin2TerrainSize) / static_cast<float>(UnrealLandscapeQuadsPerMetin2Cell);
	OutPrepared.LandscapeZScale = 128.0f;
	OutPrepared.TilemapWidth = MapInfo.MapSizeX * Metin2TileUsableSize;
	OutPrepared.TilemapHeight = MapInfo.MapSizeY * Metin2TileUsableSize;
	OutPrepared.OutputDirectory = OutputRoot / SanitizeFileName(MapInfo.MapName);

	FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*OutPrepared.OutputDirectory);

	TArray<uint16> Heightmap;
	Heightmap.Init(0, OutPrepared.HeightmapWidth * OutPrepared.HeightmapHeight);

	TArray<uint8> TileIndexMap;
	TileIndexMap.Init(0, OutPrepared.TilemapWidth * OutPrepared.TilemapHeight);

	TSet<uint8> UsedTileIndices;
	int32 MissingHeightCells = 0;
	int32 MissingTileCells = 0;

	for (int32 CellY = 0; CellY < MapInfo.MapSizeY; ++CellY)
	{
		for (int32 CellX = 0; CellX < MapInfo.MapSizeX; ++CellX)
		{
			const FString CellDirectory = MapInfo.MapDirectory / BuildCellName(CellX, CellY);

			TArray<uint16> CellHeights;
			if (LoadCellHeightmap(CellDirectory, CellHeights))
			{
				for (int32 LocalY = 0; LocalY <= Metin2TerrainSize; ++LocalY)
				{
					const int32 DestY = CellY * Metin2TerrainSize + LocalY;
					for (int32 LocalX = 0; LocalX <= Metin2TerrainSize; ++LocalX)
					{
						const int32 DestX = CellX * Metin2TerrainSize + LocalX;
						const int32 SourceIndex = (LocalY + 1) * Metin2HeightRawSize + (LocalX + 1);
						Heightmap[DestY * OutPrepared.HeightmapWidth + DestX] = CellHeights[SourceIndex];
					}
				}
			}
			else
			{
				MissingHeightCells++;
			}

			TArray<uint8> CellTiles;
			if (LoadCellTilemap(CellDirectory, CellTiles))
			{
				for (int32 LocalY = 0; LocalY < Metin2TileUsableSize; ++LocalY)
				{
					const int32 DestY = CellY * Metin2TileUsableSize + LocalY;
					for (int32 LocalX = 0; LocalX < Metin2TileUsableSize; ++LocalX)
					{
						const int32 DestX = CellX * Metin2TileUsableSize + LocalX;
						const int32 SourceIndex = (LocalY + 1) * Metin2TileRawSize + (LocalX + 1);
						const uint8 TileIndex = CellTiles[SourceIndex];
						TileIndexMap[DestY * OutPrepared.TilemapWidth + DestX] = TileIndex;
						UsedTileIndices.Add(TileIndex);
					}
				}
			}
			else
			{
				MissingTileCells++;
			}
		}
	}

	TArray<uint16> CenteredHeightmap;
	CenteredHeightmap.Reserve(Heightmap.Num());
	for (const uint16 Height : Heightmap)
	{
		const int32 CenteredHeight = FMath::Clamp(32768 + FMath::RoundToInt(static_cast<float>(Height) * MapInfo.HeightScale), 0, 65535);
		CenteredHeightmap.Add(static_cast<uint16>(CenteredHeight));
	}

	TArray<uint16> LandscapeHeightmap;
	LandscapeHeightmap.Init(32768, OutPrepared.LandscapeWidth * OutPrepared.LandscapeHeight);
	for (int32 DestY = 0; DestY < OutPrepared.LandscapeHeight; ++DestY)
	{
		const int32 SourceY = FMath::Clamp(FMath::RoundToInt(static_cast<float>(DestY) * static_cast<float>(Metin2TerrainSize) / static_cast<float>(UnrealLandscapeQuadsPerMetin2Cell)), 0, OutPrepared.HeightmapHeight - 1);
		const int32 ReversedDestY = OutPrepared.LandscapeHeight - 1 - DestY;
		for (int32 DestX = 0; DestX < OutPrepared.LandscapeWidth; ++DestX)
		{
			const int32 SourceX = FMath::Clamp(FMath::RoundToInt(static_cast<float>(DestX) * static_cast<float>(Metin2TerrainSize) / static_cast<float>(UnrealLandscapeQuadsPerMetin2Cell)), 0, OutPrepared.HeightmapWidth - 1);
			// Reverse X as well as Y: Metin2's raw grid runs the opposite way to the Unreal landscape on
			// the east-west axis, so without this the terrain came out mirrored left-right relative to the
			// (correctly placed) objects, spawns and minimap markers.
			const int32 ReversedDestX = OutPrepared.LandscapeWidth - 1 - DestX;
			LandscapeHeightmap[ReversedDestY * OutPrepared.LandscapeWidth + ReversedDestX] = CenteredHeightmap[SourceY * OutPrepared.HeightmapWidth + SourceX];
		}
	}
	OutPrepared.LandscapeHeightData = LandscapeHeightmap;

	OutPrepared.HeightmapPath = OutPrepared.OutputDirectory / TEXT("height_metin2_raw.r16");
	OutPrepared.CenteredHeightmapPath = OutPrepared.OutputDirectory / TEXT("height_ue_centered.r16");
	OutPrepared.LandscapeHeightmapPath = OutPrepared.OutputDirectory / TEXT("height_ue_landscape.r16");
	OutPrepared.TileIndexPath = OutPrepared.OutputDirectory / TEXT("tile_indices.raw");
	OutPrepared.ManifestPath = OutPrepared.OutputDirectory / TEXT("manifest.txt");

	if (!SaveRaw16(Heightmap, OutPrepared.HeightmapPath) ||
		!SaveRaw16(CenteredHeightmap, OutPrepared.CenteredHeightmapPath) ||
		!SaveRaw16(OutPrepared.LandscapeHeightData, OutPrepared.LandscapeHeightmapPath) ||
		!SaveRaw8(TileIndexMap, OutPrepared.TileIndexPath))
	{
		OutError = FString::Printf(TEXT("Failed to write landscape source files for %s."), *MapInfo.MapName);
		return false;
	}

	TArray<uint8> SortedTileIndices = UsedTileIndices.Array();
	SortedTileIndices.Sort();
	for (const uint8 TileIndex : SortedTileIndices)
	{
		TArray<uint8> Weightmap;
		Weightmap.Init(0, TileIndexMap.Num());
		for (int32 Index = 0; Index < TileIndexMap.Num(); ++Index)
		{
			Weightmap[Index] = TileIndexMap[Index] == TileIndex ? 255 : 0;
		}

		const FString WeightmapPath = OutPrepared.OutputDirectory / FString::Printf(TEXT("layer_tile_%03d.raw"), TileIndex);
		if (SaveRaw8(Weightmap, WeightmapPath))
		{
			OutPrepared.WeightmapPaths.Add(WeightmapPath);
		}

		TArray<uint8> LandscapeWeightmap;
		BuildSmoothedLandscapeWeightmap(
			TileIndexMap, OutPrepared.TilemapWidth, OutPrepared.TilemapHeight, TileIndex,
			OutPrepared.LandscapeWidth, OutPrepared.LandscapeHeight, LandscapeWeightmap);

		const FString LandscapeWeightmapPath = OutPrepared.OutputDirectory / FString::Printf(TEXT("landscape_layer_tile_%03d.raw"), TileIndex);
		if (SaveRaw8(LandscapeWeightmap, LandscapeWeightmapPath))
		{
			OutPrepared.LandscapeWeightmapPaths.Add(LandscapeWeightmapPath);
			OutPrepared.LandscapeWeightmapPathByTile.Add(TileIndex, LandscapeWeightmapPath);
			OutPrepared.LandscapeWeightDataByTile.Add(TileIndex, MoveTemp(LandscapeWeightmap));
		}
	}

	TArray<FString> Manifest;
	Manifest.Add(FString::Printf(TEXT("MapName=%s"), *MapInfo.MapName));
	Manifest.Add(FString::Printf(TEXT("MapSize=%d %d"), MapInfo.MapSizeX, MapInfo.MapSizeY));
	Manifest.Add(FString::Printf(TEXT("CellScale=%d"), MapInfo.CellScale));
	Manifest.Add(FString::Printf(TEXT("HeightScale=%f"), MapInfo.HeightScale));
	Manifest.Add(FString::Printf(TEXT("BasePosition=%d %d"), MapInfo.BasePosition.X, MapInfo.BasePosition.Y));
	Manifest.Add(FString::Printf(TEXT("HeightmapSize=%d %d"), OutPrepared.HeightmapWidth, OutPrepared.HeightmapHeight));
	Manifest.Add(FString::Printf(TEXT("LandscapeHeightmapSize=%d %d"), OutPrepared.LandscapeWidth, OutPrepared.LandscapeHeight));
	Manifest.Add(FString::Printf(TEXT("LandscapeComponents=%d %d"), OutPrepared.LandscapeComponentCountX, OutPrepared.LandscapeComponentCountY));
	Manifest.Add(FString::Printf(TEXT("LandscapeComponentSizeQuads=%d"), OutPrepared.LandscapeComponentSizeQuads));
	Manifest.Add(FString::Printf(TEXT("LandscapeScale=%f %f %f"), OutPrepared.LandscapeXYScale, OutPrepared.LandscapeXYScale, OutPrepared.LandscapeZScale));
	Manifest.Add(FString::Printf(TEXT("TilemapSize=%d %d"), OutPrepared.TilemapWidth, OutPrepared.TilemapHeight));
	Manifest.Add(FString::Printf(TEXT("MissingHeightCells=%d"), MissingHeightCells));
	Manifest.Add(FString::Printf(TEXT("MissingTileCells=%d"), MissingTileCells));
	Manifest.Add(FString::Printf(TEXT("TextureSetReference=%s"), *MapInfo.TextureSetReference));
	Manifest.Add(FString::Printf(TEXT("TextureSetPath=%s"), *MapInfo.TextureSetPath));
	Manifest.Add(TEXT(""));
	Manifest.Add(TEXT("[Textures]"));
	for (int32 Index = 0; Index < MapInfo.TextureSetTextures.Num(); ++Index)
	{
		Manifest.Add(FString::Printf(TEXT("%03d=%s"), Index + 1, *MapInfo.TextureSetTextures[Index]));
	}
	Manifest.Add(TEXT(""));
	Manifest.Add(TEXT("[Weightmaps]"));
	for (const FString& WeightmapPath : OutPrepared.WeightmapPaths)
	{
		Manifest.Add(WeightmapPath);
	}
	Manifest.Add(TEXT(""));
	Manifest.Add(TEXT("[LandscapeWeightmaps]"));
	for (const FString& WeightmapPath : OutPrepared.LandscapeWeightmapPaths)
	{
		Manifest.Add(WeightmapPath);
	}

	FFileHelper::SaveStringArrayToFile(Manifest, *OutPrepared.ManifestPath);
	return true;
}

bool FMT2MapTerrainBuilder::ParseSettingFile(const FMT2AssetRecord& SettingRecord, FMT2MapTerrainInfo& OutMap)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *SettingRecord.AbsolutePath))
	{
		return false;
	}

	OutMap = FMT2MapTerrainInfo();
	OutMap.SettingPath = SettingRecord.AbsolutePath;
	OutMap.MapDirectory = FPaths::GetPath(SettingRecord.AbsolutePath);

	TArray<FString> Parts;
	SettingRecord.ContentPath.ParseIntoArray(Parts, TEXT("/"), true);
	OutMap.MapName = Parts.Num() >= 2 ? Parts[Parts.Num() - 2] : FPaths::GetCleanFilename(OutMap.MapDirectory);

	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, false);
	for (const FString& Line : Lines)
	{
		TArray<FString> Tokens;
		Line.ParseIntoArrayWS(Tokens);
		if (Tokens.Num() == 0)
		{
			continue;
		}

		const FString Key = Tokens[0].ToLower();
		if (Key == TEXT("cellscale"))
		{
			ParseIntToken(Tokens, 1, OutMap.CellScale);
		}
		else if (Key == TEXT("heightscale"))
		{
			ParseFloatToken(Tokens, 1, OutMap.HeightScale);
		}
		else if (Key == TEXT("mapsize"))
		{
			ParseIntToken(Tokens, 1, OutMap.MapSizeX);
			ParseIntToken(Tokens, 2, OutMap.MapSizeY);
		}
		else if (Key == TEXT("baseposition"))
		{
			ParseIntToken(Tokens, 1, OutMap.BasePosition.X);
			ParseIntToken(Tokens, 2, OutMap.BasePosition.Y);
		}
		else if (Key == TEXT("textureset") && Tokens.Num() > 1)
		{
			OutMap.TextureSetReference = NormalizeContentReference(Tokens[1]);
		}
		else if (Key == TEXT("environment") && Tokens.Num() > 1)
		{
			OutMap.EnvironmentReference = NormalizeContentReference(Tokens[1]);
		}
	}

	return OutMap.MapSizeX > 0 && OutMap.MapSizeY > 0;
}

bool FMT2MapTerrainBuilder::ParseTextureSet(const FString& TextureSetPath, TArray<FString>& OutTextures,
	TArray<FMT2TerrainTextureInfo>& OutEntries)
{
	OutTextures.Reset();
	OutEntries.Reset();

	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *TextureSetPath))
	{
		return false;
	}

	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, false);
	for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
	{
		FString Line = Lines[LineIndex];
		Line.TrimStartAndEndInline();
		if (!Line.StartsWith(TEXT("\"")) || !Line.EndsWith(TEXT("\"")))
		{
			continue;
		}

		FMT2TerrainTextureInfo Entry;
		Entry.Reference = NormalizeContentReference(Line);
		TArray<FString> Values;
		for (int32 ValueLine = LineIndex + 1; ValueLine < Lines.Num() && Values.Num() < 7; ++ValueLine)
		{
			FString Value = Lines[ValueLine];
			Value.TrimStartAndEndInline();
			if (Value.IsEmpty())
			{
				continue;
			}
			if (Value.StartsWith(TEXT("End "), ESearchCase::IgnoreCase)
				|| Value.StartsWith(TEXT("Start "), ESearchCase::IgnoreCase))
			{
				break;
			}
			Values.Add(Value);
		}
		if (Values.Num() >= 7)
		{
			Entry.UScale = FCString::Atof(*Values[0]);
			Entry.VScale = FCString::Atof(*Values[1]);
			Entry.UOffset = FCString::Atof(*Values[2]);
			Entry.VOffset = FCString::Atof(*Values[3]);
			Entry.bSplat = FCString::Atoi(*Values[4]) != 0;
			Entry.BeginHeight = static_cast<uint16>(FMath::Clamp(FCString::Atoi(*Values[5]), 0, 65535));
			Entry.EndHeight = static_cast<uint16>(FMath::Clamp(FCString::Atoi(*Values[6]), 0, 65535));
		}
		OutTextures.Add(Entry.Reference);
		OutEntries.Add(MoveTemp(Entry));
	}

	return OutTextures.Num() > 0;
}

bool FMT2MapTerrainBuilder::LoadCellHeightmap(const FString& CellDirectory, TArray<uint16>& OutHeights)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *(CellDirectory / TEXT("height.raw"))) ||
		Bytes.Num() < Metin2HeightRawSize * Metin2HeightRawSize * static_cast<int32>(sizeof(uint16)))
	{
		return false;
	}

	OutHeights.SetNumUninitialized(Metin2HeightRawSize * Metin2HeightRawSize);
	FMemory::Memcpy(OutHeights.GetData(), Bytes.GetData(), OutHeights.Num() * sizeof(uint16));
	return true;
}

bool FMT2MapTerrainBuilder::LoadCellTilemap(const FString& CellDirectory, TArray<uint8>& OutTiles)
{
	if (!FFileHelper::LoadFileToArray(OutTiles, *(CellDirectory / TEXT("tile.raw"))) ||
		OutTiles.Num() < Metin2TileRawSize * Metin2TileRawSize)
	{
		return false;
	}
	return true;
}

bool FMT2MapTerrainBuilder::SaveRaw16(const TArray<uint16>& Data, const FString& Path)
{
	TArray<uint8> Bytes;
	Bytes.SetNumUninitialized(Data.Num() * sizeof(uint16));
	FMemory::Memcpy(Bytes.GetData(), Data.GetData(), Bytes.Num());
	return FFileHelper::SaveArrayToFile(Bytes, *Path);
}

bool FMT2MapTerrainBuilder::SaveRaw8(const TArray<uint8>& Data, const FString& Path)
{
	return FFileHelper::SaveArrayToFile(Data, *Path);
}

FString FMT2MapTerrainBuilder::BuildCellName(int32 CellX, int32 CellY)
{
	return FString::Printf(TEXT("%03d%03d"), CellX, CellY);
}

FString FMT2MapTerrainBuilder::SanitizeFileName(const FString& Value)
{
	return FMT2AssetScanner::SanitizePackagePathSegment(Value);
}
