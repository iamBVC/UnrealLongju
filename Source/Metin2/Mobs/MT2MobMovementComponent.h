/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Characters/MT2CharacterMovementComponent.h"
#include "MT2MobMovementComponent.generated.h"

UCLASS()
class METIN2_API UMT2MobMovementComponent : public UMT2CharacterMovementComponent
{
	GENERATED_BODY()

public:
	UMT2MobMovementComponent();

protected:
	bool bHadKnockbackMovement = false;
	virtual void PhysWalking(float DeltaSeconds, int32 Iterations) override;
	virtual void SimulatedTick(float DeltaSeconds) override;
	virtual void SmoothCorrection(
		const FVector& OldLocation,
		const FQuat& OldRotation,
		const FVector& NewLocation,
		const FQuat& NewRotation) override;
};
