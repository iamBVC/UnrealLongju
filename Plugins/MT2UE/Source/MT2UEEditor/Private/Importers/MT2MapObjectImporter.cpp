/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Importers/MT2MapObjectImporter.h"

#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
#include "Animation/SkeletalMeshActor.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Level.h"
#include "EngineUtils.h"
#include "World/MT2MapPresentationActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Importers/MT2StaticMeshImporter.h"
#include "Importers/MT2SkeletalMeshImporter.h"
#include "Misc/ScopedSlowTask.h"
#include "MT2AssetOptimizer.h"
#include "ScopedTransaction.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionHelpers.h"

#define LOCTEXT_NAMESPACE "FMT2MapObjectImporter"

namespace
{
	FName BuildPlacementTag(const FMT2MapObjectPlacement& Placement)
	{
		// Keep the identity in source space. Converted transforms can evolve as importer axis conversion is
		// corrected; source data remains stable and lets an existing actor be updated instead of duplicated.
		return FName(*FString::Printf(TEXT("MT2UE_MapObject:%s:%s:%u:%.3f:%.3f:%.3f:%.3f:%.3f:%.3f"),
			*Placement.MapName,
			*Placement.CellName,
			Placement.PropertyCrc,
			Placement.Position.X,
			Placement.Position.Y,
			Placement.Position.Z + Placement.HeightBias,
			Placement.Rotation.Pitch,
			Placement.Rotation.Yaw,
			Placement.Rotation.Roll));
	}

	FName BuildPlacedActorKey(const FVector& Location, const FRotator& Rotation, const UObject* Mesh)
	{
		if (!Mesh)
		{
			return NAME_None;
		}

		return FName(*FString::Printf(TEXT("MT2UE_Placed:%s:%.1f:%.1f:%.1f:%.1f:%.1f:%.1f"),
			*Mesh->GetPathName(),
			Location.X,
			Location.Y,
			Location.Z,
			Rotation.Pitch,
			Rotation.Yaw,
			Rotation.Roll));
	}

	bool IsImportedMapObject(const AActor* Actor)
	{
		if (!Actor)
		{
			return false;
		}
		for (const FName Tag : Actor->Tags)
		{
			if (Tag == TEXT("MT2UE_MapObject") || Tag.ToString().StartsWith(TEXT("MT2UE_MapObject:")))
			{
				return true;
			}
		}

		// Older importer revisions only assigned the stable MT2_ actor label. Keep supporting those
		// actors so maps imported before placement tags were introduced can also be repaired.
		return (Actor->IsA<AStaticMeshActor>() || Actor->IsA<ASkeletalMeshActor>())
			&& Actor->GetActorLabel().StartsWith(TEXT("MT2_"));
	}

	FName GetImportedActorKey(const AActor* Actor, bool& bOutBroken)
	{
		bOutBroken = false;
		if (const AStaticMeshActor* StaticActor = Cast<AStaticMeshActor>(Actor))
		{
			const UStaticMeshComponent* Component = StaticActor->GetStaticMeshComponent();
			if (!Component || !Component->GetStaticMesh())
			{
				bOutBroken = true;
				return NAME_None;
			}
			return BuildPlacedActorKey(Actor->GetActorLocation(), Actor->GetActorRotation(), Component->GetStaticMesh());
		}
		if (const ASkeletalMeshActor* SkeletalActor = Cast<ASkeletalMeshActor>(Actor))
		{
			const USkeletalMeshComponent* Component = SkeletalActor->GetSkeletalMeshComponent();
			if (!Component || !Component->GetSkeletalMeshAsset())
			{
				bOutBroken = true;
				return NAME_None;
			}
			return BuildPlacedActorKey(Actor->GetActorLocation(), Actor->GetActorRotation(), Component->GetSkeletalMeshAsset());
		}

		// MT2UE map-object tags belong only to importer-created mesh actors.
		bOutBroken = true;
		return NAME_None;
	}

	void BuildPlacedActorCache(
		const ULevel* Level,
		TSet<FName>& OutPlacementTags,
		TMap<FName, AActor*>& OutActorsByKey,
		TMap<FName, AActor*>& OutActorsByPlacementTag)
	{
		if (!Level)
		{
			return;
		}

		for (AActor* Actor : Level->Actors)
		{
			if (!Actor)
			{
				continue;
			}
			OutPlacementTags.Append(Actor->Tags);
			for (const FName Tag : Actor->Tags)
			{
				if (Tag.ToString().StartsWith(TEXT("MT2UE_MapObject:")))
				{
					OutActorsByPlacementTag.FindOrAdd(Tag, Actor);
				}
			}

			if (const AStaticMeshActor* StaticActor = Cast<AStaticMeshActor>(Actor))
			{
				if (UStaticMesh* StaticMesh = StaticActor->GetStaticMeshComponent()->GetStaticMesh())
				{
					OutActorsByKey.FindOrAdd(BuildPlacedActorKey(Actor->GetActorLocation(), Actor->GetActorRotation(), StaticMesh), Actor);
				}
			}
			else if (const ASkeletalMeshActor* SkeletalActor = Cast<ASkeletalMeshActor>(Actor))
			{
				if (USkeletalMesh* SkeletalMesh = SkeletalActor->GetSkeletalMeshComponent()->GetSkeletalMeshAsset())
				{
					OutActorsByKey.FindOrAdd(BuildPlacedActorKey(Actor->GetActorLocation(), Actor->GetActorRotation(), SkeletalMesh), Actor);
				}
			}
		}
	}
}

FMT2MapObjectImporter::FMT2MapObjectImporter()
	: FMT2ImporterBase(EMT2ImportDomain::MapObjects, TEXT("MapObjectImporter"))
{
}

bool FMT2MapObjectImporter::Discover(const FMT2ImportContext& Context, const FMT2AssetScanResult& ScanResult, FMT2ImportDiscovery& OutDiscovery) const
{
	FMT2ResolvedWorldResult ResolvedWorld;
	FString Error;
	FMT2PropertyResolver Resolver;
	OutDiscovery.Domain = GetDomain();
	if (!Resolver.Resolve(ScanResult, ResolvedWorld, Error))
	{
		FMT2ImportMessage Message;
		Message.Severity = EMT2ImportSeverity::Warning;
		Message.Text = Error;
		OutDiscovery.Messages.Add(MoveTemp(Message));
		return false;
	}

	TMap<FString, int32> StaticPlacementCountsByMap;
	for (const FMT2MapObjectPlacement& Placement : ResolvedWorld.MapObjects)
	{
		if (Placement.ReferencedAsset &&
			(Placement.ReferencedAsset->Kind == EMT2AssetKind::Granny ||
				Placement.ReferencedAsset->Kind == EMT2AssetKind::Tree))
		{
			StaticPlacementCountsByMap.FindOrAdd(Placement.MapName)++;
		}
	}

	StaticPlacementCountsByMap.GetKeys(OutDiscovery.EntryNames);
	OutDiscovery.EntryNames.Sort();
	OutDiscovery.EntryCountsByName = StaticPlacementCountsByMap;
	OutDiscovery.ItemsDiscovered = OutDiscovery.EntryNames.Num();
	return true;
}

bool FMT2MapObjectImporter::Import(const FMT2ImportRequest& Request, FMT2ImportResult& OutResult)
{
	if (!CanImport(Request, OutResult))
	{
		return false;
	}

	TArray<FMT2MapObjectPlacement> Placements = Request.Selection.MapObjectPlacements;
	if (Placements.Num() == 0 && Request.Context.ResolvedWorld)
	{
		Placements = Request.Context.ResolvedWorld->MapObjects;
	}

	if (Placements.Num() == 0)
	{
		OutResult.AddWarning(TEXT("No map object placements were provided."));
		OutResult.bSucceeded = true;
		return true;
	}


	TSet<FString> SelectedMaps;
	for (const FString& MapName : Request.Selection.MapNames)
	{
		SelectedMaps.Add(MapName.ToLower());
	}

	const FString MapNameFilter = Request.Context.MapNameFilter.TrimStartAndEnd().ToLower();
	TArray<const FMT2MapObjectPlacement*> FilteredPlacements;
	FilteredPlacements.Reserve(Placements.Num());
	int32 SkippedByFilterCount = 0;
	for (const FMT2MapObjectPlacement& Placement : Placements)
	{
		if (!Placement.ReferencedAsset ||
			(Placement.ReferencedAsset->Kind != EMT2AssetKind::Granny &&
				Placement.ReferencedAsset->Kind != EMT2AssetKind::Tree))
		{
			continue;
		}

		const FString PlacementMapName = Placement.MapName.ToLower();
		if ((SelectedMaps.Num() > 0 && !SelectedMaps.Contains(PlacementMapName)) ||
			(!MapNameFilter.IsEmpty() && PlacementMapName != MapNameFilter))
		{
			SkippedByFilterCount++;
			continue;
		}

		if (Request.MaxItems > 0 && FilteredPlacements.Num() >= Request.MaxItems)
		{
			break;
		}
		FilteredPlacements.Add(&Placement);
	}

	TMap<FString, UStaticMesh*> StaticMeshCache;
	TMap<FString, USkeletalMesh*> SkeletalMeshCache;
	int32 ConsideredCount = 0;
	int32 MissingMeshCount = 0;
	int32 AlreadyPlacedCount = 0;

	if (Request.Context.bDryRun)
	{
		for (const FMT2MapObjectPlacement* PlacementPtr : FilteredPlacements)
		{
			const FMT2MapObjectPlacement& Placement = *PlacementPtr;
			ConsideredCount++;
			OutResult.CreatedPackages.Add(FMT2StaticMeshImporter::BuildObjectPath(Request.Context, *Placement.ReferencedAsset));
		}

		OutResult.ItemsDiscovered = ConsideredCount;
		OutResult.ItemsSkipped = SkippedByFilterCount;
		OutResult.AddInfo(FString::Printf(TEXT("Dry run: prepared %d map object placement(s)."), ConsideredCount));
		OutResult.bSucceeded = true;
		return true;
	}

	if (!GEditor)
	{
		OutResult.AddError(TEXT("Cannot place map objects because GEditor is not available."));
		return false;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		OutResult.AddError(TEXT("Cannot place map objects because no editor world is open."));
		return false;
	}

	ULevel* TargetLevel = World->GetCurrentLevel();
	if (!TargetLevel)
	{
		TargetLevel = World->PersistentLevel.Get();
	}
	if (!TargetLevel)
	{
		OutResult.AddError(TEXT("Cannot place map objects because no target level is available."));
		return false;
	}

	int32 RepairedBrokenCount = 0;
	int32 RepairedDuplicateCount = 0;
	TArray<FString> RepairWarnings;
	const int32 RepairedActorCount = RepairImportedActors(
		World, TargetLevel, RepairedBrokenCount, RepairedDuplicateCount, RepairWarnings);
	if (RepairedActorCount > 0)
	{
		OutResult.AddInfo(FString::Printf(
			TEXT("Repaired %d imported map actor(s) before placement: %d broken, %d duplicate."),
			RepairedActorCount, RepairedBrokenCount, RepairedDuplicateCount));
	}
	for (const FString& Warning : RepairWarnings)
	{
		OutResult.AddWarning(Warning);
	}

	// Fill each placement's map width so ConvertMetin2LocationToUnreal can X-mirror it to match the
	// east-west flipped landscape. Read WorldSizeX from the map presentation actor already in the level
	// (reliable, since the terrain is imported first), plus any terrain info in the selection.
	TMap<FString, float> WorldSizeXByMap;
	for (const FMT2MapTerrainInfo& Terrain : Request.Selection.MapTerrains)
	{
		WorldSizeXByMap.Add(Terrain.MapName.ToLower(),
			Terrain.MapSizeX * 128.0f * static_cast<float>(Terrain.CellScale));
	}
	for (TActorIterator<AMT2MapPresentationActor> It(World); It; ++It)
	{
		const float SizeX = It->WorldMax.X - It->WorldMin.X;
		if (SizeX <= 0.0f) continue;
		for (const FName Tag : It->Tags)
		{
			FString TagName = Tag.ToString();
			if (TagName.RemoveFromStart(TEXT("MT2UE_MapPresentation:")))
			{
				WorldSizeXByMap.Add(TagName.ToLower(), SizeX);
			}
		}
	}
	TSet<FString> MapsMissingWidth;
	for (FMT2MapObjectPlacement& Placement : Placements)
	{
		if (Placement.WorldSizeX > 0.0f)
		{
			continue; // already provided by the map-import caller
		}
		if (const float* WorldSizeX = WorldSizeXByMap.Find(Placement.MapName.ToLower()))
		{
			Placement.WorldSizeX = *WorldSizeX;
		}
		else
		{
			MapsMissingWidth.Add(Placement.MapName);
		}
	}
	for (const FString& MissingMap : MapsMissingWidth)
	{
		OutResult.AddWarning(FString::Printf(
			TEXT("No map width for '%s' - object X not mirrored. Import that map's terrain first."), *MissingMap));
	}

	const FScopedTransaction Transaction(LOCTEXT("PlaceMT2MapStaticObjects", "Place Metin2 Map Static Objects"));
	World->Modify();
	TargetLevel->Modify();
	TSet<FName> ExistingPlacementTags;
	TMap<FName, AActor*> ExistingActorsByKey;
	TMap<FName, AActor*> ExistingActorsByPlacementTag;
	BuildPlacedActorCache(TargetLevel, ExistingPlacementTags, ExistingActorsByKey, ExistingActorsByPlacementTag);
	FScopedSlowTask PlacementProgress(static_cast<float>(FMath::Max(FilteredPlacements.Num(), 1)), LOCTEXT("PlaceMapObjectsProgress", "Placing map objects..."));

	for (int32 PlacementIndex = 0; PlacementIndex < FilteredPlacements.Num(); ++PlacementIndex)
	{
		const FMT2MapObjectPlacement& Placement = *FilteredPlacements[PlacementIndex];
		PlacementProgress.EnterProgressFrame(1.0f, FText::Format(
			LOCTEXT("PlaceMapObjectProgressFormat", "Placing object {0} of {1}: {2}"),
			FText::AsNumber(PlacementIndex + 1),
			FText::AsNumber(FilteredPlacements.Num()),
			FText::FromString(Placement.Property ? Placement.Property->PropertyName : Placement.CellName)));
		if (Request.Context.IsStopRequested() || PlacementProgress.ShouldCancel())
		{
			OutResult.AddWarning(TEXT("Map object placement stopped by user."));
			break;
		}

		ConsideredCount++;
		const FName PlacementTag = BuildPlacementTag(Placement);

		const FString MeshObjectPath = FMT2StaticMeshImporter::BuildObjectPath(Request.Context, *Placement.ReferencedAsset);
		UStaticMesh** CachedMesh = StaticMeshCache.Find(MeshObjectPath);
		UStaticMesh* StaticMesh = CachedMesh ? *CachedMesh : LoadObject<UStaticMesh>(nullptr, *MeshObjectPath);
		if (!CachedMesh)
		{
			StaticMeshCache.Add(MeshObjectPath, StaticMesh);
		}

		FString SkeletalMeshObjectPath;
		USkeletalMesh* SkeletalMesh = nullptr;
		if (Placement.ReferencedAsset->Kind == EMT2AssetKind::Granny)
		{
			USkeletalMesh** CachedSkeletalMesh = SkeletalMeshCache.Find(Placement.ReferencedAsset->AbsolutePath);
			SkeletalMesh = CachedSkeletalMesh ? *CachedSkeletalMesh : FMT2SkeletalMeshImporter::LoadImportedMesh(Request.Context, *Placement.ReferencedAsset, &SkeletalMeshObjectPath);
			if (!CachedSkeletalMesh)
			{
				SkeletalMeshCache.Add(Placement.ReferencedAsset->AbsolutePath, SkeletalMesh);
			}
		}

		if (!StaticMesh && !SkeletalMesh)
		{
			MissingMeshCount++;
			OutResult.AddWarning(FString::Printf(TEXT("Missing imported mesh for placement in %s/%s: %s or %s"),
				*Placement.MapName,
				*Placement.CellName,
				*MeshObjectPath,
				*SkeletalMeshObjectPath),
				Placement.ReferencedAsset->AbsolutePath);
			continue;
		}

		const FVector SpawnLocation = ConvertMetin2LocationToUnreal(Placement);
		const FRotator SpawnRotation = ConvertMetin2RotationToUnreal(Placement);
		if (AActor* ExistingTaggedActor = ExistingActorsByPlacementTag.FindRef(PlacementTag))
		{
			bool bUpdatedExistingActor = false;
			if (AStaticMeshActor* ExistingStaticActor = Cast<AStaticMeshActor>(ExistingTaggedActor))
			{
				if (StaticMesh)
				{
					ExistingStaticActor->Modify();
					ExistingStaticActor->SetActorLocationAndRotation(SpawnLocation, SpawnRotation);
					ExistingStaticActor->GetStaticMeshComponent()->SetStaticMesh(StaticMesh);
					ExistingStaticActor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Static);
					bUpdatedExistingActor = true;
				}
			}
			else if (ASkeletalMeshActor* ExistingSkeletalActor = Cast<ASkeletalMeshActor>(ExistingTaggedActor))
			{
				if (!StaticMesh && SkeletalMesh)
				{
					ExistingSkeletalActor->Modify();
					ExistingSkeletalActor->SetActorLocationAndRotation(SpawnLocation, SpawnRotation);
					ExistingSkeletalActor->GetSkeletalMeshComponent()->SetSkeletalMeshAsset(SkeletalMesh);
					bUpdatedExistingActor = true;
				}
			}

			if (bUpdatedExistingActor)
			{
				if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(ExistingTaggedActor->GetRootComponent()))
				{
					Primitive->SetCullDistance(FMT2AssetOptimizer::ComputeMapObjectCullDistance(Primitive->Bounds));
				}
				AlreadyPlacedCount++;
				continue;
			}

			// A legacy skeletal map actor cannot change class in place. Replace it only after the
			// corresponding static mesh has been created successfully.
			for (auto It = ExistingActorsByKey.CreateIterator(); It; ++It)
			{
				if (It.Value() == ExistingTaggedActor)
				{
					It.RemoveCurrent();
				}
			}
			ExistingActorsByPlacementTag.Remove(PlacementTag);
			ExistingPlacementTags.Remove(PlacementTag);
			World->EditorDestroyActor(ExistingTaggedActor, true);
		}
		UObject* PlacedMesh = StaticMesh ? static_cast<UObject*>(StaticMesh) : static_cast<UObject*>(SkeletalMesh);
		// Migrate actors produced by both older rotation conversions. The most recent additive mirror is
		// especially visible on C1's 225-degree warpgate: it produced 45 degrees instead of 315 degrees.
		const FRotator LegacyRotations[] =
		{
			FRotator(Placement.Rotation.Yaw,
				FMath::UnwindDegrees(Placement.Rotation.Roll + 180.0f), Placement.Rotation.Pitch),
			FRotator(Placement.Rotation.Yaw, Placement.Rotation.Roll, Placement.Rotation.Pitch)
		};
		AActor* LegacyActor = nullptr;
		for (const FRotator& LegacyRotation : LegacyRotations)
		{
			if (AActor** PreviousActorPtr = ExistingActorsByKey.Find(
				BuildPlacedActorKey(SpawnLocation, LegacyRotation, PlacedMesh)))
			{
				LegacyActor = *PreviousActorPtr;
				break;
			}
		}
		if (LegacyActor)
		{
			LegacyActor->Modify();
			LegacyActor->SetActorRotation(SpawnRotation);
			LegacyActor->Tags.AddUnique(PlacementTag);
			LegacyActor->Tags.AddUnique(TEXT("MT2UE_MapObject"));
			ExistingPlacementTags.Add(PlacementTag);
			ExistingActorsByKey.FindOrAdd(BuildPlacedActorKey(SpawnLocation, SpawnRotation, PlacedMesh), LegacyActor);
			AlreadyPlacedCount++;
			continue;
		}
		const FName PlacedActorKey = BuildPlacedActorKey(SpawnLocation, SpawnRotation, PlacedMesh);
		if (AActor** ExistingActorPtr = ExistingActorsByKey.Find(PlacedActorKey))
		{
			AActor* ExistingActor = *ExistingActorPtr;
			ExistingActor->Modify();
			ExistingActor->Tags.AddUnique(PlacementTag);
			ExistingActor->Tags.AddUnique(TEXT("MT2UE_MapObject"));
			if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(ExistingActor->GetRootComponent()))
			{
				Primitive->SetCullDistance(FMT2AssetOptimizer::ComputeMapObjectCullDistance(Primitive->Bounds));
			}
			ExistingPlacementTags.Add(PlacementTag);
			AlreadyPlacedCount++;
			continue;
		}

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = NAME_None;
		SpawnParameters.ObjectFlags = RF_Transactional;
		SpawnParameters.OverrideLevel = TargetLevel;

		AActor* Actor = nullptr;
		if (StaticMesh)
		{
			AStaticMeshActor* StaticActor = World->SpawnActor<AStaticMeshActor>(
				SpawnLocation,
				SpawnRotation,
				SpawnParameters);
			if (StaticActor)
			{
				StaticActor->GetStaticMeshComponent()->SetStaticMesh(StaticMesh);
				StaticActor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Static);
				Actor = StaticActor;
			}
		}
		else
		{
			ASkeletalMeshActor* SkeletalActor = World->SpawnActor<ASkeletalMeshActor>(
				SpawnLocation,
				SpawnRotation,
				SpawnParameters);
			if (SkeletalActor)
			{
				SkeletalActor->GetSkeletalMeshComponent()->SetSkeletalMeshAsset(SkeletalMesh);
				SkeletalActor->GetSkeletalMeshComponent()->SetMobility(EComponentMobility::Movable);
				Actor = SkeletalActor;
			}
		}

		if (!Actor)
		{
			OutResult.AddWarning(FString::Printf(TEXT("Failed to spawn mesh actor for %s in %s/%s."),
				StaticMesh ? *MeshObjectPath : *SkeletalMeshObjectPath,
				*Placement.MapName,
				*Placement.CellName),
				Placement.ReferencedAsset->AbsolutePath);
			continue;
		}

		Actor->Modify();
		Actor->Tags.AddUnique(PlacementTag);
		Actor->Tags.AddUnique(TEXT("MT2UE_MapObject"));
		if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Actor->GetRootComponent()))
		{
			Primitive->SetCullDistance(FMT2AssetOptimizer::ComputeMapObjectCullDistance(Primitive->Bounds));
		}
		ExistingPlacementTags.Add(PlacementTag);
		ExistingActorsByKey.FindOrAdd(PlacedActorKey, Actor);
		Actor->SetFolderPath(FName(*(TEXT("MT2/") + Placement.MapName + TEXT("/") + Placement.CellName)));

		const FString PropertyName = Placement.Property ? Placement.Property->PropertyName : FString::Printf(TEXT("crc_%u"), Placement.PropertyCrc);
		Actor->SetActorLabel(FString::Printf(TEXT("MT2_%s_%s_%s"), *Placement.MapName, *Placement.CellName, *PropertyName));
		OutResult.ItemsImported++;
		OutResult.CreatedPackages.Add(StaticMesh ? MeshObjectPath : SkeletalMeshObjectPath);
	}

	TargetLevel->MarkPackageDirty();
	World->MarkPackageDirty();
	OutResult.ItemsDiscovered = ConsideredCount;
	OutResult.ItemsSkipped = MissingMeshCount + SkippedByFilterCount + AlreadyPlacedCount;
	OutResult.AddInfo(FString::Printf(TEXT("Placed %d actor(s) from %d considered map placements. Already placed: %d. Missing imported meshes: %d. Skipped by filter: %d."),
		OutResult.ItemsImported,
		ConsideredCount,
		AlreadyPlacedCount,
		MissingMeshCount,
		SkippedByFilterCount));

	OutResult.bSucceeded = !OutResult.HasErrors();
	return OutResult.bSucceeded;
}

int32 FMT2MapObjectImporter::RepairImportedActors(
	UWorld* World, ULevel* Level, int32& OutBrokenCount, int32& OutDuplicateCount,
	TArray<FString>& OutWarnings)
{
	OutBrokenCount = 0;
	OutDuplicateCount = 0;
	OutWarnings.Reset();
	if (!World || !Level)
	{
		OutWarnings.Add(TEXT("Cannot repair imported map actors because no loaded world/level is available."));
		return 0;
	}

	// World Partition keeps spatial actors in external packages and normally loads only the active
	// editor cells. Hold references while repairing so broken actors outside loaded cells are included.
	FWorldPartitionHelpers::FForEachActorWithLoadingResult LoadedActors;
	if (Level == World->PersistentLevel && World->GetWorldPartition())
	{
		FWorldPartitionHelpers::FForEachActorWithLoadingParams LoadParams;
		LoadParams.bKeepReferences = true;
		LoadParams.ActorClasses = { AStaticMeshActor::StaticClass(), ASkeletalMeshActor::StaticClass() };
		FWorldPartitionHelpers::ForEachActorWithLoading(
			World->GetWorldPartition(),
			[](const FWorldPartitionActorDescInstance*) { return true; },
			LoadParams,
			LoadedActors);
	}

	TMap<FName, AActor*> FirstActorByKey;
	TArray<TPair<AActor*, bool>> ActorsToDelete;
	int32 ImportedActorCount = 0;
	for (AActor* Actor : Level->Actors)
	{
		if (!IsImportedMapObject(Actor))
		{
			continue;
		}
		++ImportedActorCount;

		bool bBroken = false;
		const FName ActorKey = GetImportedActorKey(Actor, bBroken);
		if (bBroken || ActorKey.IsNone())
		{
			ActorsToDelete.Emplace(Actor, true);
			continue;
		}
		if (FirstActorByKey.Contains(ActorKey))
		{
			ActorsToDelete.Emplace(Actor, false);
		}
		else
		{
			FirstActorByKey.Add(ActorKey, Actor);
		}
	}

	int32 DeletedCount = 0;
	for (const TPair<AActor*, bool>& Entry : ActorsToDelete)
	{
		AActor* Actor = Entry.Key;
		const FString ActorName = IsValid(Actor) ? Actor->GetActorLabel() : TEXT("Invalid map actor");
		if (IsValid(Actor) && World->EditorDestroyActor(Actor, true))
		{
			++DeletedCount;
			Entry.Value ? ++OutBrokenCount : ++OutDuplicateCount;
		}
		else
		{
			OutWarnings.Add(FString::Printf(TEXT("Could not remove imported map actor: %s"), *ActorName));
		}
	}

	if (DeletedCount > 0)
	{
		Level->MarkPackageDirty();
		World->MarkPackageDirty();
	}
	UE_LOG(LogTemp, Display,
		TEXT("MT2 map actor repair scanned %d imported actor(s), including %d loaded World Partition actor(s)."),
		ImportedActorCount, LoadedActors.ActorReferences.Num());
	return DeletedCount;
}

FVector FMT2MapObjectImporter::ConvertMetin2LocationToUnreal(const FMT2MapObjectPlacement& Placement)
{
	// Mirror X to match the east-west flipped landscape (UE X = WorldSizeX - Metin2X). WorldSizeX is
	// filled in per placement by the importer; 0 means unknown, in which case we leave X as-is.
	const float X = Placement.WorldSizeX > 0.0f
		? Placement.WorldSizeX - Placement.Position.X : Placement.Position.X;
	return FVector(X, Placement.Position.Y, Placement.Position.Z + Placement.HeightBias);
}

FRotator FMT2MapObjectImporter::ConvertMetin2RotationToUnreal(const FMT2MapObjectPlacement& Placement)
{
	// The landscape conversion mirrors source X. A mirrored heading is 180 - source angle, not
	// 180 + source angle. Source yaw rotates around Y and must be reflected too; source pitch maps to
	// UE roll. This matches CArea's D3DX yaw/pitch/roll order for the common Z-only map placements.
	return FRotator(
		-Placement.Rotation.Yaw,
		FMath::UnwindDegrees(180.0f - Placement.Rotation.Roll),
		Placement.Rotation.Pitch);
}

#undef LOCTEXT_NAMESPACE
