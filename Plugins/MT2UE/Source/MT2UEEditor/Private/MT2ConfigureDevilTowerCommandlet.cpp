#include "MT2ConfigureDevilTowerCommandlet.h"
#include "Config/MT2PathSettings.h"
#include "Core/MT2VnumRegistry.h"
#include "Dungeons/MT2DevilTowerRoom.h"
#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshCompiler.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Importers/MT2MapTerrainImporter.h"
#include "MT2MapTerrainBuilder.h"
#include "Mobs/MT2Mob.h"
#include "Mobs/MT2MobSpawnActor.h"
#include "Mobs/MT2MobSpawnComponent.h"
#include "World/MT2MapPresentationActor.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionHelpers.h"

namespace
{
	struct FFloorPlan
	{
		UStaticMeshComponent* Mesh = nullptr;
		FVector Entrance;
		TArray<FMT2DungeonSpawn> Spawns;
	};
	bool Surface(UStaticMeshComponent* Mesh, const FVector2D& XY, FVector& Out)
	{
		FHitResult Hit;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(MT2TowerBake), true);
		if (!Mesh->LineTraceComponent(Hit, FVector(XY.X, XY.Y, Mesh->Bounds.GetBox().Max.Z + 1000),
			FVector(XY.X, XY.Y, Mesh->Bounds.GetBox().Min.Z - 1000), Query)) return false;
		if (Hit.ImpactNormal.Z < .5f) return false;
		Out = Hit.ImpactPoint; return true;
	}
	bool BakeSpawns(UWorld* World, const UMT2VnumRegistry* Registry, const AMT2MapPresentationActor* Map, FFloorPlan& Plan,
		const TArray<FMT2MobSpawnEntry>& Entries, FRandomStream& Random)
	{
		int32 Group = 0;
		for (const auto& Entry : Entries)
		{
			if (Entry.Variants.Num() != 1 || Entry.SpawnChancePercent != 100.f) return false;
			for (int32 Count = 0; Count < Entry.DesiredGroupCount; ++Count)
			{
				++Group;
				for (const auto& Member : Entry.Variants[0].Members)
				{
					const auto* Reference = Registry->GetMobClasses().Find(Member.MobVnum);
					UClass* Class = Reference ? Reference->LoadSynchronous() : nullptr;
					const auto* Defaults = Class ? Class->GetDefaultObject<AMT2Mob>() : nullptr;
					if (!Defaults) { UE_LOG(LogTemp, Error, TEXT("Missing tower mob VNUM %d."), Member.MobVnum); return false; }
					FVector Ground; bool bFound = false;
					const auto* Capsule = Defaults->GetCapsuleComponent();
					for (int32 Attempt = 0; Attempt < 64 && !bFound; ++Attempt)
					{
						const FVector2D XY(Entry.Center.X + Random.FRandRange(-Entry.HorizontalExtent.X, Entry.HorizontalExtent.X),
							Entry.Center.Y + Random.FRandRange(-Entry.HorizontalExtent.Y, Entry.HorizontalExtent.Y));
						if (!Surface(Plan.Mesh, XY, Ground)) continue;
						uint8 Flags = 0;
						if (Map->Attributes.Query(Ground, Map->WorldMin, Map->WorldMax, Flags) && (Flags & MT2MapAttribute::NoWalk)) continue;
						// Do not accept horizontal wall caps/roof ledges as the arena floor.
						if (FMath::Abs(Ground.Z - (Plan.Entrance.Z - 100.f)) > 150.f) continue;
						Ground.Z += Capsule->GetScaledCapsuleHalfHeight() + 2.f;
						FCollisionQueryParams Query(SCENE_QUERY_STAT(MT2TowerSpawnBake), false);
						bFound = !World->OverlapBlockingTestByChannel(Ground, FQuat::Identity, ECC_Pawn,
							FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Query);
						for (const auto& Previous : Plan.Spawns)
							if (FVector::DistSquared2D(Previous.LocalTransform.GetLocation(), Ground) < FMath::Square(150.f)) bFound = false;
					}
					if (!bFound) { UE_LOG(LogTemp, Error, TEXT("No clear room surface for %s VNUM %d."), *Entry.SourceId.ToString(), Member.MobVnum); return false; }
					auto& Spawn = Plan.Spawns.AddDefaulted_GetRef(); Spawn.Vnum = Member.MobVnum;
					Spawn.bForceAggressive = Entry.bForceAggressive; Spawn.GroupId = Group;
					Spawn.LocalTransform = FTransform(FRotator(0, Entry.Yaw < 0 ? Random.FRandRange(0.f, 360.f) : Entry.Yaw + 90.f, 0), Ground);
				}
			}
		}
		return !Plan.Spawns.IsEmpty();
	}
}

UMT2ConfigureDevilTowerCommandlet::UMT2ConfigureDevilTowerCommandlet()
{
	IsClient = false; IsEditor = true; IsServer = false; LogToConsole = true;
}

int32 UMT2ConfigureDevilTowerCommandlet::Main(const FString& Params)
{
	const FString MapPath = TEXT("/Game/Maps/Game/devil_tower");
	UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(
		FPackageName::LongPackageNameToFilename(MapPath, FPackageName::GetMapPackageExtension()));
	if (!World) return 1;
	FWorldPartitionHelpers::FForEachActorWithLoadingResult LoadedActors;
	if (UWorldPartition* Partition = World->GetWorldPartition())
	{
		FWorldPartitionHelpers::FForEachActorWithLoadingParams LoadParams;
		LoadParams.bKeepReferences = true;
		FWorldPartitionHelpers::ForEachActorWithLoading(Partition, [](const FWorldPartitionActorDescInstance*) { return true; }, LoadParams, LoadedActors);
	}
	for (TActorIterator<AMT2MapPresentationActor> It(World); It; ++It)
		UE_LOG(LogTemp, Display, TEXT("Tower metadata: id=%s index=%d min=%s max=%s cells=%s"),
			*It->MapId, It->MapIndex, *It->WorldMin.ToString(), *It->WorldMax.ToString(), *It->MapCells.ToString());
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		TInlineComponentArray<UStaticMeshComponent*> Meshes; It->GetComponents(Meshes);
		for (UStaticMeshComponent* Component : Meshes)
			if (Component->GetStaticMesh())
				UE_LOG(LogTemp, Display, TEXT("Tower mesh: %s asset=%s center=%s extent=%s collision=%d"),
					*It->GetActorLabel(), *Component->GetStaticMesh()->GetPathName(),
					*Component->Bounds.Origin.ToString(), *Component->Bounds.BoxExtent.ToString(),
					static_cast<int32>(Component->GetCollisionEnabled()));
	}
	for (TActorIterator<AMT2MobSpawnActor> It(World); It; ++It)
		UE_LOG(LogTemp, Display, TEXT("Tower ambient spawner: %s entries=%d"),
			*It->GetActorLabel(), It->GetMobSpawnComponent()->GetSpawnEntries().Num());
	FString LocaleRoot, DungeonRoot;
	FParse::Value(*Params, TEXT("LocaleRoot="), LocaleRoot);
	FParse::Value(*Params, TEXT("DungeonRoot="), DungeonRoot);
	if (LocaleRoot.IsEmpty() || DungeonRoot.IsEmpty())
	{
		UE_LOG(LogTemp, Display, TEXT("Inspection only. Supply -LocaleRoot=... -DungeonRoot=... to validate; add -Apply to save.")); return 0;
	}
	AMT2MapPresentationActor* Map = nullptr;
	for (TActorIterator<AMT2MapPresentationActor> It(World); It; ++It) { if (Map) return 1; Map = *It; }
	if (!Map || Map->MapIndex != 66 || Map->MapCells != FIntPoint(3,3) || Map->WorldMax.X != 76800.f)
	{
		UE_LOG(LogTemp, Error, TEXT("Tower map metadata does not match the verified legacy layout.")); return 1;
	}
	const auto* Registry = LoadObject<UMT2VnumRegistry>(nullptr, UMT2PathSettings::Path(TEXT("VnumRegistry")));
	if (!Registry) return 1;
	TArray<UPackage*> CollisionPackages;
	const bool bVerify = FParse::Param(*Params, TEXT("Verify"));
	TArray<FFloorPlan> Plans; Plans.SetNum(3);
	const FVector2D LocalEntrances[] = { {117,614}, {126,384}, {134,147} };
	for (int32 Floor = 0; Floor < 3; ++Floor)
	{
		const FString LabelPart = FString::Printf(TEXT("_00000%d_deviltower_1F"), 2 - Floor);
		for (TActorIterator<AActor> It(World); It; ++It)
			if (It->GetActorLabel().Contains(LabelPart, ESearchCase::IgnoreCase))
			{
				if (Plans[Floor].Mesh) return 1;
				Plans[Floor].Mesh = It->FindComponentByClass<UStaticMeshComponent>();
			}
		if (Plans[Floor].Mesh)
		{
			UStaticMesh* Asset = Plans[Floor].Mesh->GetStaticMesh();
			FStaticMeshCompilingManager::Get().FinishCompilation({Asset});
			auto* Body = Asset->GetBodySetup();
			if (!Body) return 1;
			// A single convex hull fills an entire hollow dungeon room. Its roof is not a floor.
			// Validate against triangle collision, then save that same collision policy with -Apply.
			if (Body->CollisionTraceFlag != CTF_UseComplexAsSimple)
			{
				if (bVerify) { UE_LOG(LogTemp, Error, TEXT("Saved room mesh requires triangle collision policy.")); return 1; }
				Asset->Modify(); Body->Modify(); Body->CollisionTraceFlag = CTF_UseComplexAsSimple;
				Body->InvalidatePhysicsData(); Body->CreatePhysicsMeshes(); Asset->MarkPackageDirty();
				CollisionPackages.AddUnique(Asset->GetPackage());
				for (TActorIterator<AActor> It(World); It; ++It)
				{
					TInlineComponentArray<UStaticMeshComponent*> Components; It->GetComponents(Components);
					for (auto* Component : Components) if (Component->GetStaticMesh() == Asset) Component->RecreatePhysicsState();
				}
			}
			if (!Asset->ContainsPhysicsTriMeshData(false))
			{
				UE_LOG(LogTemp, Error, TEXT("Room mesh has no triangle collision source: %s"), *Asset->GetPathName()); return 1;
			}
		}
		if (!Plans[Floor].Mesh || !Surface(Plans[Floor].Mesh,
			FVector2D(76800 - LocalEntrances[Floor].X * 100, -LocalEntrances[Floor].Y * 100), Plans[Floor].Entrance))
		{
			if (Plans[Floor].Mesh)
			{
				auto* Mesh = Plans[Floor].Mesh;
				UE_LOG(LogTemp, Error, TEXT("Room trace diagnostics: physics=%d body=%d setup=%d complex=%d location=%s"),
					World->GetPhysicsScene() != nullptr, Mesh->GetBodyInstance()->IsValidBodyInstance(),
					Mesh->GetStaticMesh()->GetBodySetup() != nullptr,
					Mesh->GetStaticMesh()->GetBodySetup() ? static_cast<int32>(Mesh->GetStaticMesh()->GetBodySetup()->CollisionTraceFlag) : -1,
					*Mesh->GetComponentLocation().ToString());
				if (auto* Body = Mesh->GetStaticMesh()->GetBodySetup())
					UE_LOG(LogTemp, Display, TEXT("Tower collision: convex=%d boxes=%d trimeshes=%d"), Body->AggGeom.ConvexElems.Num(), Body->AggGeom.BoxElems.Num(), Body->ChaosTriMeshes.Num());
			}
			UE_LOG(LogTemp, Error, TEXT("Cannot verify floor %d entrance on its room mesh."), Floor + 1); return 1;
		}
		// Entrance is stored as ground + a conservative player capsule clearance; TeleportTo
		// checks actual occupancy on authority at runtime.
		Plans[Floor].Entrance.Z += 100;
		TArray<FMT2MobSpawnEntry> Entries; TArray<FString> Warnings;
		if (Floor == 0)
		{
			for (TActorIterator<AMT2MobSpawnActor> It(World); It; ++It)
				for (const auto& Entry : It->GetMobSpawnComponent()->GetSpawnEntries())
					if (Entry.SourceVnum == 8015 && Entry.Type == EMT2MobSpawnType::Mob) Entries.Add(Entry);
			if (Entries.IsEmpty())
			{
				// Reruns use the source entry, which was removed from the ambient scheduler.
				FMT2MapTerrainInfo Info; Info.MapSizeX = 3; Info.MapSizeY = 3;
				TArray<FMT2MobSpawnEntry> Source;
				if (!FMT2MapTerrainImporter::ReadDungeonRegen(LocaleRoot, LocaleRoot / TEXT("map/metin2_map_deviltower1/regen.txt"), Info, Source, Warnings)) return 1;
				for (const auto& Entry : Source) if (Entry.SourceVnum == 8015 && Entry.Type == EMT2MobSpawnType::Mob) Entries.Add(Entry);
			}
			if (Entries.Num() != 1) return 1;
		}
		else
		{
			FMT2MapTerrainInfo Info; Info.MapSizeX = 3; Info.MapSizeY = 3;
			if (!FMT2MapTerrainImporter::ReadDungeonRegen(LocaleRoot,
				DungeonRoot / FString::Printf(TEXT("deviltower%d_regen.txt"), Floor + 1), Info, Entries, Warnings))
			{
				for (const auto& Warning : Warnings) UE_LOG(LogTemp, Error, TEXT("%s"), *Warning);
				return 1;
			}
		}
		FRandomStream Random(66001 + Floor);
		if (!BakeSpawns(World, Registry, Map, Plans[Floor], Entries, Random)) return 1;
		UE_LOG(LogTemp, Display, TEXT("Validated tower floor %d: entrance=%s, %d baked mobs."), Floor + 1, *Plans[Floor].Entrance.ToString(), Plans[Floor].Spawns.Num());
	}
	if (bVerify)
	{
		TArray<AMT2DevilTowerRoom*> Rooms; Rooms.SetNumZeroed(3);
		for (TActorIterator<AMT2DevilTowerRoom> It(World); It; ++It)
		{
			const int32 Index = It->Floor - 1;
			if (!Rooms.IsValidIndex(Index) || Rooms[Index]) return 1;
			Rooms[Index] = *It;
		}
		for (int32 Index = 0; Index < 3; ++Index)
		{
			auto* Room = Rooms[Index];
			if (!Room || Room->RoomId != FName(*FString::Printf(TEXT("DevilTower.Floor%d"), Index + 1)) ||
				Room->Spawns.Num() != Plans[Index].Spawns.Num() || Room->EncounterSeconds != 0.f ||
				!Room->Entrance->GetComponentLocation().Equals(Plans[Index].Entrance, .1f) ||
				Room->NextRoom != (Index < 2 ? Rooms[Index+1] : nullptr) || Room->GetIsSpatiallyLoaded())
			{
				UE_LOG(LogTemp, Error, TEXT("Saved floor %d configuration differs from source."), Index + 1); return 1;
			}
			for (int32 SpawnIndex = 0; SpawnIndex < Room->Spawns.Num(); ++SpawnIndex)
			{
				const auto& Spawn = Room->Spawns[SpawnIndex]; const auto& Plan = Plans[Index].Spawns[SpawnIndex];
				if (Spawn.Vnum != Plan.Vnum || Spawn.GroupId != Plan.GroupId || Spawn.bForceAggressive != Plan.bForceAggressive ||
					!(Spawn.LocalTransform * Room->GetActorTransform()).Equals(Plan.LocalTransform, .1f) ||
					!Room->ContainsLocation((Spawn.LocalTransform * Room->GetActorTransform()).GetLocation())) return 1;
			}
		}
		for (TActorIterator<AMT2MobSpawnActor> It(World); It; ++It)
			for (const auto& Entry : It->GetMobSpawnComponent()->GetSpawnEntries()) if (Entry.SourceVnum == 8015) return 1;
		UE_LOG(LogTemp, Display, TEXT("Saved tower opening verified: three unique rooms, linked stages, baked spawns and no duplicate ambient stone.")); return 0;
	}
	if (!FParse::Param(*Params, TEXT("Apply"))) return 0;
	// No persistent modifications occur until all inputs, classes and room surfaces passed.
	TArray<AMT2DevilTowerRoom*> Rooms; TArray<UPackage*> Packages = CollisionPackages;
	for (int32 Floor = 0; Floor < 3; ++Floor)
	{
		const FName Id(*FString::Printf(TEXT("DevilTower.Floor%d"), Floor + 1));
		AMT2DevilTowerRoom* Room = nullptr;
		for (TActorIterator<AMT2DevilTowerRoom> It(World); It; ++It) if (It->RoomId == Id)
		{
			if (Room) return 1; Room = *It;
		}
		if (!Room)
		{
			Room = World->SpawnActor<AMT2DevilTowerRoom>();
			if (!Room) return 1;
			Room->SetPackageExternal(true);
		}
		Room->Modify(); Room->RoomId = Id; Room->Floor = Floor + 1;
		Room->SetActorLabel(Id.ToString()); Room->SetIsSpatiallyLoaded(false);
		Room->SetActorLocation(Plans[Floor].Mesh->Bounds.Origin);
		Room->Bounds->SetBoxExtent(Plans[Floor].Mesh->Bounds.BoxExtent + FVector(500,500,500));
		Room->Entrance->SetWorldLocation(Plans[Floor].Entrance);
		Room->TransitionSeconds = Floor == 0 ? 6.f : 4.f;
		Room->Spawns = Plans[Floor].Spawns;
		for (auto& Spawn : Room->Spawns) Spawn.LocalTransform = Spawn.LocalTransform.GetRelativeTransform(Room->GetActorTransform());
		Room->MarkPackageDirty(); Packages.AddUnique(Room->GetPackage()); Rooms.Add(Room);
	}
	Rooms[0]->NextRoom = Rooms[1]; Rooms[1]->NextRoom = Rooms[2]; Rooms[2]->NextRoom = nullptr;
	for (TActorIterator<AMT2MobSpawnActor> It(World); It; ++It)
	{
		auto Entries = It->GetMobSpawnComponent()->GetSpawnEntries();
		if (Entries.RemoveAll([](const FMT2MobSpawnEntry& Entry) { return Entry.SourceVnum == 8015 && Entry.Type == EMT2MobSpawnType::Mob; }) > 0)
		{
			It->Modify(); It->GetMobSpawnComponent()->SetSpawnEntries(Entries); It->MarkPackageDirty(); Packages.AddUnique(It->GetPackage());
		}
	}
	Packages.AddUnique(World->GetPackage());
	if (!UEditorLoadingAndSavingUtils::SavePackages(Packages, true)) return 1;
	UE_LOG(LogTemp, Display, TEXT("Saved shared Devil Tower opening stages. Floors 4-9 are not enabled."));
	return 0;
}
