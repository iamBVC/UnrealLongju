/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MT2PlayerSpatialGridSubsystem.generated.h"

class UMT2ActorRelevanceComponent;
class APawn;

UCLASS()
class METIN2_API UMT2PlayerSpatialGridSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	void RegisterRelevanceComponent(UMT2ActorRelevanceComponent* Component);
	void UnregisterRelevanceComponent(UMT2ActorRelevanceComponent* Component);
	void RegisterSimulatedPlayer(APawn* Pawn);
	void UnregisterSimulatedPlayer(APawn* Pawn);

	UFUNCTION(BlueprintPure, Category = "MT2|Spatial Grid")
	int32 EstimatePlayersInRadius(const FVector& Location, float Radius) const;

	// Every player pawn within Radius (2D), via the cell buckets - what the voice relay uses to
	// decide who hears a speaker without scanning every player on the server.
	void GetPlayerPawnsInRadius(const FVector& Location, float Radius, TArray<APawn*>& OutPawns) const;
	float GetClosestPlayerDistanceSquared(const FVector& Location, float MaxRadius) const;
	APawn* FindClosestPlayerPawn(const FVector& Location, float Radius) const;

	UFUNCTION(BlueprintPure, Category = "MT2|Spatial Grid")
	float GetGridCellSize() const { return GridCellSize; }

	bool HasTrackedPlayers() const { return !PlayersByCell.IsEmpty(); }

private:
	friend class FMT2FakePlayersTest;
	FIntPoint ToCell(const FVector& Location) const;
	void RebuildPlayerGrid();
	void RefreshRelevanceComponents();

	UPROPERTY(EditAnywhere, Category = "MT2|Spatial Grid", meta = (ClampMin = "1000.0", Units = "cm"))
	float GridCellSize = 2500.0f;

	UPROPERTY(EditAnywhere, Category = "MT2|Spatial Grid", meta = (ClampMin = "0.1", Units = "s"))
	float RefreshInterval = 1.0f;

	TMap<FIntPoint, int32> PlayerCountsByCell;
	TMap<FIntPoint, TArray<TWeakObjectPtr<APawn>>> PlayersByCell;
	TSet<TWeakObjectPtr<UMT2ActorRelevanceComponent>> RelevanceComponents;
	TSet<TWeakObjectPtr<APawn>> SimulatedPlayers;
	float TimeUntilRefresh = 0.0f;
};
