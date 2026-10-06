#include "Characters/MT2CharacterMovementComponent.h"

#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "World/MT2MapAttributes.h"

bool UMT2CharacterMovementComponent::MoveUpdatedComponentImpl(const FVector& Delta,
	const FQuat& NewRotation, bool bSweep, FHitResult* OutHit, ETeleportType Teleport)
{
	// Server simulation and autonomous prediction use the same cooked attribute grid.
	// Replicated proxy corrections and deliberate teleports must not be intercepted.
	const UWorld* World = GetWorld();
	const UMT2MapAttributeSubsystem* Attributes = World ? World->GetSubsystem<UMT2MapAttributeSubsystem>() : nullptr;
	FHitResult AttributeHit;
	if (!bSweep || Teleport != ETeleportType::None || !UpdatedComponent || !CharacterOwner ||
		CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy || !Attributes ||
		!Attributes->TraceBlockedMovement(UpdatedComponent->GetComponentLocation(),
			UpdatedComponent->GetComponentLocation() + Delta, AttributeHit))
	{
		return Super::MoveUpdatedComponentImpl(Delta, NewRotation, bSweep, OutHit, Teleport);
	}
	// Stop just before the boundary, while retaining ordinary capsule collision and sliding.
	const double Backoff = 0.1 / FMath::Max(Delta.Size2D(), 0.1);
	const double Fraction = FMath::Max(0.0, double(AttributeHit.Time) - Backoff);
	FHitResult PhysicalHit;
	const bool bMoved = Super::MoveUpdatedComponentImpl(Delta * Fraction, NewRotation, bSweep, &PhysicalHit, Teleport);
	if (OutHit)
	{
		if (PhysicalHit.bBlockingHit)
		{
			*OutHit = PhysicalHit;
			OutHit->Time *= Fraction;
			OutHit->TraceEnd = AttributeHit.TraceEnd;
		}
		else
		{
			*OutHit = AttributeHit;
			OutHit->Time = Fraction;
			OutHit->Location = UpdatedComponent->GetComponentLocation();
		}
	}
	return bMoved;
}
