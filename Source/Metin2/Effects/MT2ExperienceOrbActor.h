/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MT2ExperienceOrbActor.generated.h"

class UParticleSystemComponent;
class UMaterialBillboardComponent;
class USceneComponent;

// Client-only visual. The server sends only one source position to each rewarded player; all orb
// movement is simulated locally and never enters actor replication.
UCLASS(NotBlueprintable)
class METIN2_API AMT2ExperienceOrbActor : public AActor
{
	GENERATED_BODY()

public:
	AMT2ExperienceOrbActor();

	static void SpawnOrbs(
		UWorld* World, const FVector& SourceLocation, AActor* Recipient,
		int64 ExperienceAmount);

protected:
	virtual void Tick(float DeltaSeconds) override;

private:
	void InitializeOrb(AActor* Recipient, const FVector& SourceLocation);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UParticleSystemComponent> Particle;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UMaterialBillboardComponent> OrbSprite;

	TWeakObjectPtr<AActor> TargetActor;
	FVector Velocity = FVector::ZeroVector;
	FVector Acceleration = FVector::ZeroVector;
	float MaximumSpeed = 300.0f;
	float ElapsedSeconds = 0.0f;
	float HomingStartTime = 1.0f;
	float HomingTurnRate = 300.0f;
	float RemainingRange = 10000.0f;
	float HitRadius = 40.0f;
	float MinimumVisibleTime = 0.6f;
	float VisualOffsetPeriod = 0.4f;
	float VisualOffsetAmplitude = 150.0f;
	float VisualAngularVelocity = 450.0f;
};
