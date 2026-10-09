/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "World/MT2PlayerSpatialGridSubsystem.h"

#include "Components/MT2ActorRelevanceComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FramePro/FramePro.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

namespace
{
	template<typename Value, typename Visitor>
	void VisitNearbyCells(const TMap<FIntPoint, Value>& Cells, FIntPoint MinCell, FIntPoint MaxCell, Visitor Visit)
	{
		const int64 Width = int64(MaxCell.X) - MinCell.X + 1;
		const int64 Height = int64(MaxCell.Y) - MinCell.Y + 1;
		if (Cells.IsEmpty() || Width <= 0 || Height <= 0) return;
		// Neighbor lookups for local queries; occupied buckets for large sparse queries.
		if (Width <= Cells.Num() && Height <= Cells.Num() / Width)
		{
			for (int64 X = MinCell.X; X <= MaxCell.X; ++X)
				for (int64 Y = MinCell.Y; Y <= MaxCell.Y; ++Y)
					if (const Value* Bucket = Cells.Find(FIntPoint(int32(X), int32(Y)))) Visit(*Bucket);
		}
		else
		{
			for (const auto& Pair : Cells)
				if (Pair.Key.X >= MinCell.X && Pair.Key.X <= MaxCell.X &&
					Pair.Key.Y >= MinCell.Y && Pair.Key.Y <= MaxCell.Y) Visit(Pair.Value);
		}
	}
}

void UMT2PlayerSpatialGridSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	TimeUntilRefresh = 0.0f;
}

void UMT2PlayerSpatialGridSubsystem::Deinitialize()
{
	RelevanceComponents.Reset();
	SimulatedPlayers.Reset();
	PlayerCountsByCell.Reset();
	PlayersByCell.Reset();
	Super::Deinitialize();
}

void UMT2PlayerSpatialGridSubsystem::Tick(float DeltaTime)
{
	FRAMEPRO_NAMED_SCOPE("MT2.PlayerGrid.Tick");
	TRACE_CPUPROFILER_EVENT_SCOPE(MT2_PlayerGrid_Tick);
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	TimeUntilRefresh -= DeltaTime;
	if (TimeUntilRefresh > 0.0f)
	{
		return;
	}
	TimeUntilRefresh = RefreshInterval;
	RebuildPlayerGrid();
	RefreshRelevanceComponents();
}

TStatId UMT2PlayerSpatialGridSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMT2PlayerSpatialGridSubsystem, STATGROUP_Tickables);
}

void UMT2PlayerSpatialGridSubsystem::RegisterRelevanceComponent(UMT2ActorRelevanceComponent* Component)
{
	if (Component)
	{
		RelevanceComponents.Add(Component);
	}
}

void UMT2PlayerSpatialGridSubsystem::UnregisterRelevanceComponent(UMT2ActorRelevanceComponent* Component)
{
	RelevanceComponents.Remove(Component);
}

void UMT2PlayerSpatialGridSubsystem::RegisterSimulatedPlayer(APawn* Pawn)
{
	if (Pawn && Pawn->HasAuthority()) { SimulatedPlayers.Add(Pawn); TimeUntilRefresh = 0.f; }
}

void UMT2PlayerSpatialGridSubsystem::UnregisterSimulatedPlayer(APawn* Pawn)
{
	SimulatedPlayers.Remove(Pawn); TimeUntilRefresh = 0.f;
}

int32 UMT2PlayerSpatialGridSubsystem::EstimatePlayersInRadius(const FVector& Location, float Radius) const
{
	FRAMEPRO_NAMED_SCOPE("MT2.PlayerGrid.EstimatePlayersInRadius");
	if (GridCellSize <= UE_SMALL_NUMBER || Radius < 0.0f)
	{
		return 0;
	}

	const FIntPoint MinCell = ToCell(Location - FVector(Radius, Radius, 0.0f));
	const FIntPoint MaxCell = ToCell(Location + FVector(Radius, Radius, 0.0f));
	int32 Count = 0;
	VisitNearbyCells(PlayerCountsByCell, MinCell, MaxCell, [&Count](int32 CellCount) { Count += CellCount; });
	return Count;
}

void UMT2PlayerSpatialGridSubsystem::GetPlayerPawnsInRadius(
	const FVector& Location, float Radius, TArray<APawn*>& OutPawns) const
{
	FRAMEPRO_NAMED_SCOPE("MT2.PlayerGrid.GetPlayerPawnsInRadius");
	OutPawns.Reset();
	if (GridCellSize <= UE_SMALL_NUMBER || Radius <= 0.0f)
	{
		return;
	}
	const float RadiusSquared = FMath::Square(Radius);
	const FIntPoint MinCell = ToCell(Location - FVector(Radius, Radius, 0.0f));
	const FIntPoint MaxCell = ToCell(Location + FVector(Radius, Radius, 0.0f));
	VisitNearbyCells(PlayersByCell, MinCell, MaxCell, [&](const auto& Bucket)
	{
		for (const TWeakObjectPtr<APawn>& WeakPawn : Bucket)
		{
			APawn* Pawn = WeakPawn.Get();
			if (Pawn && FVector::DistSquared2D(Location, Pawn->GetActorLocation()) <= RadiusSquared)
			{
				OutPawns.Add(Pawn);
			}
		}
	});
}

float UMT2PlayerSpatialGridSubsystem::GetClosestPlayerDistanceSquared(
	const FVector& Location, float MaxRadius) const
{
	FRAMEPRO_NAMED_SCOPE("MT2.PlayerGrid.GetClosestPlayerDistance");
	TRACE_CPUPROFILER_EVENT_SCOPE(MT2_PlayerGrid_GetClosestPlayerDistance);
	const FIntPoint MinCell = ToCell(Location - FVector(MaxRadius, MaxRadius, 0.0f));
	const FIntPoint MaxCell = ToCell(Location + FVector(MaxRadius, MaxRadius, 0.0f));
	float Best = TNumericLimits<float>::Max();
	VisitNearbyCells(PlayersByCell, MinCell, MaxCell, [&](const auto& Bucket)
	{
		for (const TWeakObjectPtr<APawn>& WeakPawn : Bucket)
		{
			if (const APawn* Pawn = WeakPawn.Get())
			{
				Best = FMath::Min(Best, FVector::DistSquared2D(Location, Pawn->GetActorLocation()));
			}
		}
	});
	return Best;
}

APawn* UMT2PlayerSpatialGridSubsystem::FindClosestPlayerPawn(const FVector& Location, float Radius) const
{
	FRAMEPRO_NAMED_SCOPE("MT2.PlayerGrid.FindClosestPlayer");
	const FIntPoint MinCell = ToCell(Location - FVector(Radius, Radius, 0.0f));
	const FIntPoint MaxCell = ToCell(Location + FVector(Radius, Radius, 0.0f));
	APawn* BestPawn = nullptr;
	float BestDistanceSquared = FMath::Square(Radius);
	VisitNearbyCells(PlayersByCell, MinCell, MaxCell, [&](const auto& Bucket)
	{
		for (const TWeakObjectPtr<APawn>& WeakPawn : Bucket)
		{
			APawn* Pawn = WeakPawn.Get();
			if (!Pawn) continue;
			const float DistanceSquared = FVector::DistSquared2D(Location, Pawn->GetActorLocation());
			if (DistanceSquared <= BestDistanceSquared)
			{
				BestDistanceSquared = DistanceSquared;
				BestPawn = Pawn;
			}
		}
	});
	return BestPawn;
}

FIntPoint UMT2PlayerSpatialGridSubsystem::ToCell(const FVector& Location) const
{
	return FIntPoint(
		FMath::FloorToInt(Location.X / GridCellSize),
		FMath::FloorToInt(Location.Y / GridCellSize));
}

void UMT2PlayerSpatialGridSubsystem::RebuildPlayerGrid()
{
	FRAMEPRO_NAMED_SCOPE("MT2.PlayerGrid.Rebuild");
	TRACE_CPUPROFILER_EVENT_SCOPE(MT2_PlayerGrid_Rebuild);
	PlayerCountsByCell.Reset();
	PlayersByCell.Reset();
	for (TActorIterator<APlayerController> It(GetWorld()); It; ++It)
	{
		APlayerController* Controller = *It;
		APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
		if (Pawn && !Pawn->IsActorBeingDestroyed())
		{
			const FIntPoint Cell = ToCell(Pawn->GetActorLocation());
			PlayerCountsByCell.FindOrAdd(Cell)++;
			PlayersByCell.FindOrAdd(Cell).Add(Pawn);
		}
	}
	for (auto It = SimulatedPlayers.CreateIterator(); It; ++It)
	{
		APawn* Pawn = It->Get();
		if (!Pawn || Pawn->IsActorBeingDestroyed()) { It.RemoveCurrent(); continue; }
		// Avoid duplicates if a registered pawn is later possessed by a real player controller.
		if (Cast<APlayerController>(Pawn->GetController())) continue;
		const FIntPoint Cell = ToCell(Pawn->GetActorLocation());
		PlayerCountsByCell.FindOrAdd(Cell)++;
		PlayersByCell.FindOrAdd(Cell).Add(Pawn);
	}
}

void UMT2PlayerSpatialGridSubsystem::RefreshRelevanceComponents()
{
	FRAMEPRO_NAMED_SCOPE("MT2.PlayerGrid.RefreshRelevance");
	TRACE_CPUPROFILER_EVENT_SCOPE(MT2_PlayerGrid_RefreshRelevance);
	for (auto It = RelevanceComponents.CreateIterator(); It; ++It)
	{
		UMT2ActorRelevanceComponent* Component = It->Get();
		if (!Component)
		{
			It.RemoveCurrent();
			continue;
		}
		Component->RefreshSpatialRelevance(*this);
	}
}
