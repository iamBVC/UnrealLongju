/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "MT2RepairMapActorsCommandlet.h"

#include "FileHelpers.h"
#include "Importers/MT2MapObjectImporter.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"

UMT2RepairMapActorsCommandlet::UMT2RepairMapActorsCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMT2RepairMapActorsCommandlet::Main(const FString& Params)
{
	FString MapsArgument;
	FParse::Value(*Params, TEXT("Maps="), MapsArgument);
	TArray<FString> MapPaths;
	MapsArgument.ParseIntoArray(MapPaths, TEXT(","), true);
	if (MapPaths.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("MT2 map actor repair requires -Maps=/Game/MapA,/Game/MapB."));
		return 1;
	}

	int32 TotalRemoved = 0;
	bool bFailed = false;
	for (FString MapPath : MapPaths)
	{
		MapPath.TrimStartAndEndInline();
		const FString Filename = FPackageName::LongPackageNameToFilename(
			MapPath, FPackageName::GetMapPackageExtension());
		UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Filename);
		ULevel* Level = World ? World->PersistentLevel.Get() : nullptr;
		if (!World || !Level)
		{
			UE_LOG(LogTemp, Error, TEXT("Could not load map for actor repair: %s"), *MapPath);
			bFailed = true;
			continue;
		}

		int32 BrokenCount = 0;
		int32 DuplicateCount = 0;
		TArray<FString> Warnings;
		const int32 RemovedCount = FMT2MapObjectImporter::RepairImportedActors(
			World, Level, BrokenCount, DuplicateCount, Warnings);
		for (const FString& Warning : Warnings)
		{
			UE_LOG(LogTemp, Warning, TEXT("%s: %s"), *MapPath, *Warning);
		}

		if (RemovedCount > 0 && !UEditorLoadingAndSavingUtils::SaveMap(World, MapPath))
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to save repaired map: %s"), *MapPath);
			bFailed = true;
			continue;
		}
		TotalRemoved += RemovedCount;
		UE_LOG(LogTemp, Display,
			TEXT("MT2 map actor repair %s: removed %d (%d broken, %d duplicate)."),
			*MapPath, RemovedCount, BrokenCount, DuplicateCount);
	}

	UE_LOG(LogTemp, Display, TEXT("MT2 map actor repair complete: removed %d actor(s)."), TotalRemoved);
	return bFailed ? 1 : 0;
}
