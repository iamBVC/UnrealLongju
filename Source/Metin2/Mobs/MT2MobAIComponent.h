/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "MT2MobAIComponent.generated.h"

class AAIController;
class AMT2Mob;
struct FMT2MobDefinition;

UENUM(BlueprintType)
enum class EMT2MobAIState : uint8
{
	Idle,
	Wandering,
	Chasing,
	Attacking,
	Fleeing,
	ReturningHome,
	Dead
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FMT2MobAIStateChangedSignature, EMT2MobAIState, OldState, EMT2MobAIState, NewState);

UCLASS(BlueprintType, ClassGroup = "MT2", meta = (BlueprintSpawnableComponent))
class METIN2_API UMT2MobAIComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMT2MobAIComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Writes the imported mob_proto AI fields onto this component. Called by the mob importer on the
	// Blueprint CDO; the values then persist as editable Blueprint defaults.
	void Configure(const FMT2MobDefinition& Definition);
	// Applies the NOMOVE flag's movement constraints to the owning mob; called at runtime spawn since
	// Configure only runs at import time.
	void ApplyMovementConstraints();
	void InitializeHome(const FVector& Location);
	void ApplyTickPolicy();

	UFUNCTION(BlueprintPure, Category = "Mob|AI")
	bool HasFlag(EMT2MobAIFlag Flag) const { return (AIFlags & (1 << static_cast<int32>(Flag))) != 0; }
	void TickBehavior(AAIController* Controller, float DeltaSeconds);
	float GetLegacyUpdateDelay() const;
	void NotifyDeath(AAIController* Controller);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Mob|AI")
	void SetForceAggressive(bool bForceAggressive);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Mob|AI")
	void SetTargetActor(AActor* NewTarget);

	UFUNCTION(BlueprintPure, Category = "Mob|AI")
	AActor* GetTargetActor() const { return TargetActor; }

	UFUNCTION(BlueprintPure, Category = "Mob|AI")
	EMT2MobAIState GetState() const { return State; }

	UPROPERTY(BlueprintAssignable, Category = "Mob|AI")
	FMT2MobAIStateChangedSignature OnStateChanged;

private:
	friend class FMT2LegacyMobSchedulingTest;
	void SetState(EMT2MobAIState NewState);
	AActor* FindTarget() const;
	bool IsValidTarget(const AActor* Candidate) const;
	void Wander(AMT2Mob* Mob);
	void ReturnHome(AMT2Mob* Mob);
	void FleeFromTarget(AMT2Mob* Mob);

	// Steers straight at Destination (no pathfinding); collision avoidance is whatever the
	// CharacterMovementComponent's own capsule sweeping provides. Returns true once within
	// AcceptanceRadius of the destination.
	bool MoveTowards(AMT2Mob* Mob, const FVector& Destination, float AcceptanceRadius) const;

	UPROPERTY(ReplicatedUsing = OnRep_State)
	EMT2MobAIState State = EMT2MobAIState::Idle;

	UPROPERTY(Replicated)
	TObjectPtr<AActor> TargetActor;

	UFUNCTION()
	void OnRep_State(EMT2MobAIState OldState);

	// ---- Imported mob_proto AI data. UPROPERTY so it bakes into the mob Blueprint (importer-written
	// class defaults) and stays visible/editable in the editor. ----

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mob|AI", meta = (AllowPrivateAccess = "true", Bitmask, BitmaskEnum = "/Script/Metin2.EMT2MobAIFlag"))
	int32 AIFlags = 0;

	// mob_proto extended AI flags; not wired yet.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mob|AI", meta = (AllowPrivateAccess = "true"))
	int32 ExtendedAIFlags = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mob|AI", meta = (AllowPrivateAccess = "true"))
	int32 Empire = 0;

	// mob_proto AGGRESSIVE_SIGHT: how far the mob notices targets.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mob|AI", meta = (AllowPrivateAccess = "true", Units = "cm"))
	float SightRadius = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mob|AI", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", Units = "cm"))
	float AggressiveDetectionRadius = 0.0f;

	// mob_proto AGGRESSIVE_HP_PCT: below this HP%, even passive mobs turn aggressive; not wired yet.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mob|AI", meta = (AllowPrivateAccess = "true", ClampMin = "0", ClampMax = "100"))
	int32 AggressiveHealthPercent = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mob|AI", meta = (AllowPrivateAccess = "true", Units = "cm"))
	float AttackRange = 175.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mob|AI", meta = (AllowPrivateAccess = "true", Units = "cm"))
	float LeashRadius = 0.0f;

	FVector HomeLocation = FVector::ZeroVector;
	FVector WanderTarget = FVector::ZeroVector;
	bool bHasWanderTarget = false;
	float NextDecisionTime = 0.0f;
};
