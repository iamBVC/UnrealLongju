#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MT2LoadTestSettings.generated.h"

/** Development-only server-controlled players; these do not create network connections. */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Server Load Testing"))
class METIN2_API UMT2LoadTestSettings : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	virtual FName GetCategoryName() const override { return TEXT("Game"); }
	UPROPERTY(Config, EditAnywhere, Category="Fake Players") bool bEnabled = true;
	UPROPERTY(Config, EditAnywhere, Category="Fake Players", meta=(ClampMin="1", ClampMax="500")) int32 MaximumPlayers = 200;
	UPROPERTY(Config, EditAnywhere, Category="Fake Players", meta=(ClampMin="1", ClampMax="20")) int32 SpawnBatchSize = 4;
	UPROPERTY(Config, EditAnywhere, Category="Fake Players", meta=(ClampMin="100", ClampMax="20000", Units="cm")) float DefaultRadius = 3000.f;
	UPROPERTY(Config, EditAnywhere, Category="Fake Players", meta=(ClampMin="1")) int32 DefaultLevel = 1;
	UPROPERTY(Config, EditAnywhere, Category="Behavior", meta=(ClampMin="0.1", Units="s")) float DecisionInterval = .5f;
	UPROPERTY(Config, EditAnywhere, Category="Behavior", meta=(ClampMin="100", ClampMax="5000", Units="cm")) float TargetSearchRadius = 1500.f;
	UPROPERTY(Config, EditAnywhere, Category="Behavior", meta=(ClampMin="1", Units="s")) float RespawnDelay = 5.f;
};
