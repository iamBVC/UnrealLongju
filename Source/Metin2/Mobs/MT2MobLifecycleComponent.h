/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "MT2MobLifecycleComponent.generated.h"

class UMT2HealthComponent;
struct FMT2MobDefinition;

UCLASS(BlueprintType, ClassGroup = "MT2", meta = (BlueprintSpawnableComponent))
class METIN2_API UMT2MobLifecycleComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMT2MobLifecycleComponent();
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void Configure(const FMT2MobDefinition& Definition);
	void StartRegeneration(UMT2HealthComponent* HealthComponent);
	void StopRegeneration();
	void ProcessRegeneration(double CurrentTime);
	bool SpawnResurrectionMob();

	UFUNCTION(BlueprintPure, Category = "Mob|Lifecycle")
	int32 GetResurrectionVnum() const { return ResurrectionVnum; }

	UFUNCTION(BlueprintPure, Category = "Mob|Lifecycle")
	int32 GetSummonVnum() const { return SummonVnum; }

private:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mob|Lifecycle", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", Units = "s"))
	float RegenerationCycle = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mob|Lifecycle", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "100.0"))
	float RegenerationPercent = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mob|Lifecycle", meta = (AllowPrivateAccess = "true"))
	int32 ResurrectionVnum = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mob|Lifecycle", meta = (AllowPrivateAccess = "true"))
	int32 SummonVnum = 0;

	TWeakObjectPtr<UMT2HealthComponent> Health;
	double NextRegenerationTime = 0.0;
};
