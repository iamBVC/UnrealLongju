/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Importers/MT2MapTerrainImporter.h"
#include "Config/MT2PathSettings.h"
#include "MT2MapAttributeReader.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
#include "Importers/MT2AudioImporter.h"
#include "Importers/MT2LandscapeMaterialImporter.h"
#include "Importers/MT2MapObjectImporter.h"
#include "Importers/MT2MapEnvironmentReader.h"
#include "Importers/MT2MeshUsageClassifier.h"
#include "Importers/MT2MobProtoReader.h"
#include "Importers/MT2SkeletalMeshImporter.h"
#include "Importers/MT2StaticMeshImporter.h"
#include "Importers/MT2TextureImporter.h"
#include "Importers/MT2GrannyMeshConverter.h"
#include "Core/MT2VnumRegistry.h"
#include "Engine/Level.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "LandscapeEdit.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeProxy.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Mobs/MT2MobSpawnActor.h"
#include "Mobs/MT2MobSpawnComponent.h"
#include "Mobs/MT2Mob.h"
#include "World/MT2MapPresentationActor.h"
#include "World/MT2Portal.h"
#include "Components/SkeletalMeshComponent.h"
#include "ScopedTransaction.h"
#include "Sound/SoundBase.h"

#define LOCTEXT_NAMESPACE "FMT2MapTerrainImporter"

namespace
{
	struct FImportedMobGroup
	{
		TArray<int32> Members;
	};

	struct FImportedMobGroupChoice
	{
		int32 GroupVnum = 0;
		int32 Weight = 1;
	};

	struct FServerMapBounds
	{
		FString MapName;
		double BaseX = 0.0;
		double BaseY = 0.0;
		double WorldSizeX = 0.0;
		double WorldSizeY = 0.0;
		double ImportedWorldSizeX = 0.0;

		bool Contains(double X, double Y) const
		{
			return X >= BaseX && Y >= BaseY && X <= BaseX + WorldSizeX && Y <= BaseY + WorldSizeY;
		}
	};

	FString FindServerLocaleRoot(const FMT2ImportContext& Context);
	FString FindServerMapDirectory(const FString& LocaleRoot, const FString& MapName);
	bool LoadServerMapIdentity(
		const FMT2ImportContext& Context, const FString& SourceMapName,
		FString& OutMapIdentifier, int32& OutMapIndex);
	bool LoadLocalizedMapName(
		const FMT2ImportContext& Context, const FString& MapIdentifier, int32 MapIndex,
		FString& OutMapName);
	bool LoadTownSpawnLocations(const FMT2ImportContext& Context, const FMT2MapTerrainInfo& Map,
		FVector2D& OutDefaultSpawn, TArray<FVector2D>& OutEmpireSpawns);

	bool LoadServerMapBounds(const FString& MapDirectory, FServerMapBounds& OutBounds)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *(MapDirectory / UMT2PathSettings::Path(TEXT("Part_Setting")))))
		{
			return false;
		}

		FIntPoint BasePosition = FIntPoint::ZeroValue;
		FIntPoint MapSize = FIntPoint::ZeroValue;
		float CellScale = 200.0f;
		bool bHasBasePosition = false;
		bool bHasMapSize = false;
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, true);
		for (FString Line : Lines)
		{
			TArray<FString> Tokens;
			Line.TrimStartAndEndInline();
			Line.ParseIntoArrayWS(Tokens);
			if (Tokens.Num() >= 3 && Tokens[0].Equals(TEXT("BasePosition"), ESearchCase::IgnoreCase))
			{
				BasePosition = FIntPoint(FCString::Atoi(*Tokens[1]), FCString::Atoi(*Tokens[2]));
				bHasBasePosition = true;
			}
			else if (Tokens.Num() >= 3 && Tokens[0].Equals(TEXT("MapSize"), ESearchCase::IgnoreCase))
			{
				MapSize = FIntPoint(FCString::Atoi(*Tokens[1]), FCString::Atoi(*Tokens[2]));
				bHasMapSize = true;
			}
			else if (Tokens.Num() >= 2 && Tokens[0].Equals(TEXT("CellScale"), ESearchCase::IgnoreCase))
			{
				CellScale = FCString::Atof(*Tokens[1]);
			}
		}
		if (!bHasBasePosition || !bHasMapSize || MapSize.X <= 0 || MapSize.Y <= 0 || CellScale <= 0.0f)
		{
			return false;
		}

		OutBounds.MapName = FPaths::GetCleanFilename(MapDirectory);
		OutBounds.BaseX = BasePosition.X;
		OutBounds.BaseY = BasePosition.Y;
		OutBounds.WorldSizeX = MapSize.X * 128.0 * CellScale;
		OutBounds.WorldSizeY = MapSize.Y * 128.0 * CellScale;
		OutBounds.ImportedWorldSizeX = OutBounds.WorldSizeX;
		return true;
	}

	void LoadAllServerMapBounds(
		const FString& LocaleRoot, const FMT2AssetScanResult* ScanResult,
		TArray<FServerMapBounds>& OutBounds)
	{
		const FString MapRoot = LocaleRoot / UMT2PathSettings::Path(TEXT("Part_map"));
		TArray<FString> IndexedMapNames;
		TArray<FString> IndexLines;
		if (FFileHelper::LoadFileToStringArray(IndexLines, *(MapRoot / UMT2PathSettings::Path(TEXT("Part_index")))))
		{
			for (FString Line : IndexLines)
			{
				const int32 CommentIndex = Line.Find(TEXT("#"));
				if (CommentIndex != INDEX_NONE)
				{
					Line.LeftInline(CommentIndex);
				}

				TArray<FString> Parts;
				Line.ParseIntoArrayWS(Parts);
				if (Parts.Num() >= 2 && Parts[0].IsNumeric())
				{
					IndexedMapNames.AddUnique(Parts[1]);
				}
			}
		}

		TArray<FString> MapDirectories;
		IFileManager::Get().FindFiles(MapDirectories, *(MapRoot / TEXT("*")), false, true);
		const TArray<FString>& MapNames = IndexedMapNames.IsEmpty() ? MapDirectories : IndexedMapNames;
		for (const FString& MapName : MapNames)
		{
			const FString* MatchingDirectory = MapDirectories.FindByPredicate(
				[&MapName](const FString& Directory)
				{
					return Directory.Equals(MapName, ESearchCase::IgnoreCase);
				});
			if (!MatchingDirectory)
			{
				continue;
			}

			FServerMapBounds Bounds;
			if (LoadServerMapBounds(MapRoot / *MatchingDirectory, Bounds))
			{
				OutBounds.Add(MoveTemp(Bounds));
			}
		}

		if (!ScanResult)
		{
			return;
		}

		TArray<FMT2MapTerrainInfo> ImportedMaps;
		FString DiscoveryError;
		if (!FMT2MapTerrainBuilder::DiscoverMaps(*ScanResult, ImportedMaps, DiscoveryError))
		{
			return;
		}
		for (FServerMapBounds& Bounds : OutBounds)
		{
			const FMT2MapTerrainInfo* ImportedMap = ImportedMaps.FindByPredicate(
				[&Bounds](const FMT2MapTerrainInfo& Candidate)
				{
					return Candidate.MapName.Equals(Bounds.MapName, ESearchCase::IgnoreCase);
				});
			if (ImportedMap && ImportedMap->MapSizeX > 0 && ImportedMap->CellScale > 0)
			{
				Bounds.ImportedWorldSizeX =
					ImportedMap->MapSizeX * 128.0 * ImportedMap->CellScale;
			}
		}
	}

	bool ParsePortalTarget(
		const FMT2MobDefinition& Definition,
		const FMT2MapTerrainInfo& SourceMap,
		const TArray<FServerMapBounds>& MapBounds,
		FString& OutMapName,
		FVector2D& OutDestination,
		FString& OutDisplayName)
	{
		const FString EncodedTarget = Definition.DisplayName.IsEmpty()
			? Definition.InternalName : Definition.DisplayName;
		TArray<FString> Tokens;
		EncodedTarget.ParseIntoArrayWS(Tokens);
		if (Tokens.Num() < 3 || !Tokens[Tokens.Num() - 2].IsNumeric() || !Tokens.Last().IsNumeric())
		{
			return false;
		}

		const double TargetX = FCString::Atod(*Tokens[Tokens.Num() - 2]) * 100.0;
		const double TargetY = FCString::Atod(*Tokens.Last()) * 100.0;
		Tokens.SetNum(Tokens.Num() - 2);
		OutDisplayName = FString::Join(Tokens, TEXT(" ")).Replace(TEXT("_"), TEXT(" "));
		OutDisplayName.TrimStartAndEndInline();

		if (Definition.Type == EMT2MobType::Goto)
		{
			const double SourceWorldSizeX = SourceMap.MapSizeX * 128.0 * SourceMap.CellScale;
			OutMapName = SourceMap.MapName;
			OutDestination = FVector2D(SourceWorldSizeX - TargetX, -TargetY);
			return true;
		}

		const FServerMapBounds* TargetMap = nullptr;
		for (const FServerMapBounds& Candidate : MapBounds)
		{
			if (!Candidate.Contains(TargetX, TargetY))
			{
				continue;
			}
			if (!TargetMap || Candidate.WorldSizeX * Candidate.WorldSizeY < TargetMap->WorldSizeX * TargetMap->WorldSizeY)
			{
				TargetMap = &Candidate;
			}
		}
		if (!TargetMap)
		{
			return false;
		}

		OutMapName = TargetMap->MapName;
		OutDestination = FVector2D(
			TargetMap->ImportedWorldSizeX - (TargetX - TargetMap->BaseX),
			-(TargetY - TargetMap->BaseY));
		return true;
	}

	float FindPortalGroundZ(UWorld* World, const FVector2D& Location)
	{
		if (!World)
		{
			return 0.0f;
		}
		TArray<FHitResult> Hits;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MT2PortalImportGround), false);
		if (World->LineTraceMultiByChannel(
			Hits, FVector(Location.X, Location.Y, 500000.0f), FVector(Location.X, Location.Y, -500000.0f),
			ECC_WorldStatic, Params))
		{
			for (const FHitResult& Hit : Hits)
			{
				if (Hit.GetActor() && Hit.GetActor()->IsA<ALandscapeProxy>())
				{
					return Hit.ImpactPoint.Z;
				}
			}
		}

		// Imported maps can use landscape streaming proxies. If collision is not currently available,
		// sample the landscape height directly rather than accepting a building above the terrain.
		for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
		{
			if (const TOptional<float> Height = It->GetHeightAtLocation(
				FVector(Location.X, Location.Y, 0.0f), EHeightfieldSource::Complex))
			{
				return Height.GetValue();
			}
		}
		return 0.0f;
	}

	bool LoadServerMapBgm(
		const FMT2ImportContext& Context, int32 MapIndex,
		FString& OutBgmFileName, FString& OutSettingsPath)
	{
		OutBgmFileName.Reset();
		OutSettingsPath = FindServerLocaleRoot(Context) / UMT2PathSettings::Path(TEXT("Part_settings"));
		if (MapIndex <= 0)
		{
			return false;
		}

		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *OutSettingsPath))
		{
			return false;
		}
		for (FString Line : Lines)
		{
			const int32 CommentIndex = Line.Find(TEXT("--"));
			if (CommentIndex != INDEX_NONE)
			{
				Line.LeftInline(CommentIndex);
			}

			const int32 FunctionIndex = Line.Find(TEXT("add_bgm_info"), ESearchCase::IgnoreCase);
			const int32 OpenParenthesis = Line.Find(TEXT("("), ESearchCase::CaseSensitive,
				ESearchDir::FromStart, FMath::Max(FunctionIndex, 0));
			const int32 CloseParenthesis = Line.Find(TEXT(")"), ESearchCase::CaseSensitive,
				ESearchDir::FromEnd);
			if (FunctionIndex == INDEX_NONE || OpenParenthesis == INDEX_NONE ||
				CloseParenthesis <= OpenParenthesis)
			{
				continue;
			}

			TArray<FString> Arguments;
			Line.Mid(OpenParenthesis + 1, CloseParenthesis - OpenParenthesis - 1)
				.ParseIntoArray(Arguments, TEXT(","), true);
			if (Arguments.Num() < 2)
			{
				continue;
			}
			Arguments[0].TrimStartAndEndInline();
			if (!Arguments[0].IsNumeric() || FCString::Atoi(*Arguments[0]) != MapIndex)
			{
				continue;
			}

			OutBgmFileName = Arguments[1].TrimStartAndEnd();
			OutBgmFileName.RemoveFromStart(TEXT("\""));
			OutBgmFileName.RemoveFromEnd(TEXT("\""));
			OutBgmFileName.RemoveFromStart(TEXT("'"));
			OutBgmFileName.RemoveFromEnd(TEXT("'"));
			OutBgmFileName.ReplaceInline(TEXT("\\"), TEXT("/"));
			return !OutBgmFileName.IsEmpty();
		}
		return false;
	}

	const FMT2AssetRecord* FindMapBgmRecord(
		const FMT2AssetScanResult* ScanResult, const FString& BgmFileName)
	{
		if (!ScanResult)
		{
			return nullptr;
		}

		FString NormalizedBgm = BgmFileName.ToLower();
		NormalizedBgm.ReplaceInline(TEXT("\\"), TEXT("/"));
		const FString ExpectedContentPath = TEXT("bgm/") + NormalizedBgm;
		const FMT2AssetRecord* FileNameMatch = nullptr;
		for (const FMT2AssetRecord& Record : ScanResult->Records)
		{
			if (Record.Kind != EMT2AssetKind::Audio)
			{
				continue;
			}
			FString ContentPath = Record.ContentPath.ToLower();
			ContentPath.ReplaceInline(TEXT("\\"), TEXT("/"));
			if (ContentPath == ExpectedContentPath || ContentPath.EndsWith(TEXT("/") + ExpectedContentPath))
			{
				return &Record;
			}
			if (!FileNameMatch && FPaths::GetCleanFilename(ContentPath).Equals(
				FPaths::GetCleanFilename(NormalizedBgm), ESearchCase::IgnoreCase))
			{
				FileNameMatch = &Record;
			}
		}
		return FileNameMatch;
	}

	USoundBase* ResolveOrImportMapBgm(
		const FMT2ImportContext& Context, const FString& BgmFileName,
		FMT2ImportResult& OutResult)
	{
		const FMT2AssetRecord* Record = FindMapBgmRecord(Context.ScanResult, BgmFileName);
		if (!Record)
		{
			return nullptr;
		}

		const FString ObjectPath = FMT2AudioImporter::BuildObjectPath(Context, *Record);
		if (USoundBase* ExistingSound = Cast<USoundBase>(FSoftObjectPath(ObjectPath).TryLoad()))
		{
			return ExistingSound;
		}

		FMT2ImportRequest AudioRequest;
		AudioRequest.Context = Context;
		AudioRequest.Context.bReplaceExisting = false;
		AudioRequest.Selection.Domain = EMT2ImportDomain::Audio;
		AudioRequest.Selection.AssetRecords.Add(*Record);
		FMT2AudioImporter AudioImporter;
		FMT2ImportResult AudioResult;
		AudioImporter.Import(AudioRequest, AudioResult);
		OutResult.CreatedPackages.Append(AudioResult.CreatedPackages);
		OutResult.Messages.Append(AudioResult.Messages);
		return Cast<USoundBase>(FSoftObjectPath(ObjectPath).TryLoad());
	}

	void ImportMapPresentationForMap(
		const FMT2ImportContext& Context, const FMT2MapTerrainInfo& Map, FMT2ImportResult& OutResult)
	{
		if (!GEditor) return;
		UWorld* World = GEditor->GetEditorWorldContext().World();
		ULevel* Level = World ? World->GetCurrentLevel() : nullptr;
		if (!World || !Level) return;

		const FName ActorTag(*(TEXT("MT2UE_MapPresentation:") + Map.MapName.ToLower()));
		for (AActor* Actor : Level->Actors)
		{
			if (const AMT2MapPresentationActor* ExistingPresentation = Cast<AMT2MapPresentationActor>(Actor);
				ExistingPresentation &&
				(ExistingPresentation->Tags.Contains(ActorTag) ||
					ExistingPresentation->MapId.Equals(Map.MapName, ESearchCase::IgnoreCase) ||
					ExistingPresentation->GetActorLabel().Equals(
						FString::Printf(TEXT("MT2_MapPresentation_%s"), *Map.MapName), ESearchCase::IgnoreCase)))
			{
				FMT2MapAttributes Attributes;
				FString Error;
				const FString AttributeFile = FindServerMapDirectory(FindServerLocaleRoot(Context), Map.MapName) / UMT2PathSettings::Path(TEXT("Part_server_attr"));
				const bool bServerAttributes = FMT2MapAttributeReader::Read(AttributeFile, Attributes, Error) &&
					Attributes.Size == FIntPoint(Map.MapSizeX, Map.MapSizeY) * 512;
				if (bServerAttributes || FMT2MapAttributeReader::ReadClient(Map.MapDirectory,
					FIntPoint(Map.MapSizeX, Map.MapSizeY), Attributes, Error))
				{
					AMT2MapPresentationActor* Mutable = const_cast<AMT2MapPresentationActor*>(ExistingPresentation);
					Mutable->Modify();
					Mutable->Attributes = MoveTemp(Attributes);
					Mutable->MarkPackageDirty();
					if (!bServerAttributes) { OutResult.AddWarning(TEXT("Used client attributes: server data unavailable or dimensions differ."), Map.MapDirectory); }
				}
				else { OutResult.AddWarning(Error, AttributeFile); }
				OutResult.AddInfo(FString::Printf(
					TEXT("Kept existing map presentation for %s; reimport did not modify its lighting, fog, music, minimap, or spawn settings."),
					*Map.MapName), Map.MapDirectory);
				return;
			}
		}

		TArray<FMT2AssetRecord> TileRecords;
		if (Context.ScanResult)
		{
			const FString MapDirectory = FPaths::ConvertRelativePathToFull(Map.MapDirectory);
			for (const FMT2AssetRecord& Record : Context.ScanResult->Records)
			{
				if (Record.Kind != EMT2AssetKind::Texture ||
					!FPaths::GetBaseFilename(Record.AbsolutePath).Equals(TEXT("minimap"), ESearchCase::IgnoreCase))
				{
					continue;
				}
				const FString Parent = FPaths::GetCleanFilename(FPaths::GetPath(Record.AbsolutePath));
				const FString FullPath = FPaths::ConvertRelativePathToFull(Record.AbsolutePath);
				if (Parent.Len() == 6 && Parent.IsNumeric() && FullPath.StartsWith(MapDirectory, ESearchCase::IgnoreCase))
				{
					TileRecords.Add(Record);
				}
			}
		}

		if (!TileRecords.IsEmpty())
		{
			FMT2ImportRequest TextureRequest;
			TextureRequest.Context = Context;
			TextureRequest.Context.bReplaceExisting = false;
			TextureRequest.Selection.Domain = EMT2ImportDomain::Textures;
			TextureRequest.Selection.AssetRecords = TileRecords;
			FMT2TextureImporter TextureImporter;
			FMT2ImportResult TextureResult;
			TextureImporter.Import(TextureRequest, TextureResult);
			OutResult.CreatedPackages.Append(TextureResult.CreatedPackages);
			OutResult.Messages.Append(TextureResult.Messages);
		}

		TArray<FMT2MinimapTile> Tiles;
		for (const FMT2AssetRecord& Record : TileRecords)
		{
			const FString CellName = FPaths::GetCleanFilename(FPaths::GetPath(Record.AbsolutePath));
			FMT2MinimapTile& Tile = Tiles.AddDefaulted_GetRef();
			// Mirror the sector column to match the east-west flipped landscape, so minimap tiles line up
			// with the world markers (which follow the flipped world coordinates).
			Tile.Cell.X = (Map.MapSizeX - 1) - FCString::Atoi(*CellName.Left(3));
			Tile.Cell.Y = FCString::Atoi(*CellName.Right(3));
			Tile.Texture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(FMT2TextureImporter::BuildObjectPath(Context, Record)));
		}

		FActorSpawnParameters Parameters;
		Parameters.ObjectFlags = RF_Transactional;
		Parameters.OverrideLevel = Level;
		AMT2MapPresentationActor* PresentationActor = World->SpawnActor<AMT2MapPresentationActor>(
			FVector::ZeroVector, FRotator::ZeroRotator, Parameters);
		if (!PresentationActor) return;

		const float WorldSizeX = Map.MapSizeX * 128.0f * Map.CellScale;
		const float WorldSizeY = Map.MapSizeY * 128.0f * Map.CellScale;
		FVector2D DefaultTownSpawn(WorldSizeX * 0.5f, -WorldSizeY * 0.5f);
		TArray<FVector2D> EmpireTownSpawns;
		const bool bHasTownSpawns = LoadTownSpawnLocations(
			Context, Map, DefaultTownSpawn, EmpireTownSpawns);
		FString MapIdentifier = Map.MapName;
		int32 MapIndex = 0;
		const bool bHasMapIdentity = LoadServerMapIdentity(
			Context, Map.MapName, MapIdentifier, MapIndex);
		if (!bHasMapIdentity)
		{
			OutResult.AddWarning(FString::Printf(
				TEXT("Map '%s' is missing from the server map/index file; Map ID remains 0."),
				*Map.MapName), Map.MapDirectory);
		}
		FString LocalizedMapName;
		const bool bHasLocalizedMapName = LoadLocalizedMapName(
			Context, MapIdentifier, MapIndex, LocalizedMapName);
		PresentationActor->Modify();
		PresentationActor->Tags.AddUnique(ActorTag);
		PresentationActor->SetActorLabel(FString::Printf(TEXT("MT2_MapPresentation_%s"), *Map.MapName));
		PresentationActor->SetFolderPath(FName(*(TEXT("MT2/") + Map.MapName + TEXT("/Presentation"))));
		PresentationActor->Configure(MapIdentifier, MapIndex, FIntPoint(Map.MapSizeX, Map.MapSizeY),
			FVector2D(0.0f, -WorldSizeY), FVector2D(WorldSizeX, 0.0f), Tiles,
			DefaultTownSpawn, EmpireTownSpawns);
		FString AttributeError;
		FMT2MapAttributes Attributes;
		const FString AttributeFile = FindServerMapDirectory(FindServerLocaleRoot(Context), Map.MapName) / UMT2PathSettings::Path(TEXT("Part_server_attr"));
		const bool bServerAttributes = FMT2MapAttributeReader::Read(AttributeFile, Attributes, AttributeError) &&
			Attributes.Size == FIntPoint(Map.MapSizeX, Map.MapSizeY) * 512;
		if (!bServerAttributes && !FMT2MapAttributeReader::ReadClient(Map.MapDirectory,
			FIntPoint(Map.MapSizeX, Map.MapSizeY), Attributes, AttributeError))
		{
			OutResult.AddWarning(AttributeError, AttributeFile);
		}
		else
		{
			PresentationActor->Attributes = MoveTemp(Attributes);
			if (!bServerAttributes) { OutResult.AddWarning(TEXT("Used client attributes: server data unavailable or dimensions differ."), Map.MapDirectory); }
		}
		if (bHasLocalizedMapName)
		{
			PresentationActor->MapName = LocalizedMapName;
		}
		else
		{
			OutResult.AddWarning(FString::Printf(
				TEXT("No English locale_game.txt display name found for map '%s' (index %d)."),
				*MapIdentifier, MapIndex), Map.MapDirectory);
		}
		if (!PresentationActor->DefaultMusic)
		{
			FString BgmFileName;
			FString SettingsPath;
			if (LoadServerMapBgm(Context, MapIndex, BgmFileName, SettingsPath))
			{
				if (USoundBase* MapMusic = ResolveOrImportMapBgm(Context, BgmFileName, OutResult))
				{
					PresentationActor->DefaultMusic = MapMusic;
					OutResult.AddInfo(FString::Printf(
						TEXT("Configured map BGM '%s' from settings.lua for map index %d."),
						*BgmFileName, MapIndex), SettingsPath);
				}
				else
				{
					OutResult.AddWarning(FString::Printf(
						TEXT("Map index %d uses BGM '%s', but the source audio was not found."),
						MapIndex, *BgmFileName), SettingsPath);
				}
			}
		}
		if (!Map.EnvironmentPath.IsEmpty())
		{
			FMT2MapEnvironmentData SourceEnvironment;
			FString EnvironmentError;
			if (FMT2MapEnvironmentReader::Load(Map.EnvironmentPath, SourceEnvironment, EnvironmentError))
			{
				FMT2MapEnvironmentSettings Environment;
				Environment.SourceReference = Map.EnvironmentReference;
				Environment.bDirectionalLightEnabled = SourceEnvironment.bDirectionalLightEnabled;
				Environment.LightDirection = FVector(
					-SourceEnvironment.Direction.X, -SourceEnvironment.Direction.Y, SourceEnvironment.Direction.Z);
				Environment.DirectionalLightColor = SourceEnvironment.DirectionalDiffuse;
				Environment.DirectionalLightIntensity = UE_PI;
				Environment.AmbientLightColor = SourceEnvironment.MaterialAmbient;
				Environment.AmbientLightIntensity = FMath::Clamp(
					FMath::Max3(SourceEnvironment.MaterialEmissive.R,
						SourceEnvironment.MaterialEmissive.G, SourceEnvironment.MaterialEmissive.B)
					+ FMath::Max3(SourceEnvironment.DirectionalAmbient.R,
						SourceEnvironment.DirectionalAmbient.G, SourceEnvironment.DirectionalAmbient.B),
					0.0f, 2.0f);
				Environment.bFogEnabled = SourceEnvironment.bFogEnabled;
				Environment.FogNearDistance = SourceEnvironment.FogNearDistance;
				Environment.FogFarDistance = SourceEnvironment.FogFarDistance;
				Environment.FogColor = SourceEnvironment.FogColor;
				Environment.FogDensity = FMath::Clamp(
					6500.0f / FMath::Max(1.0f, SourceEnvironment.FogFarDistance - SourceEnvironment.FogNearDistance),
					0.001f, 10.0f);
				PresentationActor->ConfigureEnvironment(Environment);
				OutResult.AddInfo(FString::Printf(TEXT("Configured map environment from %s."),
					*Map.EnvironmentReference), Map.EnvironmentPath);
			}
			else
			{
				OutResult.AddWarning(EnvironmentError, Map.EnvironmentPath);
			}
		}
		else if (!Map.EnvironmentReference.IsEmpty())
		{
			OutResult.AddWarning(FString::Printf(TEXT("Map environment was not found: %s"),
				*Map.EnvironmentReference), Map.SettingPath);
		}
		PresentationActor->MarkPackageDirty();
		Level->MarkPackageDirty();
		World->MarkPackageDirty();
		OutResult.AddInfo(FString::Printf(
			TEXT("Configured map presentation for %s (index %d) with %d minimap tile(s), %s identity and %s town spawn data."),
			*MapIdentifier, MapIndex, Tiles.Num(), bHasMapIdentity ? TEXT("server") : TEXT("fallback"),
			bHasTownSpawns ? TEXT("server") : TEXT("center fallback")),
			Map.MapDirectory);
	}

	float ParseSpawnTime(const FString& Value)
	{
		float Seconds = 0.0f;
		int32 Number = 0;
		for (const TCHAR Character : Value)
		{
			if (FChar::IsDigit(Character))
			{
				Number = Number * 10 + (Character - TEXT('0'));
			}
			else if (Character == TEXT('h'))
			{
				Seconds += Number * 3600.0f;
				Number = 0;
			}
			else if (Character == TEXT('m'))
			{
				Seconds += Number * 60.0f;
				Number = 0;
			}
			else if (Character == TEXT('s'))
			{
				Seconds += Number;
				Number = 0;
			}
		}
		return FMath::Max(Seconds + Number, 0.0f);
	}

	void LoadMobGroups(const FString& Path, TMap<int32, FImportedMobGroup>& OutGroups)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path)) return;
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, true);
		int32 CurrentVnum = 0;
		TArray<int32> Members;
		for (FString Line : Lines)
		{
			Line.TrimStartAndEndInline();
			if (Line == TEXT("}"))
			{
				if (CurrentVnum > 0 && !Members.IsEmpty()) OutGroups.FindOrAdd(CurrentVnum).Members = Members;
				CurrentVnum = 0;
				Members.Reset();
				continue;
			}
			TArray<FString> Tokens;
			Line.ParseIntoArrayWS(Tokens);
			if (Tokens.Num() < 2) continue;
			if (Tokens[0].Equals(TEXT("Vnum"), ESearchCase::IgnoreCase))
			{
				CurrentVnum = FCString::Atoi(*Tokens.Last());
			}
			else if (Tokens[0].Equals(TEXT("Leader"), ESearchCase::IgnoreCase))
			{
				const int32 MobVnum = FCString::Atoi(*Tokens.Last());
				if (MobVnum > 0) Members.Add(MobVnum);
			}
			else if (FChar::IsDigit(Tokens[0][0]))
			{
				const int32 MobVnum = FCString::Atoi(*Tokens.Last());
				if (MobVnum > 0) Members.Add(MobVnum);
			}
		}
	}

	void LoadMobGroupChoices(const FString& Path, TMap<int32, TArray<FImportedMobGroupChoice>>& OutChoices)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path)) return;
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, true);
		int32 CurrentVnum = 0;
		for (FString Line : Lines)
		{
			Line.TrimStartAndEndInline();
			if (Line == TEXT("}"))
			{
				CurrentVnum = 0;
				continue;
			}
			TArray<FString> Tokens;
			Line.ParseIntoArrayWS(Tokens);
			if (Tokens.Num() < 2) continue;
			if (Tokens[0].Equals(TEXT("Vnum"), ESearchCase::IgnoreCase))
			{
				CurrentVnum = FCString::Atoi(*Tokens.Last());
			}
			else if (CurrentVnum > 0 && Tokens.Num() >= 3 && FChar::IsDigit(Tokens[0][0]))
			{
				FImportedMobGroupChoice& Choice = OutChoices.FindOrAdd(CurrentVnum).AddDefaulted_GetRef();
				Choice.GroupVnum = FCString::Atoi(*Tokens[1]);
				Choice.Weight = FMath::Max(FCString::Atoi(*Tokens[2]), 1);
			}
		}
	}

	FString FindServerLocaleRoot(const FMT2ImportContext& Context)
	{
		const TArray<FString> Candidates = {
			Context.SourceRoot / UMT2PathSettings::Path(TEXT("Part_server_src_share_locale_italy")),
			UMT2PathSettings::Path(TEXT("LegacyServerLocaleRoot"))
		};
		for (const FString& Candidate : Candidates)
		{
			if (FPaths::DirectoryExists(Candidate / UMT2PathSettings::Path(TEXT("Part_map")))) return Candidate;
		}
		return FString();
	}

	FString FindServerMapDirectory(const FString& LocaleRoot, const FString& MapName)
	{
		const FString Exact = LocaleRoot / UMT2PathSettings::Path(TEXT("Part_map")) / MapName;
		if (FPaths::DirectoryExists(Exact)) return Exact;
		TArray<FString> Directories;
		IFileManager::Get().FindFiles(Directories, *(LocaleRoot / UMT2PathSettings::Path(TEXT("Part_map")) / TEXT("*")), false, true);
		for (const FString& Directory : Directories)
		{
			if (Directory.Equals(MapName, ESearchCase::IgnoreCase)) return LocaleRoot / UMT2PathSettings::Path(TEXT("Part_map")) / Directory;
		}
		return FString();
	}

	bool LoadServerMapIdentity(
		const FMT2ImportContext& Context, const FString& SourceMapName,
		FString& OutMapIdentifier, int32& OutMapIndex)
	{
		OutMapIdentifier = SourceMapName;
		OutMapIndex = 0;
		TArray<FString> Lines;
		const FString IndexPath = FindServerLocaleRoot(Context) / UMT2PathSettings::Path(TEXT("Part_map")) / UMT2PathSettings::Path(TEXT("Part_index"));
		if (!FFileHelper::LoadFileToStringArray(Lines, *IndexPath))
		{
			return false;
		}
		for (FString Line : Lines)
		{
			int32 CommentIndex = Line.Find(TEXT("#"));
			if (CommentIndex != INDEX_NONE) Line.LeftInline(CommentIndex);
			TArray<FString> Parts;
			Line.ParseIntoArrayWS(Parts);
			if (Parts.Num() >= 2 && Parts[0].IsNumeric() &&
				Parts[1].Equals(SourceMapName, ESearchCase::IgnoreCase))
			{
				OutMapIndex = FCString::Atoi(*Parts[0]);
				OutMapIdentifier = Parts[1];
				return OutMapIndex > 0;
			}
		}
		return false;
	}

	bool LoadLocalizedMapName(
		const FMT2ImportContext& Context, const FString& MapIdentifier, int32 MapIndex,
		FString& OutMapName)
	{
		OutMapName.Reset();

		TMap<FString, FString> LocalizedTextByKey;
		TArray<FString> LocaleLines;
		const FString LocaleGamePath = Context.SourceRoot / UMT2PathSettings::Path(TEXT("Part_locale_en_locale_game"));
		if (!FFileHelper::LoadFileToStringArray(LocaleLines, *LocaleGamePath))
		{
			return false;
		}
		for (FString Line : LocaleLines)
		{
			Line.TrimStartAndEndInline();
			int32 SeparatorIndex = INDEX_NONE;
			if (!Line.FindChar(TEXT('\t'), SeparatorIndex) || SeparatorIndex <= 0)
			{
				continue;
			}
			FString Key = Line.Left(SeparatorIndex).TrimStartAndEnd();
			FString Value = Line.Mid(SeparatorIndex + 1).TrimStartAndEnd();
			int32 ExtraColumnIndex = INDEX_NONE;
			if (Value.FindChar(TEXT('\t'), ExtraColumnIndex))
			{
				Value.LeftInline(ExtraColumnIndex);
				Value.TrimEndInline();
			}
			if (Key.StartsWith(TEXT("MAP_")) && !Value.IsEmpty())
			{
				LocalizedTextByKey.Add(Key.ToUpper(), MoveTemp(Value));
			}
		}

		TMap<FString, FString> LocaleKeyByMapIdentifier;
		TMap<int32, FString> LocaleKeyByMapIndex;
		TArray<FString> LocaleInfoLines;
		if (FFileHelper::LoadFileToStringArray(
			LocaleInfoLines, *(Context.SourceRoot / UMT2PathSettings::Path(TEXT("Part_localeinfo")))))
		{
			enum class EMapDictionary : uint8 { None, Identifier, Index };
			EMapDictionary Dictionary = EMapDictionary::None;
			for (FString Line : LocaleInfoLines)
			{
				Line.TrimStartAndEndInline();
				if (Line.StartsWith(TEXT("MINIMAP_ZONE_NAME_DICT =")))
				{
					Dictionary = EMapDictionary::Identifier;
					continue;
				}
				if (Line.StartsWith(TEXT("MINIMAP_ZONE_NAME_DICT_BY_IDX =")))
				{
					Dictionary = EMapDictionary::Index;
					continue;
				}
				if (Dictionary == EMapDictionary::None)
				{
					continue;
				}
				if (Line.StartsWith(TEXT("}")))
				{
					Dictionary = EMapDictionary::None;
					continue;
				}

				const int32 ColonIndex = Line.Find(TEXT(":"));
				if (ColonIndex == INDEX_NONE)
				{
					continue;
				}
				FString Value = Line.Mid(ColonIndex + 1).TrimStartAndEnd();
				Value.RemoveFromEnd(TEXT(","));
				Value.TrimStartAndEndInline();
				if (!Value.StartsWith(TEXT("MAP_")))
				{
					continue;
				}

				if (Dictionary == EMapDictionary::Identifier)
				{
					const int32 FirstQuote = Line.Find(TEXT("\""));
					const int32 SecondQuote = FirstQuote == INDEX_NONE
						? INDEX_NONE : Line.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, FirstQuote + 1);
					if (FirstQuote != INDEX_NONE && SecondQuote > FirstQuote)
					{
						LocaleKeyByMapIdentifier.Add(
							Line.Mid(FirstQuote + 1, SecondQuote - FirstQuote - 1).ToLower(), Value.ToUpper());
					}
				}
				else
				{
					const FString IndexText = Line.Left(ColonIndex).TrimStartAndEnd();
					if (IndexText.IsNumeric())
					{
						LocaleKeyByMapIndex.Add(FCString::Atoi(*IndexText), Value.ToUpper());
					}
				}
			}
		}

		FString LocaleKey = LocaleKeyByMapIdentifier.FindRef(MapIdentifier.ToLower());
		if (!LocalizedTextByKey.Contains(LocaleKey) && MapIndex > 0)
		{
			LocaleKey = LocaleKeyByMapIndex.FindRef(MapIndex);
		}
		if (const FString* LocalizedName = LocalizedTextByKey.Find(LocaleKey))
		{
			OutMapName = *LocalizedName;
			return !OutMapName.IsEmpty();
		}
		return false;
	}

	bool LoadTownSpawnLocations(const FMT2ImportContext& Context, const FMT2MapTerrainInfo& Map,
		FVector2D& OutDefaultSpawn, TArray<FVector2D>& OutEmpireSpawns)
	{
		const FString LocaleRoot = FindServerLocaleRoot(Context);
		const FString MapDirectory = FindServerMapDirectory(LocaleRoot, Map.MapName);
		FString Text;
		if (MapDirectory.IsEmpty() ||
			!FFileHelper::LoadFileToString(Text, *(MapDirectory / UMT2PathSettings::Path(TEXT("Part_Town")))))
		{
			return false;
		}

		TArray<int32> Values;
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, true);
		for (FString Line : Lines)
		{
			int32 CommentIndex = INDEX_NONE;
			if (Line.FindChar(TEXT('#'), CommentIndex)) Line.LeftInline(CommentIndex);
			if ((CommentIndex = Line.Find(TEXT("//"))) != INDEX_NONE) Line.LeftInline(CommentIndex);
			TArray<FString> Tokens;
			Line.ParseIntoArrayWS(Tokens);
			for (const FString& Token : Tokens)
			{
				if (Token.IsNumeric()) Values.Add(FCString::Atoi(*Token));
			}
		}
		if (Values.Num() < 2) return false;

		const float WorldSizeX = Map.MapSizeX * 128.0f * Map.CellScale;
		auto ToUnrealLocation = [&Values, WorldSizeX](int32 PairIndex)
		{
			// Town.txt uses map-local Metin2 coordinates. Match terrain, objects and regen imports:
			// UE X is east-west mirrored and UE Y is the negated Metin2 Y.
			return FVector2D(
				WorldSizeX - Values[PairIndex * 2] * 100.0f,
				-Values[PairIndex * 2 + 1] * 100.0f);
		};
		OutDefaultSpawn = ToUnrealLocation(0);
		OutEmpireSpawns.Reset();
		const int32 EmpireCount = FMath::Min((Values.Num() - 2) / 2, 3);
		for (int32 EmpireIndex = 0; EmpireIndex < EmpireCount; ++EmpireIndex)
		{
			OutEmpireSpawns.Add(ToUnrealLocation(EmpireIndex + 1));
		}
		return true;
	}

	bool AddMobMember(FMT2MobSpawnVariant& Variant, int32 MobVnum, bool bLeader = false)
	{
		if (MobVnum <= 0) return false;
		FMT2MobSpawnMember& Member = Variant.Members.AddDefaulted_GetRef();
		Member.MobVnum = MobVnum;
		Member.bLeader = bLeader;
		return true;
	}

	void FillGroupVariant(
		FMT2MobSpawnVariant& Variant, int32 GroupVnum, const TMap<int32, FImportedMobGroup>& Groups)
	{
		if (const FImportedMobGroup* Group = Groups.Find(GroupVnum))
		{
			for (int32 MemberIndex = 0; MemberIndex < Group->Members.Num(); ++MemberIndex)
			{
				AddMobMember(Variant, Group->Members[MemberIndex], MemberIndex == 0);
			}
		}
	}

	EMT2MobSpawnSource SpawnSourceFromFile(const FString& Path)
	{
		const FString Name = FPaths::GetCleanFilename(Path).ToLower();
		if (Name == TEXT("npc.txt")) return EMT2MobSpawnSource::Npc;
		if (Name == TEXT("stone.txt")) return EMT2MobSpawnSource::Stone;
		if (Name == TEXT("boss.txt")) return EMT2MobSpawnSource::Boss;
		return EMT2MobSpawnSource::Regen;
	}

	void ParseRegenFile(
		const FString& Path, const FMT2MapTerrainInfo& Map,
		const TMap<int32, FImportedMobGroup>& Groups,
		const TMap<int32, TArray<FImportedMobGroupChoice>>& GroupChoices,
		TArray<FMT2MobSpawnEntry>& OutEntries,
		TArray<FMT2MobSpawnExclusion>& OutExclusions,
		TArray<FString>& OutWarnings)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path)) return;
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, true);
		for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
		{
			FString Line = Lines[LineIndex].TrimStartAndEnd();
			if (Line.IsEmpty() || Line.StartsWith(TEXT("//"))) continue;
			int32 CommentIndex = INDEX_NONE;
			if (Line.FindChar(TEXT('/'), CommentIndex) && Line.Mid(CommentIndex).StartsWith(TEXT("//")))
			{
				Line = Line.Left(CommentIndex).TrimEnd();
			}
			TArray<FString> Tokens;
			Line.ParseIntoArrayWS(Tokens);
			if (Tokens.IsEmpty()) continue;

			const FString Type = Tokens[0].ToLower();
			if (Type == TEXT("e"))
			{
				if (Tokens.Num() < 6)
				{
					OutWarnings.Add(FString::Printf(TEXT("Malformed exclusion at %s:%d"), *Path, LineIndex + 1));
					continue;
				}
				FMT2MobSpawnExclusion& Exclusion = OutExclusions.AddDefaulted_GetRef();
				Exclusion.SourceId = FName(*FString::Printf(TEXT("%s:%d"), *FPaths::GetCleanFilename(Path), LineIndex + 1));
				// Mirror X to match the east-west flipped landscape (UE X = WorldSizeX - Metin2X).
				const float ExclusionWorldSizeX = Map.MapSizeX * 128.0f * Map.CellScale;
				Exclusion.Center = FVector2D(
					ExclusionWorldSizeX - FCString::Atof(*Tokens[1]) * 100.0f, -FCString::Atof(*Tokens[2]) * 100.0f);
				Exclusion.HorizontalExtent = FVector2D(FCString::Atof(*Tokens[3]) * 100.0f, FCString::Atof(*Tokens[4]) * 100.0f);
				continue;
			}
			if (Tokens.Num() < 11)
			{
				OutWarnings.Add(FString::Printf(TEXT("Malformed spawn row at %s:%d"), *Path, LineIndex + 1));
				continue;
			}
			if (Type != TEXT("m") && Type != TEXT("g") && Type != TEXT("ga")
				&& Type != TEXT("r") && Type != TEXT("s"))
			{
				OutWarnings.Add(FString::Printf(TEXT("Unsupported spawn type '%s' at %s:%d"), *Type, *Path, LineIndex + 1));
				continue;
			}
			const int32 Vnum = FCString::Atoi(*Tokens[10]);
			FMT2MobSpawnEntry Entry;
			Entry.SourceId = FName(*FString::Printf(TEXT("%s:%d"), *FPaths::GetCleanFilename(Path), LineIndex + 1));
			Entry.Source = SpawnSourceFromFile(Path);
			Entry.SourceVnum = Vnum;
			Entry.ZSection = FCString::Atoi(*Tokens[5]);
			// Mirror X to match the east-west flipped landscape (UE X = WorldSizeX - Metin2X), so mobs,
			// NPCs and metins spawn on the correct side of the terrain.
			const float SpawnWorldSizeX = Map.MapSizeX * 128.0f * Map.CellScale;
			Entry.Center = FVector(
				SpawnWorldSizeX - FCString::Atof(*Tokens[1]) * 100.0f, -FCString::Atof(*Tokens[2]) * 100.0f, 0.0f);
			Entry.HorizontalExtent = FVector2D(FCString::Atof(*Tokens[3]) * 100.0f, FCString::Atof(*Tokens[4]) * 100.0f);
			Entry.RespawnDelay = ParseSpawnTime(Tokens[7]);
			Entry.SpawnChancePercent = FMath::Clamp(FCString::Atof(*Tokens[8]), 0.0f, 100.0f);
			Entry.DesiredGroupCount = FMath::Max(FCString::Atoi(*Tokens[9]), 0);
			Entry.bForceAggressive = Type == TEXT("ga");
			const int32 Direction = FCString::Atoi(*Tokens[6]);
			Entry.Yaw = Direction == 0
				? -1.0f
				: FMath::Fmod(360.0f - (Direction - 1) * 45.0f + 180.0f, 360.0f);

			if (Type == TEXT("s"))
			{
				Entry.Center = FVector(
					Map.MapSizeX * 128.0f * Map.CellScale * 0.5f,
					-Map.MapSizeY * 128.0f * Map.CellScale * 0.5f, 0.0f);
				Entry.HorizontalExtent = FVector2D(Entry.Center.X, FMath::Abs(Entry.Center.Y));
			}

			if (Type == TEXT("m") || Type == TEXT("s"))
			{
				Entry.Type = Type == TEXT("s") ? EMT2MobSpawnType::Anywhere : EMT2MobSpawnType::Mob;
				FMT2MobSpawnVariant& Variant = Entry.Variants.AddDefaulted_GetRef();
				AddMobMember(Variant, Vnum, true);
			}
			else if (Type == TEXT("g") || Type == TEXT("ga"))
			{
				Entry.Type = Type == TEXT("ga") ? EMT2MobSpawnType::AggressiveGroup : EMT2MobSpawnType::Group;
				FMT2MobSpawnVariant& Variant = Entry.Variants.AddDefaulted_GetRef();
				Variant.GroupVnum = Vnum;
				FillGroupVariant(Variant, Vnum, Groups);
				if (Variant.Members.IsEmpty())
				{
					OutWarnings.Add(FString::Printf(TEXT("Missing or empty group VNUM %d at %s:%d"), Vnum, *Path, LineIndex + 1));
				}
			}
			else if (Type == TEXT("r"))
			{
				Entry.Type = EMT2MobSpawnType::GroupGroup;
				if (const TArray<FImportedMobGroupChoice>* Choices = GroupChoices.Find(Vnum))
				{
					for (const FImportedMobGroupChoice& Choice : *Choices)
					{
						FMT2MobSpawnVariant& Variant = Entry.Variants.AddDefaulted_GetRef();
						Variant.Weight = Choice.Weight;
						Variant.GroupVnum = Choice.GroupVnum;
						FillGroupVariant(Variant, Choice.GroupVnum, Groups);
					}
				}
				else
				{
					OutWarnings.Add(FString::Printf(TEXT("Missing group-group VNUM %d at %s:%d"), Vnum, *Path, LineIndex + 1));
				}
			}
			Entry.Variants.RemoveAll([](const FMT2MobSpawnVariant& Variant) { return Variant.Members.IsEmpty(); });
			if (!Entry.Variants.IsEmpty()) OutEntries.Add(MoveTemp(Entry));
		}
	}

	int32 ImportPortalsForMap(
		const FMT2ImportContext& Context,
		const FMT2MapTerrainInfo& Map,
		const FString& LocaleRoot,
		TArray<FMT2MobSpawnEntry>& InOutEntries,
		FMT2ImportResult& OutResult)
	{
		if (!GEditor)
		{
			return 0;
		}
		UWorld* World = GEditor->GetEditorWorldContext().World();
		ULevel* Level = World ? World->GetCurrentLevel() : nullptr;
		if (!World || !Level)
		{
			return 0;
		}

		FMT2MobProtoReadResult Proto;
		FString ProtoError;
		if (!FMT2MobProtoReader::Read(Context.SourceRoot / UMT2PathSettings::Path(TEXT("Part_locale_en_mob_proto")), Proto, ProtoError))
		{
			OutResult.AddWarning(FString::Printf(
				TEXT("Could not read mob_proto while importing portals for %s: %s"), *Map.MapName, *ProtoError));
			return 0;
		}

		TMap<int32, const FMT2MobDefinition*> PortalDefinitions;
		for (const FMT2MobDefinition& Definition : Proto.Definitions)
		{
			if (Definition.Type == EMT2MobType::Warp || Definition.Type == EMT2MobType::Goto)
			{
				PortalDefinitions.Add(Definition.Vnum, &Definition);
			}
		}
		if (PortalDefinitions.IsEmpty())
		{
			return 0;
		}

		TArray<FServerMapBounds> MapBounds;
		LoadAllServerMapBounds(LocaleRoot, Context.ScanResult, MapBounds);
		const UMT2VnumRegistry* Registry = LoadObject<UMT2VnumRegistry>(
			nullptr, UMT2PathSettings::Path(TEXT("VnumRegistry")));
		int32 PlacedCount = 0;
		int32 UpdatedCount = 0;
		const FScopedTransaction Transaction(LOCTEXT("ImportPortals", "Import Metin2 Portals"));
		World->Modify();
		Level->Modify();

		for (int32 EntryIndex = InOutEntries.Num() - 1; EntryIndex >= 0; --EntryIndex)
		{
			const FMT2MobSpawnEntry& Entry = InOutEntries[EntryIndex];
			if (Entry.Variants.Num() != 1 || Entry.Variants[0].Members.Num() != 1)
			{
				continue;
			}
			const int32 PortalVnum = Entry.Variants[0].Members[0].MobVnum;
			const FMT2MobDefinition* const* DefinitionPtr = PortalDefinitions.Find(PortalVnum);
			if (!DefinitionPtr || !*DefinitionPtr)
			{
				continue;
			}

			const FMT2MobDefinition& Definition = **DefinitionPtr;
			FString DestinationMap;
			FVector2D DestinationLocation = FVector2D::ZeroVector;
			FString PortalDisplayName;
			const bool bResolvedTarget = ParsePortalTarget(
				Definition, Map, MapBounds, DestinationMap, DestinationLocation, PortalDisplayName);
			if (!bResolvedTarget)
			{
				OutResult.AddWarning(FString::Printf(
					TEXT("Placed portal VNUM %d from %s, but its encoded destination '%s' could not be resolved. Configure it manually."),
					PortalVnum, *Entry.SourceId.ToString(),
					*(!Definition.DisplayName.IsEmpty() ? Definition.DisplayName : Definition.InternalName)));
				PortalDisplayName = !Definition.DisplayName.IsEmpty() ? Definition.DisplayName : Definition.InternalName;
			}

			const FName PortalTag(*FString::Printf(
				TEXT("MT2UE_Portal:%s:%s"), *Map.MapName.ToLower(), *Entry.SourceId.ToString().ToLower()));
			AMT2Portal* Portal = nullptr;
			for (AActor* Actor : Level->Actors)
			{
				if (AMT2Portal* Candidate = Cast<AMT2Portal>(Actor); Candidate && Candidate->Tags.Contains(PortalTag))
				{
					Portal = Candidate;
					break;
				}
			}

			const bool bExistingPortal = Portal != nullptr;
			const FVector2D PortalXY(Entry.Center.X, Entry.Center.Y);
			const FVector PortalLocation(PortalXY.X, PortalXY.Y, FindPortalGroundZ(World, PortalXY));
			const float PortalYaw = Entry.Yaw < 0.0f ? 0.0f : Entry.Yaw;
			if (!Portal)
			{
				FActorSpawnParameters Parameters;
				Parameters.ObjectFlags = RF_Transactional;
				Parameters.OverrideLevel = Level;
				Portal = World->SpawnActor<AMT2Portal>(PortalLocation, FRotator(0.0f, PortalYaw, 0.0f), Parameters);
			}
			if (!Portal)
			{
				OutResult.AddWarning(FString::Printf(
					TEXT("Failed to place portal VNUM %d for %s."), PortalVnum, *Map.MapName));
				continue;
			}

			Portal->Modify();
			Portal->Tags.AddUnique(PortalTag);
			Portal->SetActorLocationAndRotation(PortalLocation, FRotator(0.0f, PortalYaw, 0.0f));
			Portal->MapName = DestinationMap;
			Portal->DestinationLocation = DestinationLocation;
			Portal->bUseCity = false;
			Portal->DisplayName = PortalDisplayName;
			Portal->SetActorLabel(FString::Printf(
				TEXT("MT2_Portal_%d_%s"), PortalVnum, *Entry.SourceId.ToString().Replace(TEXT(":"), TEXT("_"))));
			Portal->SetFolderPath(FName(*(TEXT("MT2/") + Map.MapName + TEXT("/Gameplay/Portals"))));

			if (Registry)
			{
				if (const TSoftClassPtr<AMT2Mob>* MobClassPtr = Registry->GetMobClasses().Find(PortalVnum))
				{
					if (UClass* MobClass = MobClassPtr->LoadSynchronous())
					{
						AMT2Mob* MobDefaults = MobClass->GetDefaultObject<AMT2Mob>();
						USkeletalMeshComponent* MobMesh = MobDefaults ? MobDefaults->GetMesh() : nullptr;
						if (MobMesh && MobMesh->GetSkeletalMeshAsset())
						{
							Portal->ConfigureSkeletalVisual(
								MobMesh->GetSkeletalMeshAsset(), MobMesh->GetAnimClass(), MobMesh->GetRelativeTransform());
						}
					}
				}
			}

			Portal->PostEditChange();
			Portal->MarkPackageDirty();
			OutResult.CreatedPackages.Add(FString::Printf(TEXT("WorldActor:%s"), *Portal->GetActorLabel()));
			bExistingPortal ? ++UpdatedCount : ++PlacedCount;
			InOutEntries.RemoveAt(EntryIndex);
		}

		if (PlacedCount > 0 || UpdatedCount > 0)
		{
			Level->MarkPackageDirty();
			World->MarkPackageDirty();
			OutResult.AddInfo(FString::Printf(
				TEXT("Configured %d portal(s) for %s (%d new, %d updated); portal spawns were removed from the mob scheduler."),
				PlacedCount + UpdatedCount, *Map.MapName, PlacedCount, UpdatedCount));
		}
		return PlacedCount + UpdatedCount;
	}

	void ImportMobSpawnsForMap(
		const FMT2ImportContext& Context, const FMT2MapTerrainInfo& Map, FMT2ImportResult& OutResult)
	{
		if (!GEditor) return;
		UWorld* World = GEditor->GetEditorWorldContext().World();
		ULevel* Level = World ? World->GetCurrentLevel() : nullptr;
		if (!World || !Level) return;

		const FString LocaleRoot = FindServerLocaleRoot(Context);
		const FString MapDirectory = FindServerMapDirectory(LocaleRoot, Map.MapName);
		if (LocaleRoot.IsEmpty() || MapDirectory.IsEmpty())
		{
			OutResult.AddWarning(FString::Printf(TEXT("No server spawn directory found for map %s."), *Map.MapName));
			return;
		}

		TMap<int32, FImportedMobGroup> Groups;
		TMap<int32, TArray<FImportedMobGroupChoice>> GroupChoices;
		LoadMobGroups(LocaleRoot / UMT2PathSettings::Path(TEXT("Part_group")), Groups);
		LoadMobGroupChoices(LocaleRoot / UMT2PathSettings::Path(TEXT("Part_group_group")), GroupChoices);

		TArray<FMT2MobSpawnEntry> Entries;
		TArray<FMT2MobSpawnExclusion> Exclusions;
		TArray<FString> SpawnWarnings;
		for (const TCHAR* FileName : { TEXT("regen.txt"), TEXT("npc.txt"), TEXT("boss.txt"), TEXT("stone.txt") })
		{
			ParseRegenFile(MapDirectory / FileName, Map, Groups, GroupChoices, Entries, Exclusions, SpawnWarnings);
		}
		for (const FString& Warning : SpawnWarnings)
		{
			OutResult.AddWarning(Warning, MapDirectory);
		}

		const int32 ImportedPortalCount = ImportPortalsForMap(Context, Map, LocaleRoot, Entries, OutResult);

		if (const UMT2VnumRegistry* Registry = LoadObject<UMT2VnumRegistry>(
			nullptr, UMT2PathSettings::Path(TEXT("VnumRegistry"))))
		{
			TSet<int32> MissingVnums;
			for (const FMT2MobSpawnEntry& Entry : Entries)
			{
				for (const FMT2MobSpawnVariant& Variant : Entry.Variants)
				{
					for (const FMT2MobSpawnMember& Member : Variant.Members)
					{
						if (!Registry->GetMobClasses().Contains(Member.MobVnum))
						{
							MissingVnums.Add(Member.MobVnum);
						}
					}
				}
			}
			if (!MissingVnums.IsEmpty())
			{
				TArray<int32> SortedMissing = MissingVnums.Array();
				SortedMissing.Sort();
				TArray<FString> Values;
				for (int32 Index = 0; Index < FMath::Min(SortedMissing.Num(), 32); ++Index)
				{
					Values.Add(FString::FromInt(SortedMissing[Index]));
				}
				OutResult.AddWarning(FString::Printf(
					TEXT("Map %s references %d mob VNUM(s) missing from DA_MT2VnumRegistry: %s%s"),
					*Map.MapName, SortedMissing.Num(), *FString::Join(Values, TEXT(", ")),
					SortedMissing.Num() > Values.Num() ? TEXT(", ...") : TEXT("")), MapDirectory);
			}
		}
		if (Entries.IsEmpty())
		{
			if (ImportedPortalCount == 0)
			{
				OutResult.AddWarning(FString::Printf(TEXT("No resolvable mob spawn entries found for %s."), *Map.MapName), MapDirectory);
			}
			else
			{
				OutResult.AddInfo(FString::Printf(
					TEXT("Map %s contains portals but no scheduled mob/NPC spawn entries."), *Map.MapName), MapDirectory);
			}
			return;
		}

		const FName ActorTag(*(TEXT("MT2UE_MobSpawns:") + Map.MapName.ToLower()));
		AMT2MobSpawnActor* SpawnActor = nullptr;
		for (AActor* Actor : Level->Actors)
		{
			if (AMT2MobSpawnActor* Candidate = Cast<AMT2MobSpawnActor>(Actor); Candidate && Candidate->Tags.Contains(ActorTag))
			{
				SpawnActor = Candidate;
				break;
			}
		}
		const FScopedTransaction Transaction(LOCTEXT("ImportMobSpawns", "Import Metin2 Mob Spawns"));
		World->Modify();
		Level->Modify();
		if (!SpawnActor)
		{
			FActorSpawnParameters Parameters;
			Parameters.ObjectFlags = RF_Transactional;
			Parameters.OverrideLevel = Level;
			SpawnActor = World->SpawnActor<AMT2MobSpawnActor>(FVector::ZeroVector, FRotator::ZeroRotator, Parameters);
		}
		if (!SpawnActor) return;
		SpawnActor->Modify();
		SpawnActor->Tags.AddUnique(ActorTag);
		SpawnActor->SetActorLabel(FString::Printf(TEXT("MT2_MobSpawns_%s"), *Map.MapName));
		SpawnActor->SetFolderPath(FName(*(TEXT("MT2/") + Map.MapName + TEXT("/Gameplay"))));
		SpawnActor->GetMobSpawnComponent()->SetSpawnEntries(Entries);
		SpawnActor->GetMobSpawnComponent()->SetSpawnExclusions(Exclusions);
		SpawnActor->MarkPackageDirty();
		Level->MarkPackageDirty();
		World->MarkPackageDirty();
		OutResult.CreatedPackages.Add(FString::Printf(TEXT("WorldActor:MT2_MobSpawns_%s"), *Map.MapName));
		OutResult.AddInfo(FString::Printf(
			TEXT("Configured %d spawn regions and %d exclusion regions for %s (%d warning(s))."),
			Entries.Num(), Exclusions.Num(), *Map.MapName, SpawnWarnings.Num()), MapDirectory);
	}

	FName BuildLandscapeTag(const FString& MapName)
	{
		return FName(*(TEXT("MT2UE_Landscape:") + MapName.ToLower()));
	}

	const FName SmoothedWeightmapsTag(TEXT("MT2UE_SmoothedWeightmaps_v1"));

	void BuildCombinedLandscapeWeightData(
		const FMT2ImportContext& Context, const FMT2MapTerrainInfo& Map,
		const FMT2PreparedMapTerrain& Prepared, TMap<FName, TArray<uint8>>& OutWeightDataByLayer)
	{
		OutWeightDataByLayer.Reset();
		for (const TPair<uint8, TArray<uint8>>& Pair : Prepared.LandscapeWeightDataByTile)
		{
			const FName LayerName = FMT2LandscapeMaterialImporter::BuildLayerNameForTile(Context, Map, Pair.Key);
			TArray<uint8>* ExistingData = OutWeightDataByLayer.Find(LayerName);
			if (!ExistingData)
			{
				OutWeightDataByLayer.Add(LayerName, Pair.Value);
				continue;
			}

			const int32 PixelCount = FMath::Min(ExistingData->Num(), Pair.Value.Num());
			for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
			{
				(*ExistingData)[PixelIndex] = static_cast<uint8>(FMath::Min(
					static_cast<int32>((*ExistingData)[PixelIndex]) + static_cast<int32>(Pair.Value[PixelIndex]), 255));
			}
		}
	}

	bool RefreshLandscapeWeightData(
		ALandscape* Landscape, const FMT2ImportContext& Context, const FMT2MapTerrainInfo& Map,
		const FMT2PreparedMapTerrain& Prepared, FMT2ImportResult& OutResult)
	{
		ULandscapeInfo* LandscapeInfo = Landscape ? Landscape->GetLandscapeInfo() : nullptr;
		if (!LandscapeInfo)
		{
			OutResult.AddWarning(FString::Printf(TEXT("Cannot refresh smoothed weights for %s: landscape info is unavailable."), *Map.MapName));
			return false;
		}

		int32 MinX = 0;
		int32 MinY = 0;
		int32 MaxX = 0;
		int32 MaxY = 0;
		if (!LandscapeInfo->GetLandscapeExtent(MinX, MinY, MaxX, MaxY)
			|| MaxX - MinX + 1 != Prepared.LandscapeWidth
			|| MaxY - MinY + 1 != Prepared.LandscapeHeight)
		{
			OutResult.AddWarning(FString::Printf(
				TEXT("Cannot refresh smoothed weights for %s: imported landscape dimensions do not match."), *Map.MapName));
			return false;
		}

		TMap<FName, TArray<uint8>> WeightDataByLayer;
		BuildCombinedLandscapeWeightData(Context, Map, Prepared, WeightDataByLayer);
		FLandscapeEditDataInterface LandscapeEdit(LandscapeInfo);
		for (const TPair<FName, TArray<uint8>>& Pair : WeightDataByLayer)
		{
			ULandscapeLayerInfoObject* LayerInfo = LandscapeInfo->GetLayerInfoByName(Pair.Key, Landscape);
			if (!LayerInfo || Pair.Value.Num() != Prepared.LandscapeWidth * Prepared.LandscapeHeight)
			{
				OutResult.AddWarning(FString::Printf(
					TEXT("Skipped smoothed weight refresh for missing or invalid layer %s."), *Pair.Key.ToString()));
				continue;
			}
			LandscapeEdit.SetAlphaData(
				LayerInfo, MinX, MinY, MaxX, MaxY, Pair.Value.GetData(), Prepared.LandscapeWidth,
				ELandscapeLayerPaintingRestriction::None, false, false);
		}
		LandscapeEdit.Flush();
		Landscape->Tags.AddUnique(SmoothedWeightmapsTag);
		Landscape->MarkPackageDirty();
		OutResult.AddInfo(FString::Printf(TEXT("Refreshed smoothed landscape layer weights for %s."), *Map.MapName));
		return true;
	}

	ULevel* GetCurrentTargetLevel()
	{
		if (!GEditor)
		{
			return nullptr;
		}

		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (!World)
		{
			return nullptr;
		}

		ULevel* TargetLevel = World->GetCurrentLevel();
		return TargetLevel ? TargetLevel : World->PersistentLevel.Get();
	}

	ALandscape* FindImportedLandscape(const ULevel* Level, const FString& MapName)
	{
		if (!Level)
		{
			return nullptr;
		}

		const FName LandscapeTag = BuildLandscapeTag(MapName);
		const FString LandscapeLabel = FString::Printf(TEXT("MT2_Landscape_%s"), *MapName);
		for (AActor* Actor : Level->Actors)
		{
			ALandscape* Landscape = Cast<ALandscape>(Actor);
			if (Landscape && (Landscape->Tags.Contains(LandscapeTag) || Landscape->GetActorLabel().Equals(LandscapeLabel, ESearchCase::IgnoreCase)))
			{
				return Landscape;
			}
		}

		return nullptr;
	}

	void ImportLandscapeTextures(const FMT2ImportContext& Context, const FMT2MapTerrainInfo& Map, FMT2ImportResult& OutResult)
	{
		if (!Context.bImportReferencedTextures)
		{
			return;
		}
		if (!Context.ScanResult)
		{
			OutResult.AddWarning(FString::Printf(TEXT("Cannot import referenced terrain textures for %s because Context.ScanResult is not available."), *Map.MapName), Map.TextureSetPath);
			return;
		}

		TMap<FString, FMT2AssetRecord> UniqueTerrainTextures;
		for (const FString& TextureReference : Map.TextureSetTextures)
		{
			if (const FMT2AssetRecord* TextureRecord = FMT2TextureImporter::FindRecordForReference(*Context.ScanResult, TextureReference))
			{
				UniqueTerrainTextures.FindOrAdd(TextureRecord->AbsolutePath, *TextureRecord);
				if (Context.bEnableDebugLogs)
				{
					OutResult.AddInfo(FString::Printf(TEXT("[Debug][MapTerrain] Terrain texture ref '%s' -> '%s' (%s)."),
						*TextureReference,
						*TextureRecord->VirtualPath,
						*TextureRecord->AbsolutePath),
						Map.TextureSetPath);
				}
			}
			else
			{
				OutResult.AddWarning(FString::Printf(TEXT("Could not resolve terrain texture reference for %s: %s"), *Map.MapName, *TextureReference), Map.TextureSetPath);
			}
		}

		if (UniqueTerrainTextures.Num() == 0)
		{
			if (Map.TextureSetTextures.Num() > 0)
			{
				OutResult.AddWarning(FString::Printf(TEXT("Texture set for %s had %d reference(s), but none resolved to scanned texture files."), *Map.MapName, Map.TextureSetTextures.Num()), Map.TextureSetPath);
			}
			return;
		}

		FMT2ImportRequest TextureRequest;
		TextureRequest.Context = Context;
		TextureRequest.Context.bReplaceExisting = false;
		TextureRequest.Selection.Domain = EMT2ImportDomain::Textures;
		UniqueTerrainTextures.GenerateValueArray(TextureRequest.Selection.AssetRecords);
		FMT2TextureImporter TextureImporter;
		FMT2ImportResult TextureResult;
		TextureImporter.Import(TextureRequest, TextureResult);
		OutResult.CreatedPackages.Append(TextureResult.CreatedPackages);
		OutResult.Messages.Append(TextureResult.Messages);
	}
}

FMT2MapTerrainImporter::FMT2MapTerrainImporter()
	: FMT2ImporterBase(EMT2ImportDomain::MapTerrains, TEXT("MapTerrainImporter"))
{
}

bool FMT2MapTerrainImporter::Discover(const FMT2ImportContext& Context, const FMT2AssetScanResult& ScanResult, FMT2ImportDiscovery& OutDiscovery) const
{
	FString Error;
	OutDiscovery.Domain = GetDomain();
	if (!FMT2MapTerrainBuilder::DiscoverMaps(ScanResult, OutDiscovery.MapTerrains, Error))
	{
		FMT2ImportMessage Message;
		Message.Severity = EMT2ImportSeverity::Warning;
		Message.Text = Error;
		OutDiscovery.Messages.Add(MoveTemp(Message));
		return false;
	}

	OutDiscovery.ItemsDiscovered = OutDiscovery.MapTerrains.Num();
	for (const FMT2MapTerrainInfo& MapTerrain : OutDiscovery.MapTerrains)
	{
		OutDiscovery.EntryNames.Add(MapTerrain.MapName);
	}
	OutDiscovery.EntryNames.Sort();
	return true;
}

bool FMT2MapTerrainImporter::Import(const FMT2ImportRequest& Request, FMT2ImportResult& OutResult)
{
	if (!CanImport(Request, OutResult))
	{
		return false;
	}

	if (Request.Selection.MapTerrains.Num() == 0)
	{
		OutResult.AddWarning(TEXT("No map terrain records were selected."));
		OutResult.bSucceeded = true;
		return true;
	}

	const int32 MaxItems = Request.MaxItems > 0 ? Request.MaxItems : Request.Selection.MapTerrains.Num();
	const int32 MapsToProcess = FMath::Min(MaxItems, Request.Selection.MapTerrains.Num());
	FScopedSlowTask Progress(static_cast<float>(MapsToProcess * 8), LOCTEXT("ImportMapProgress", "Importing Metin2 map..."));
	Progress.MakeDialog(true, false);

	FMT2ImportContext EffectiveContext = Request.Context;
	EffectiveContext.bReplaceExisting = false;
	const TFunction<bool()> OriginalShouldCancel = EffectiveContext.ShouldCancel;
	EffectiveContext.ShouldCancel = [OriginalShouldCancel, &Progress]()
	{
		return Progress.ShouldCancel() || (OriginalShouldCancel && OriginalShouldCancel());
	};

	const FString OutputRoot = FMT2StaticMeshImporter::GetWorkingRoot(EffectiveContext) / UMT2PathSettings::Path(TEXT("Part_LandscapeSources"));
	FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*OutputRoot);

	FMT2ResolvedWorldResult SharedResolvedWorld;
	bool bAttemptedWorldResolve = EffectiveContext.ResolvedWorld != nullptr;
	int32 ConsideredCount = 0;

	auto EnterStage = [&Progress](const FString& MapName, const FString& Stage)
	{
		Progress.EnterProgressFrame(1.0f, FText::Format(
			LOCTEXT("MapImportStageFormat", "{0}: {1}"),
			FText::FromString(MapName),
			FText::FromString(Stage)));
	};

	for (const FMT2MapTerrainInfo& Map : Request.Selection.MapTerrains)
	{
		if (ConsideredCount >= MapsToProcess || EffectiveContext.IsStopRequested())
		{
			if (EffectiveContext.IsStopRequested())
			{
				OutResult.AddWarning(TEXT("Map terrain import stopped by user."));
			}
			break;
		}

		ConsideredCount++;
		OutResult.ItemsDiscovered++;

		if (EffectiveContext.bDryRun)
		{
			EnterStage(Map.MapName, TEXT("Landscape materials (dry run)"));
			EnterStage(Map.MapName, TEXT("Landscape heightmap (dry run)"));
			EnterStage(Map.MapName, TEXT("Create landscape with material (dry run)"));
			EnterStage(Map.MapName, TEXT("Build object property-to-mesh cache (dry run)"));
			EnterStage(Map.MapName, TEXT("Import missing meshes (dry run)"));
			EnterStage(Map.MapName, TEXT("Place meshes in landscape (dry run)"));
			EnterStage(Map.MapName, TEXT("Configure mob spawns (dry run)"));
			EnterStage(Map.MapName, TEXT("Configure minimap (dry run)"));
			OutResult.ItemsSkipped++;
			OutResult.AddInfo(FString::Printf(TEXT("Dry run: would import map %s in seven stages."), *Map.MapName), Map.SettingPath);
			continue;
		}

		ALandscape* ExistingLandscape = EffectiveContext.bCreateLandscapeActors
			? FindImportedLandscape(GetCurrentTargetLevel(), Map.MapName)
			: nullptr;
		const bool bLandscapeAlreadyImported = ExistingLandscape != nullptr;
		const bool bNeedsWeightmapSmoothing =
			bLandscapeAlreadyImported && !ExistingLandscape->Tags.Contains(SmoothedWeightmapsTag);

		EnterStage(Map.MapName, bLandscapeAlreadyImported
			? TEXT("Refresh landscape materials")
			: TEXT("Landscape materials"));
		ImportLandscapeTextures(EffectiveContext, Map, OutResult);

		EnterStage(Map.MapName, bLandscapeAlreadyImported && !bNeedsWeightmapSmoothing
			? TEXT("Landscape heightmap already imported - skipping")
			: bNeedsWeightmapSmoothing
				? TEXT("Prepare smoothed landscape weights")
			: TEXT("Landscape heightmap"));
		FMT2PreparedMapTerrain Prepared;
		bool bPreparedLandscape = bLandscapeAlreadyImported && !bNeedsWeightmapSmoothing;
		if (!bLandscapeAlreadyImported || bNeedsWeightmapSmoothing)
		{
			FString Error;
			OutResult.AddInfo(FString::Printf(TEXT("Preparing landscape sources for map %s."), *Map.MapName), Map.SettingPath);
			bPreparedLandscape = FMT2MapTerrainBuilder::PrepareLandscapeSources(Map, OutputRoot, Prepared, Error);
			if (!bPreparedLandscape)
			{
				OutResult.ItemsSkipped++;
				OutResult.AddWarning(Error, Map.SettingPath);
			}
			else
			{
				OutResult.CreatedFiles.Add(Prepared.OutputDirectory);
				OutResult.CreatedFiles.Add(Prepared.HeightmapPath);
				OutResult.CreatedFiles.Add(Prepared.LandscapeHeightmapPath);
				OutResult.CreatedFiles.Add(Prepared.TileIndexPath);
				for (const FString& WeightmapPath : Prepared.WeightmapPaths)
				{
					OutResult.CreatedFiles.Add(WeightmapPath);
				}
			}
		}

		EnterStage(Map.MapName, bLandscapeAlreadyImported
			? TEXT("Landscape already exists - reusing current level")
			: TEXT("Create landscape with material"));
		if (bLandscapeAlreadyImported)
		{
			ExistingLandscape->Modify();
			ExistingLandscape->Tags.AddUnique(BuildLandscapeTag(Map.MapName));
			if (bNeedsWeightmapSmoothing && bPreparedLandscape)
			{
				RefreshLandscapeWeightData(ExistingLandscape, EffectiveContext, Map, Prepared, OutResult);
			}
			if (UMaterialInterface* LandscapeMaterial =
				FMT2LandscapeMaterialImporter::CreateMaterialInstanceForPreparedMap(
					EffectiveContext, Map, Prepared, OutResult))
			{
				ExistingLandscape->LandscapeMaterial = LandscapeMaterial;
			}
			OutResult.ItemsSkipped++;
			OutResult.AddInfo(FString::Printf(TEXT("Landscape for %s already exists in the current level. Skipping terrain import."), *Map.MapName));
		}
		else if (bPreparedLandscape && EffectiveContext.bCreateLandscapeActors)
		{
			if (!CreateLandscapeActor(EffectiveContext, Map, Prepared, OutResult))
			{
				OutResult.ItemsSkipped++;
			}
		}

		EnterStage(Map.MapName, EffectiveContext.bImportStaticObjectsWithMaps
			? TEXT("Build object property-to-mesh cache")
			: TEXT("Map objects disabled - skipping cache"));
		bool bObjectCacheReady = false;
		if (EffectiveContext.bImportStaticObjectsWithMaps)
		{
			if (!EffectiveContext.ResolvedWorld && !bAttemptedWorldResolve)
			{
				bAttemptedWorldResolve = true;
				if (EffectiveContext.ScanResult)
				{
					FString ResolveError;
					FMT2PropertyResolver Resolver;
					if (Resolver.Resolve(*EffectiveContext.ScanResult, SharedResolvedWorld, ResolveError))
					{
						EffectiveContext.ResolvedWorld = &SharedResolvedWorld;
					}
					else
					{
						OutResult.AddWarning(FString::Printf(TEXT("Could not build map object cache: %s"), *ResolveError));
					}
				}
				else
				{
					OutResult.AddWarning(TEXT("Cannot build map object cache because ScanResult is not available."));
				}
			}
			bObjectCacheReady = EffectiveContext.ResolvedWorld != nullptr;
		}

		if (EffectiveContext.bImportStaticObjectsWithMaps && bObjectCacheReady)
		{
			ImportAndPlaceStaticObjectsForMap(EffectiveContext, Map.MapName,
				Map.MapSizeX * 128.0f * Map.CellScale, OutResult, &Progress);
		}
		else
		{
			EnterStage(Map.MapName, TEXT("Import missing meshes - skipped"));
			EnterStage(Map.MapName, TEXT("Place meshes in landscape - skipped"));
		}

		EnterStage(Map.MapName, TEXT("Configure mob spawns"));
		ImportMobSpawnsForMap(EffectiveContext, Map, OutResult);
		EnterStage(Map.MapName, TEXT("Configure minimap"));
		ImportMapPresentationForMap(EffectiveContext, Map, OutResult);
		if (!bLandscapeAlreadyImported && bPreparedLandscape)
		{
			OutResult.ItemsImported++;
			OutResult.AddInfo(FString::Printf(TEXT("Prepared terrain sources for %s: height %dx%d, tile %dx%d, %d weightmap(s)."),
				*Map.MapName,
				Prepared.HeightmapWidth,
				Prepared.HeightmapHeight,
				Prepared.TilemapWidth,
				Prepared.TilemapHeight,
				Prepared.WeightmapPaths.Num()),
				Prepared.OutputDirectory);
		}
	}

	OutResult.bSucceeded = !OutResult.HasErrors();
	return OutResult.bSucceeded;
}
bool FMT2MapTerrainImporter::CreateLandscapeActor(const FMT2ImportContext& Context, const FMT2MapTerrainInfo& Map, const FMT2PreparedMapTerrain& Prepared, FMT2ImportResult& OutResult)
{
	if (!GEditor)
	{
		OutResult.AddError(TEXT("Cannot create landscape because GEditor is not available."));
		return false;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		OutResult.AddError(TEXT("Cannot create landscape because no editor world is open."));
		return false;
	}

	ULevel* TargetLevel = World->GetCurrentLevel();
	if (!TargetLevel)
	{
		TargetLevel = World->PersistentLevel.Get();
	}
	if (!TargetLevel)
	{
		OutResult.AddError(TEXT("Cannot create landscape because no target level is available."));
		return false;
	}

	if (Prepared.LandscapeHeightData.Num() != Prepared.LandscapeWidth * Prepared.LandscapeHeight)
	{
		OutResult.AddError(FString::Printf(TEXT("Prepared landscape height data is invalid for %s."), *Map.MapName), Prepared.LandscapeHeightmapPath);
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("CreateMT2Landscape", "Create Metin2 Landscape"));
	World->Modify();
	TargetLevel->Modify();

	const float TotalWorldY = static_cast<float>(Map.MapSizeY * 128 * Map.CellScale);
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.ObjectFlags = RF_Transactional;
	SpawnParameters.OverrideLevel = TargetLevel;
	ALandscape* Landscape = World->SpawnActor<ALandscape>(FVector(0.0f, -TotalWorldY, 0.0f), FRotator::ZeroRotator, SpawnParameters);
	if (!Landscape)
	{
		OutResult.AddError(FString::Printf(TEXT("Failed to spawn landscape actor for %s."), *Map.MapName));
		return false;
	}

	Landscape->Modify();
	Landscape->Tags.AddUnique(BuildLandscapeTag(Map.MapName));
	Landscape->Tags.AddUnique(SmoothedWeightmapsTag);
	Landscape->SetActorLabel(FString::Printf(TEXT("MT2_Landscape_%s"), *Map.MapName));
	Landscape->SetFolderPath(FName(*(TEXT("MT2/") + Map.MapName + TEXT("/Terrain"))));
	Landscape->SetActorScale3D(FVector(Prepared.LandscapeXYScale, Prepared.LandscapeXYScale, Prepared.LandscapeZScale));
	Landscape->LandscapeMaterial = FMT2LandscapeMaterialImporter::CreateMaterialInstanceForPreparedMap(Context, Map, Prepared, OutResult);

	TMap<FGuid, TArray<uint16>> HeightDataPerLayer;
	HeightDataPerLayer.Add(FGuid(), Prepared.LandscapeHeightData);

	// A texture set may assign the same texture to several numeric tile IDs. Unreal only needs one
	// landscape layer for that texture, so combine those mutually-exclusive masks before import.
	TMap<FName, TArray<uint8>> WeightDataByLayer;
	TMap<FName, FString> SourcePathByLayer;
	BuildCombinedLandscapeWeightData(Context, Map, Prepared, WeightDataByLayer);
	for (const TPair<uint8, TArray<uint8>>& Pair : Prepared.LandscapeWeightDataByTile)
	{
		const FName LayerName = FMT2LandscapeMaterialImporter::BuildLayerNameForTile(Context, Map, Pair.Key);
		SourcePathByLayer.FindOrAdd(LayerName) = Prepared.LandscapeWeightmapPathByTile.FindRef(Pair.Key);
	}

	TArray<FLandscapeImportLayerInfo> ImportLayerInfos;
	TArray<FName> LayerNames;
	WeightDataByLayer.GetKeys(LayerNames);
	LayerNames.Sort(FNameLexicalLess());
	for (const FName LayerName : LayerNames)
	{
		const FString SanitizedLayerName = FMT2AssetScanner::SanitizePackagePathSegment(LayerName.ToString());
		const FString LayerAssetName = TEXT("LI_") + SanitizedLayerName;
		const FString LayerPackagePath = Context.DestinationRoot / UMT2PathSettings::Path(TEXT("Part_LandscapeLayers"));
		const FString LayerPackageName = LayerPackagePath / LayerAssetName;
		const FString LayerObjectPath = LayerPackageName + TEXT(".") + LayerAssetName;

		UPackage* Package = CreatePackage(*LayerPackageName);
		ULandscapeLayerInfoObject* LayerInfo = LoadObject<ULandscapeLayerInfoObject>(nullptr, *LayerObjectPath);
		if (!LayerInfo)
		{
			LayerInfo = NewObject<ULandscapeLayerInfoObject>(Package, *LayerAssetName, RF_Public | RF_Standalone | RF_Transactional);
			LayerInfo->LayerName = LayerName;
			LayerInfo->SetBlendMethod(ELandscapeTargetLayerBlendMethod::FinalWeightBlending, false);
			LayerInfo->LayerUsageDebugColor = FLinearColor::MakeRandomColor();
			FAssetRegistryModule::AssetCreated(LayerInfo);
			Package->MarkPackageDirty();
			OutResult.CreatedPackages.Add(LayerObjectPath);
		}

		FLandscapeImportLayerInfo ImportLayerInfo;
		ImportLayerInfo.LayerName = LayerName;
		ImportLayerInfo.LayerInfo = LayerInfo;
		ImportLayerInfo.SourceFilePath = SourcePathByLayer.FindRef(LayerName);
		ImportLayerInfo.LayerData = WeightDataByLayer.FindChecked(LayerName);
		ImportLayerInfos.Add(MoveTemp(ImportLayerInfo));

		if (Context.bEnableDebugLogs)
		{
			OutResult.AddInfo(FString::Printf(TEXT("[Debug][LandscapeImport] Map=%s Layer=%s WeightData=%d Source='%s' SourceExists=%s"),
				*Map.MapName,
				*LayerName.ToString(),
				WeightDataByLayer.FindChecked(LayerName).Num(),
				*SourcePathByLayer.FindRef(LayerName),
				IFileManager::Get().FileExists(*SourcePathByLayer.FindRef(LayerName)) ? TEXT("true") : TEXT("false")),
				SourcePathByLayer.FindRef(LayerName));
		}
	}

	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerDataPerLayer;
	MaterialLayerDataPerLayer.Add(FGuid(), MoveTemp(ImportLayerInfos));

	const FGuid LandscapeGuid = FGuid::NewGuid();
	Landscape->Import(
		LandscapeGuid,
		0,
		0,
		Prepared.LandscapeWidth - 1,
		Prepared.LandscapeHeight - 1,
		Prepared.LandscapeNumSubsections,
		Prepared.LandscapeComponentSizeQuads,
		HeightDataPerLayer,
		*Prepared.LandscapeHeightmapPath,
		MaterialLayerDataPerLayer,
		ELandscapeImportAlphamapType::Additive,
		TArrayView<const FLandscapeLayer>());

	if (ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo())
	{
		LandscapeInfo->UpdateLayerInfoMap(Landscape);
	}

	Landscape->PostEditChange();
	TargetLevel->MarkPackageDirty();
	World->MarkPackageDirty();
	OutResult.CreatedPackages.Add(FString::Printf(TEXT("WorldActor:MT2_Landscape_%s"), *Map.MapName));
	return true;
}

void FMT2MapTerrainImporter::ImportAndPlaceStaticObjectsForMap(const FMT2ImportContext& Context, const FString& MapName, float WorldSizeX, FMT2ImportResult& OutResult, FScopedSlowTask* Progress)
{
	auto EnterObjectStage = [Progress, &MapName](const FString& Stage)
	{
		if (Progress)
		{
			Progress->EnterProgressFrame(1.0f, FText::Format(
				LOCTEXT("MapObjectStageFormat", "{0}: {1}"),
				FText::FromString(MapName),
				FText::FromString(Stage)));
		}
	};
	auto SkipObjectStages = [&EnterObjectStage]()
	{
		EnterObjectStage(TEXT("Import missing meshes - skipped"));
		EnterObjectStage(TEXT("Place meshes in landscape - skipped"));
	};

	if (!Context.ScanResult)
	{
		OutResult.AddWarning(FString::Printf(TEXT("Cannot import/place static objects for %s because Context.ScanResult is not available."), *MapName));
		SkipObjectStages();
		return;
	}

	FMT2ResolvedWorldResult LocalResolvedWorld;
	const FMT2ResolvedWorldResult* ResolvedWorld = Context.ResolvedWorld;
	if (!ResolvedWorld)
	{
		FString Error;
		FMT2PropertyResolver Resolver;
		if (!Resolver.Resolve(*Context.ScanResult, LocalResolvedWorld, Error))
		{
			OutResult.AddWarning(FString::Printf(TEXT("Could not resolve map static objects for %s: %s"), *MapName, *Error));
			SkipObjectStages();
			return;
		}
		ResolvedWorld = &LocalResolvedWorld;
	}

	const FString TargetMapName = MapName.ToLower();
	TMap<FString, FMT2AssetRecord> UniqueMeshesByPath;
	TArray<FMT2MapObjectPlacement> MapPlacements;
	for (const FMT2MapObjectPlacement& Placement : ResolvedWorld->MapObjects)
	{
		if (Placement.MapName.ToLower() != TargetMapName ||
			!Placement.ReferencedAsset ||
			(Placement.ReferencedAsset->Kind != EMT2AssetKind::Granny &&
				Placement.ReferencedAsset->Kind != EMT2AssetKind::Tree))
		{
			continue;
		}

		// Carry the map width so the object importer X-mirrors the placement to match the flipped
		// landscape (UE X = WorldSizeX - Metin2X).
		FMT2MapObjectPlacement& Added = MapPlacements.Add_GetRef(Placement);
		Added.WorldSizeX = WorldSizeX;
		UniqueMeshesByPath.FindOrAdd(Placement.ReferencedAsset->AbsolutePath, *Placement.ReferencedAsset);
	}

	if (MapPlacements.Num() == 0 || UniqueMeshesByPath.Num() == 0)
	{
		OutResult.AddInfo(FString::Printf(TEXT("No GR2 or SpeedTree map object placements found for map %s. Resolver read %d AreaData file(s), %d placement(s), %d placement(s) with properties, and %d placement(s) with resolved assets."),
			*MapName,
			ResolvedWorld->AreaDataFilesRead,
			ResolvedWorld->MapObjects.Num(),
			ResolvedWorld->ObjectsWithProperty,
			ResolvedWorld->ObjectsWithResolvedAsset));
		SkipObjectStages();
		return;
	}

	TArray<FMT2AssetRecord> MissingStaticMeshes;
	TArray<FMT2AssetRecord> MissingSkeletalMeshes;
	const FMT2MeshUsageClassifier UsageClassifier(Context.ScanResult);
	if (!UsageClassifier.IsReady())
	{
		for (const FString& Warning : UsageClassifier.GetWarnings())
		{
			OutResult.AddWarning(Warning);
		}
	}
	int32 ExistingMeshCount = 0;
	FScopedSlowTask MeshCacheProgress(static_cast<float>(UniqueMeshesByPath.Num()), LOCTEXT("BuildMapMeshCacheProgress", "Building map object mesh cache..."));
	for (const TPair<FString, FMT2AssetRecord>& Pair : UniqueMeshesByPath)
	{
		MeshCacheProgress.EnterProgressFrame(1.0f, FText::Format(
			LOCTEXT("BuildMapMeshCacheProgressFormat", "Checking mesh: {0}"),
			FText::FromString(Pair.Value.VirtualPath)));
		if (Context.IsStopRequested() || MeshCacheProgress.ShouldCancel())
		{
			return;
		}

		if (Pair.Value.Kind == EMT2AssetKind::Tree)
		{
			if (LoadObject<UStaticMesh>(nullptr, *FMT2StaticMeshImporter::BuildObjectPath(Context, Pair.Value)))
			{
				ExistingMeshCount++;
			}
			else
			{
				MissingStaticMeshes.Add(Pair.Value);
			}
			continue;
		}

		FMT2GrannyFileInspection Inspection;
		FString Error;
		if (!FMT2GrannyMeshConverter::InspectGrannyFile(Pair.Value.AbsolutePath, Inspection, Error))
		{
			OutResult.AddWarning(FString::Printf(TEXT("Could not inspect map object mesh %s: %s"), *Pair.Value.VirtualPath, *Error), Pair.Value.AbsolutePath);
			continue;
		}

		const bool bNeedsSkeletalMesh = Inspection.Type == EMT2GrannyFileType::SkeletalMesh &&
			UsageClassifier.RequiresSkeletalMesh(Pair.Value, Inspection);
		if (bNeedsSkeletalMesh)
		{
			if (FMT2SkeletalMeshImporter::LoadImportedMesh(Context, Pair.Value))
			{
				ExistingMeshCount++;
			}
			else
			{
				MissingSkeletalMeshes.Add(Pair.Value);
			}
		}
		else if (Inspection.Type == EMT2GrannyFileType::StaticMesh ||
			Inspection.Type == EMT2GrannyFileType::SkeletalMesh)
		{
			if (LoadObject<UStaticMesh>(nullptr, *FMT2StaticMeshImporter::BuildObjectPath(Context, Pair.Value)))
			{
				ExistingMeshCount++;
			}
			else
			{
				MissingStaticMeshes.Add(Pair.Value);
			}
		}
	}

	const int32 MissingMeshCount = MissingStaticMeshes.Num() + MissingSkeletalMeshes.Num();
	OutResult.AddInfo(FString::Printf(TEXT("Map %s has %d placement(s), %d unique mesh source(s), %d existing mesh asset(s), and %d missing mesh asset(s)."),
		*MapName,
		MapPlacements.Num(),
		UniqueMeshesByPath.Num(),
		ExistingMeshCount,
		MissingMeshCount));

	EnterObjectStage(FString::Printf(TEXT("Import missing meshes (%d)"), MissingMeshCount));
	if (Context.IsStopRequested())
	{
		return;
	}

	if (MissingStaticMeshes.Num() > 0)
	{
		FMT2ImportRequest StaticMeshRequest;
		StaticMeshRequest.Context = Context;
		StaticMeshRequest.Context.bReplaceExisting = false;
		StaticMeshRequest.Selection.Domain = EMT2ImportDomain::StaticMeshes;
		StaticMeshRequest.Selection.AssetRecords = MoveTemp(MissingStaticMeshes);

		FMT2StaticMeshImporter StaticMeshImporter;
		FMT2ImportResult StaticMeshResult;
		StaticMeshImporter.Import(StaticMeshRequest, StaticMeshResult);
		OutResult.Messages.Append(StaticMeshResult.Messages);
		OutResult.CreatedFiles.Append(StaticMeshResult.CreatedFiles);
		OutResult.CreatedPackages.Append(StaticMeshResult.CreatedPackages);
	}

	if (MissingSkeletalMeshes.Num() > 0 && !Context.IsStopRequested())
	{
		FMT2ImportRequest SkeletalMeshRequest;
		SkeletalMeshRequest.Context = Context;
		SkeletalMeshRequest.Context.bReplaceExisting = false;
		SkeletalMeshRequest.Selection.Domain = EMT2ImportDomain::SkeletalMeshes;
		SkeletalMeshRequest.Selection.AssetRecords = MoveTemp(MissingSkeletalMeshes);

		FMT2SkeletalMeshImporter SkeletalMeshImporter;
		FMT2ImportResult SkeletalMeshResult;
		SkeletalMeshImporter.Import(SkeletalMeshRequest, SkeletalMeshResult);
		OutResult.Messages.Append(SkeletalMeshResult.Messages);
		OutResult.CreatedFiles.Append(SkeletalMeshResult.CreatedFiles);
		OutResult.CreatedPackages.Append(SkeletalMeshResult.CreatedPackages);
	}

	EnterObjectStage(FString::Printf(TEXT("Place meshes in landscape (%d objects)"), MapPlacements.Num()));
	if (Context.IsStopRequested())
	{
		return;
	}

	FMT2ImportContext PlacementContext = Context;
	PlacementContext.ResolvedWorld = ResolvedWorld;
	PlacementContext.MapNameFilter.Reset();

	FMT2ImportRequest PlacementRequest;
	PlacementRequest.Context = PlacementContext;
	PlacementRequest.Selection.Domain = EMT2ImportDomain::MapObjects;
	PlacementRequest.Selection.MapObjectPlacements = MoveTemp(MapPlacements);

	FMT2MapObjectImporter MapObjectImporter;
	FMT2ImportResult PlacementResult;
	MapObjectImporter.Import(PlacementRequest, PlacementResult);
	OutResult.Messages.Append(PlacementResult.Messages);
	OutResult.CreatedPackages.Append(PlacementResult.CreatedPackages);
}
#undef LOCTEXT_NAMESPACE
