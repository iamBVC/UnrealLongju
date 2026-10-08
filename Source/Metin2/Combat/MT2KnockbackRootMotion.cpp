#include "Combat/MT2KnockbackRootMotion.h"

void FMT2KnockbackRootMotion::PrepareRootMotion(float SimulationTime, float MovementTickTime,
	const ACharacter& Character, const UCharacterMovementComponent& MoveComponent)
{
	const FVector AverageForce = Force;
	const float Start = FMath::Clamp(GetTime(), 0.f, Duration);
	const float End = FMath::Clamp(GetTime() + SimulationTime, 0.f, Duration);
	// Integrate the linear velocity ramp over this simulation step, independent of frame rate.
	const float Scale = Duration > 0.f && SimulationTime > 0.f
		? (End - Start) / SimulationTime * (2.f - (Start + End) / Duration) : 0.f;
	Force *= Scale;
	FRootMotionSource_ConstantForce::PrepareRootMotion(SimulationTime, MovementTickTime, Character, MoveComponent);
	Force = AverageForce;
}
