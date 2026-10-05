/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "MT2TargetIndicatorComponent.generated.h"

class UMT2CombatComponent;
class UStaticMeshComponent;

// Draws the old EFFECT_SELECT: a red ring spun flat at the current target's feet (old
// __AttachSelectEffect, attach-bone "" = actor origin). Lives on the local player, follows its
// combat component's selected target, and hides itself when there is none. The ring's mesh,
// material, colour, scale, height and spin speed all come from UMT2GameplaySettings so the real
// imported effect can be swapped in from the ini. See Docs/OldGameResearch/TargetAndSelectEffect.md.
UCLASS(ClassGroup = "MT2", meta = (BlueprintSpawnableComponent))
class METIN2_API UMT2TargetIndicatorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMT2TargetIndicatorComponent();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(
		float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	UFUNCTION() void HandleSelectedTargetChanged(AActor* OldTarget, AActor* NewTarget);
	void TryBindCombat();
	void EnsureRing();
	void ShowOn(AActor* Target);
	// Places the ring at ground-level + height directly under Target (world space).
	void PositionRing(AActor* Target);
	void Hide();

	UMT2CombatComponent* ResolveCombatComponent() const;

	// The spinning ring. Created lazily from the settings mesh, re-parented under each target.
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> RingMesh;

	TWeakObjectPtr<AActor> CurrentTarget;
	TWeakObjectPtr<UMT2CombatComponent> BoundCombat;
	float CurrentSpin = 0.0f;
};
