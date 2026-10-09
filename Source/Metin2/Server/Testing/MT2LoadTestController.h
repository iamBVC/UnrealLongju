#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "MT2LoadTestController.generated.h"

class AMT2PlayerCharacter;
class AMT2Mob;

UCLASS(Transient, NotBlueprintable)
class METIN2_API AMT2LoadTestController : public AAIController
{
	GENERATED_BODY()
public:
	AMT2LoadTestController();
	virtual void InitPlayerState() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	void InitializeBehavior(const FVector& Origin, float Radius);
	void Think(double Now);
	void RemoveFakePlayer();
private:
	FVector Home = FVector::ZeroVector;
	float RoamRadius = 3000.f;
	double NextDecisionTime = 0.;
	double DeadSince = -1.;
	double NextWanderTime = 0.;
	double NextPickupTime = 0.;
	TWeakObjectPtr<AMT2Mob> Target;
	TWeakObjectPtr<AMT2PlayerCharacter> TrackedPawn;
};
