#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MT2CharacterMovementComponent.generated.h"

UCLASS()
class METIN2_API UMT2CharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()
protected:
	virtual bool MoveUpdatedComponentImpl(const FVector& Delta, const FQuat& NewRotation,
		bool bSweep, FHitResult* OutHit = nullptr, ETeleportType Teleport = ETeleportType::None) override;
};
