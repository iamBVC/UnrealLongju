/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MT2MobSpawnActor.generated.h"

class UMT2MobSpawnComponent;
class USceneComponent;

UCLASS(BlueprintType)
class METIN2_API AMT2MobSpawnActor : public AActor
{
	GENERATED_BODY()

public:
	AMT2MobSpawnActor();

	UFUNCTION(BlueprintPure, Category = "Mob Spawning")
	UMT2MobSpawnComponent* GetMobSpawnComponent() const { return MobSpawnComponent; }

private:
	UPROPERTY(VisibleAnywhere, Category = "Mob Spawning")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Mob Spawning")
	TObjectPtr<UMT2MobSpawnComponent> MobSpawnComponent;
};
