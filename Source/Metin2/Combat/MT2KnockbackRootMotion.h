#pragma once

#include "GameFramework/RootMotionSource.h"
#include "MT2KnockbackRootMotion.generated.h"

// Legacy CEaseOutInterpolation starts at twice the average speed and decelerates to zero.
// A native source avoids transient curve assets that cannot resolve on another network peer.
USTRUCT()
struct METIN2_API FMT2KnockbackRootMotion : public FRootMotionSource_ConstantForce
{
	GENERATED_BODY()
	virtual FRootMotionSource* Clone() const override { return new FMT2KnockbackRootMotion(*this); }
	virtual UScriptStruct* GetScriptStruct() const override { return StaticStruct(); }
	virtual void PrepareRootMotion(float SimulationTime, float MovementTickTime,
		const ACharacter& Character, const UCharacterMovementComponent& MoveComponent) override;
};

template<> struct TStructOpsTypeTraits<FMT2KnockbackRootMotion> : TStructOpsTypeTraitsBase2<FMT2KnockbackRootMotion>
{
	enum { WithNetSerializer = true, WithCopy = true };
};
