/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Animation/MT2AnimationMotionData.h"
#include "MT2MotionEffectComponent.generated.h"

class UAnimSequence;
class UParticleSystemComponent;
class USkeletalMeshComponent;

// Plays the cosmetic MotionEventType 1 entries imported from an animation's .msa source. The
// component is shared by players and mobs and never replicates effect objects: the authoritative
// animation multicast makes every client schedule the same local visual events.
UCLASS(ClassGroup = (MT2), meta = (BlueprintSpawnableComponent))
class METIN2_API UMT2MotionEffectComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMT2MotionEffectComponent();

	UFUNCTION(BlueprintCallable, Category = "Effects")
	void PlayMotionEffects(UAnimSequence* Animation, float PlayRate = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "Effects")
	void StopPendingMotionEffects();

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void SpawnMotionEffect(FMT2MotionEffectEvent Event);
	void TrackMotionEffect(UParticleSystemComponent* Effect);
	USkeletalMeshComponent* ResolveOwnerMesh() const;

	TArray<FTimerHandle> PendingEffectTimers;
	TArray<TObjectPtr<UParticleSystemComponent>> ActiveMotionEffects;
};
