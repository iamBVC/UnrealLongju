/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "MT2SkillVisualEffectComponent.generated.h"

class UParticleSystemComponent;
struct FStreamableHandle;

// Client visualizer for replicated toggle-skill affects. The server still owns the status effect;
// this component only mirrors it as old-style looping particles on every observing client.
UCLASS(BlueprintType, ClassGroup = "MT2", meta = (BlueprintSpawnableComponent))
class METIN2_API UMT2SkillVisualEffectComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMT2SkillVisualEffectComponent();
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(
		float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category = "Skills|Visual Effects")
	void RefreshEffects();

private:
	void ClearEffects();
	void FinishPendingLoad(uint32 ExpectedHash);
	bool IsCharacterMoving() const;

	UPROPERTY(Transient)
	TMap<int64, TObjectPtr<UParticleSystemComponent>> ActiveEffects;

	TSet<int64> MovementOnlyEffects;
	TSharedPtr<FStreamableHandle> PendingLoadHandle;
	uint32 PendingLoadHash = 0;
};
