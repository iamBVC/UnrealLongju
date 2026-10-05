/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MT2HitEffectActor.generated.h"

class UBillboardComponent;
class UTexture2D;

// Minimal, code-only hit-spark placeholder: a short sprite flipbook that always faces the camera,
// spawned purely cosmetically on each client (never replicated). There are no Niagara/particle
// assets imported for this yet, only the raw source textures, so this is a stand-in until a proper
// VFX is authored in the Niagara editor.
UCLASS()
class METIN2_API AMT2HitEffectActor : public AActor
{
	GENERATED_BODY()

public:
	AMT2HitEffectActor();

	// Spawns the default impact flipbook (ymir_work/effect/monster2/T_impact1-3) at Location.
	static void SpawnDefaultHitEffect(UWorld* World, const FVector& Location);

protected:
	virtual void Tick(float DeltaSeconds) override;

private:
	void PlayFrames(const TArray<UTexture2D*>& Frames, float FrameDuration, float Size);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBillboardComponent> Billboard;

	TArray<TObjectPtr<UTexture2D>> FrameTextures;
	float SecondsPerFrame = 0.05f;
	float ElapsedTime = 0.0f;
	int32 CurrentFrameIndex = 0;
};
