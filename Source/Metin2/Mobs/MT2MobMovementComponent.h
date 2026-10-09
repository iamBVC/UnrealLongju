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

USTRUCT()
struct FMT2MobMoveSegment
{
	GENERATED_BODY()
	UPROPERTY() FVector_NetQuantize10 Start = FVector::ZeroVector;
	UPROPERTY() FVector_NetQuantize10 Destination = FVector::ZeroVector;
	UPROPERTY() double ServerStartTime = 0.;
	UPROPERTY() float Duration = 0.f;
	UPROPERTY() uint32 Serial = 0;
	UPROPERTY() bool bMoving = false;
	UPROPERTY() bool bExternalMotion = false;
};

UCLASS()
class METIN2_API UMT2MobMovementComponent : public UMT2CharacterMovementComponent
{
	GENERATED_BODY()

public:
	UMT2MobMovementComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void StopMovementImmediately() override;
	void StartMoveSegment(const FVector& Destination, float AcceptanceRadius);
	void BeginExternalKnockback(const FVector& Direction, float Distance, float Duration);
	void FinishExternalKnockback();
	const FMT2MobMoveSegment& GetMoveSegment() const { return MoveSegment; }
	bool IsMoveSegmentInProgress() const;
	void CompleteExpiredMoveSegment();

protected:
	UPROPERTY(ReplicatedUsing = OnRep_MoveSegment)
	FMT2MobMoveSegment MoveSegment;
	UFUNCTION() void OnRep_MoveSegment();
	void PublishStoppedSegment();
	void UpdateReplicationFrequency(float Frequency);
	double GetMovementServerTime() const;
	FVector RequestedDestination = FVector::ZeroVector;
	float RequestedAcceptanceRadius = 0.f;
	float SegmentSpeed = 0.f;
	friend class FMT2MobMoveSegmentsTest;
	friend class FMT2KnockbackMovementTest;
	virtual void PhysWalking(float DeltaSeconds, int32 Iterations) override;
};
