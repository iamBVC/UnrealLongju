/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "MT2ActorRelevanceComponent.generated.h"

class UMT2PlayerSpatialGridSubsystem;

UCLASS(BlueprintType, ClassGroup = "MT2", meta = (BlueprintSpawnableComponent))
class METIN2_API UMT2ActorRelevanceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMT2ActorRelevanceComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void RefreshSpatialRelevance(const UMT2PlayerSpatialGridSubsystem& PlayerGrid);

	UFUNCTION(BlueprintPure, Category = "MT2|Relevance")
	bool IsSpatiallyActive() const { return bSpatiallyActive; }

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MT2|Relevance", meta = (ClampMin = "100.0", Units = "cm"))
	float ActivationRadius = 12000.0f;

private:
	void SetSpatiallyActive(bool bNewActive);

	TMap<TWeakObjectPtr<UActorComponent>, bool> SavedComponentTickStates;
	bool bSavedActorTickEnabled = false;
	bool bSpatiallyActive = true;
};
