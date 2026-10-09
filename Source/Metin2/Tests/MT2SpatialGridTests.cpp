#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Components/SceneComponent.h"
#include "World/MT2PlayerSpatialGridSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2SpatialQueriesTest, "Metin2.World.SpatialQueries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2SpatialQueriesTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	auto* Grid = World->GetSubsystem<UMT2PlayerSpatialGridSubsystem>();
	if (!TestNotNull(TEXT("Grid"), Grid)) return false;
	for (int32 Index = -32; Index < 32; ++Index)
	{
		auto* Pawn = World->SpawnActor<APawn>();
		auto* Root = NewObject<USceneComponent>(Pawn);
		Pawn->SetRootComponent(Root); Root->RegisterComponent();
		Pawn->SetActorLocation(FVector(Index * 10000., Index * 3000., 0));
		const FIntPoint Cell = Grid->ToCell(Pawn->GetActorLocation());
		Grid->PlayersByCell.FindOrAdd(Cell).Add(Pawn);
		Grid->PlayerCountsByCell.FindOrAdd(Cell)++;
	}
	for (const FVector Location : {FVector(100, 100, 0), FVector(-10050, -3050, 0)})
	{
		for (float Radius : {200.f, 5000.f, 1000000.f})
		{
			const FIntPoint Min = Grid->ToCell(Location - FVector(Radius, Radius, 0));
			const FIntPoint Max = Grid->ToCell(Location + FVector(Radius, Radius, 0));
			TArray<APawn*> Expected;
			float Closest = TNumericLimits<float>::Max();
			float BestRadiusDistance = FMath::Square(Radius);
			APawn* BestPawn = nullptr;
			int32 Count = 0;
			for (const auto& Pair : Grid->PlayersByCell)
			{
				if (Pair.Key.X < Min.X || Pair.Key.X > Max.X || Pair.Key.Y < Min.Y || Pair.Key.Y > Max.Y) continue;
				Count += Pair.Value.Num();
				for (const auto& Weak : Pair.Value)
				{
					auto* Pawn = Weak.Get();
					const float Distance = FVector::DistSquared2D(Location, Pawn->GetActorLocation());
					Closest = FMath::Min(Closest, Distance);
					if (Distance <= FMath::Square(Radius)) Expected.Add(Pawn);
					if (Distance <= BestRadiusDistance) { BestRadiusDistance = Distance; BestPawn = Pawn; }
				}
			}
			TArray<APawn*> Actual; Grid->GetPlayerPawnsInRadius(Location, Radius, Actual);
			TestEqual(TEXT("Neighbor and sparse searches match exhaustive counts"), Actual.Num(), Expected.Num());
			for (auto* Pawn : Expected) TestTrue(TEXT("Expected pawn present"), Actual.Contains(Pawn));
			TestEqual(TEXT("Estimated occupied cell counts unchanged"), Grid->EstimatePlayersInRadius(Location, Radius), Count);
			TestEqual(TEXT("Nearest cell distance unchanged"), Grid->GetClosestPlayerDistanceSquared(Location, Radius), Closest);
			TestEqual(TEXT("Nearest in-radius target unchanged"), Grid->FindClosestPlayerPawn(Location, Radius), BestPawn);
		}
	}
	return true;
}
#endif
