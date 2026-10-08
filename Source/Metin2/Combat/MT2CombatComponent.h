/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Combat/MT2DamageTypes.h"
#include "Components/ActorComponent.h"
#include "TimerManager.h"
#include "MT2CombatComponent.generated.h"

class UGameplayEffect;
class UAnimSequence;
struct FMT2MotionAttackEvent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FMT2BasicAttackHitSignature, AActor*, Target, float, AppliedDamage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMT2BasicAttackPerformedSignature, int32, ComboIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FMT2SelectedTargetChangedSignature, AActor*, OldTarget, AActor*, NewTarget);

UCLASS(ClassGroup = "Combat", BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class METIN2_API UMT2CombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMT2CombatComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Combat")
	AActor* PerformBasicAttack();

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Combat")
	bool PerformBasicAttackOnTarget(AActor* TargetActor, float ExternalForce = 0, int32 HittingType = 2);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Combat")
	void ConfigureBasicAttack(float Range, float Radius, float Damage, float Interval, int32 ComboLength = 1);

	// Configures the ordered attack motion chain. LoopStartIndex is normally zero; mounted melee
	// chains use one because the old client plays 1,2,3 once and then repeats 2,3 while held.
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void ConfigureBasicAttackCombo(int32 ComboLength, int32 LoopStartIndex = 0);

	UFUNCTION(BlueprintPure, Category = "Combat")
	int32 GetBasicAttackComboLength() const { return BasicAttackComboLength; }

	UFUNCTION(BlueprintCallable, Category = "Combat")
	void StartBasicAttackLoop();

	UFUNCTION(BlueprintCallable, Category = "Combat")
	void StopBasicAttackLoop();

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsBasicAttackHeld() const { return bBasicAttackHeld; }

	// Uses the .msa ComboInputData.DirectInputTime. Normal attacks without combo data use the old
	// client's 90% fallback window.
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void NotifyAttackMotion(float InputWindowSeconds, const FVector& WorldAdvance);

	UFUNCTION(BlueprintPure, Category = "Combat")
	float GetCurrentAttackInputWindow() const { return CurrentAttackInputWindow; }

	UFUNCTION(BlueprintPure, Category = "Combat")
	FVector GetCurrentAttackWorldAdvance() const { return CurrentAttackWorldAdvance; }

	// Set by click-to-target (see AMT2PlayerCharacter::RequestClickMove). While valid, alive, and in
	// range, basic attacks go to this actor instead of whatever the proximity sweep happens to find -
	// otherwise clicking a specific target wouldn't reliably mean attacking THAT target.
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void SetSelectedTarget(AActor* NewTarget);

	// The click that selects a target happens on the client; mirror it so the server (which does
	// all the actual targeting) knows what the player picked.
	UFUNCTION(Server, Reliable, Category = "Combat")
	void ServerSetSelectedTarget(AActor* NewTarget);

	UFUNCTION(BlueprintPure, Category = "Combat")
	AActor* GetSelectedTarget() const { return SelectedTarget; }

	// True when the current selection is a living, attackable actor. Used by "target on hit" to
	// decide whether the swing should adopt what it just hit.
	UFUNCTION(BlueprintPure, Category = "Combat")
	bool HasValidSelectedTarget() const;

	UFUNCTION(BlueprintPure, Category = "Combat")
	float GetBasicAttackRange() const { return BasicAttackRange; }

	UPROPERTY(BlueprintAssignable, Category = "Combat")
	FMT2BasicAttackHitSignature OnBasicAttackHit;

	UPROPERTY(BlueprintAssignable, Category = "Combat")
	FMT2BasicAttackPerformedSignature OnBasicAttackPerformed;

	UPROPERTY(BlueprintAssignable, Category = "Combat")
	FMT2SelectedTargetChangedSignature OnSelectedTargetChanged;

	// Every hostile the swing lands on, nearest first. The old client's AttackProcess tests the
	// swing against *every* instance in the world and sends an attack per collision, so one swing
	// hits everything in its arc - it never picks a single "best" target.
	// See Docs/OldGameResearch/DamageDistributionAndAoE.md.
	UFUNCTION(BlueprintCallable, Category = "Combat")
	TArray<AActor*> FindBasicAttackTargets() const;

	// Old NEW_GetFrontInstance: the nearest hostile inside a fan in front of the owner. The fan
	// narrows with distance - 50 degrees half-angle at point blank down to 10 at 1000+ units - so a
	// far-off mob has to be almost dead ahead to be picked up. Used by SEARCH_TARGET skills.
	UFUNCTION(BlueprintCallable, Category = "Combat")
	AActor* FindFrontTarget(float MaxDistance) const;

	// Old FuncSplashDamage: every hostile within Radius of Center - which is the *target's*
	// position, not the caster's - nearest first, capped at MaxHits (the old lMaxHit; 0 = no cap).
	UFUNCTION(BlueprintCallable, Category = "Combat")
	TArray<AActor*> FindSplashTargets(const FVector& Center, float Radius, int32 MaxHits) const;

	// Applies an already-computed damage amount (e.g. from a skill formula) through the same
	// pipeline as a basic attack: defense subtraction, the damage GameplayEffect, the floating
	// number, the victim's hit reaction and the counter-attack aggro. Server only.
	bool ApplySkillDamage(AActor* TargetActor, float RawDamage,
		EMT2DamageDisplayType DisplayType = EMT2DamageDisplayType::Normal);

	// Knockback distance for the next landed hit, in old-game sliding units. Set from the attack
	// motion's .msa ExternalForce (combo finishers) or a skill's CRUSH flags; consumed per hit.
	void SetPendingKnockback(float Distance, float Duration = 0, bool bSideways = false, int32 HittingType = 2, bool bSyncPush = false)
	{
		PendingKnockbackDistance = FMath::IsFinite(Distance) ? FMath::Max(Distance, 0.0f) : 0.f;
		PendingKnockbackDuration = Duration;
		bPendingKnockbackSideways = bSideways;
		PendingHittingType = HittingType;
		bPendingSyncPush = bSyncPush;
	}
	float GetPendingKnockback() const { return PendingKnockbackDistance; }
	void SetBasicAttackMotion(UAnimSequence* Sequence, float PlayRate);
	void CancelPendingBasicAttackHits();
	static float MotionKnockbackDistance(float ExternalForce);
	// PhysicsObject.cpp: 100 integration steps at c_fFrameTime (0.02 seconds).
	static constexpr float MotionKnockbackDuration = 2.f;

private:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	bool ScheduleBasicAttackHits(AActor* TargetActor, bool bSingleTarget);
	void ExecuteBasicAttackHit(FMT2MotionAttackEvent Event, TWeakObjectPtr<AActor> TargetActor, bool bSingleTarget);
	TWeakObjectPtr<UAnimSequence> BasicAttackMotion;
	float BasicAttackPlayRate = 1.f;
	TArray<FTimerHandle> BasicAttackHitTimers;
	void RequestBasicAttack();
	void ScheduleNextBasicAttack(float DelaySeconds);
	void ResetBasicAttackCombo();
	int32 AdvanceBasicAttackCombo();
	bool ApplyBasicAttackDamage(AActor* TargetActor);

	UFUNCTION(Server, Reliable)
	void ServerResetBasicAttackCombo();

	UPROPERTY(EditDefaultsOnly, Category = "Combat", meta = (ClampMin = "0.0", Units = "cm"))
	float BasicAttackRange = 200.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat", meta = (ClampMin = "0.0", Units = "cm"))
	float BasicAttackRadius = 60.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat", meta = (ClampMin = "0.0"))
	float BasicAttackDamage = 10.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	TSubclassOf<UGameplayEffect> DamageEffectClass;

	UPROPERTY(EditDefaultsOnly, Category = "Combat", meta = (ClampMin = "0.05", Units = "s"))
	float BasicAttackInterval = 0.65f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat", meta = (ClampMin = "1"))
	int32 BasicAttackComboLength = 4;

	UPROPERTY(EditDefaultsOnly, Category = "Combat", meta = (ClampMin = "0"))
	int32 BasicAttackComboLoopStartIndex = 0;

	UPROPERTY(EditDefaultsOnly, Category = "Combat", meta = (ClampMin = "0.0", Units = "s"))
	float ComboResetDelay = 1.0f;

	// Short retry used only if the combo request reached GAS before the previous attack ability had
	// ended. Retrying promptly prevents one failed request from adding a full swing of dead time.
	UPROPERTY(EditDefaultsOnly, Category = "Combat", meta = (ClampMin = "0.01", Units = "s"))
	float BasicAttackRetryInterval = 0.03f;

	FTimerHandle BasicAttackTimer;
	bool bBasicAttackHeld = false;
	int32 CurrentComboIndex = INDEX_NONE;
	double LastAttackTime = -DBL_MAX;
	float CurrentAttackInputWindow = 0.65f;
	FVector CurrentAttackWorldAdvance = FVector::ZeroVector;
	float PendingKnockbackDistance = 0.0f;
	float PendingKnockbackDuration = 0.f;
	bool bPendingKnockbackSideways = false;
	int32 PendingHittingType = 2;
	bool bPendingSyncPush = false;

	// Replicated so the server (set-on-hit) and the owning client (click, and the target ring) agree
	// on one selection. OnRep drives the ring via OnSelectedTargetChanged.
	UPROPERTY(ReplicatedUsing = OnRep_SelectedTarget)
	TObjectPtr<AActor> SelectedTarget;

	UFUNCTION() void OnRep_SelectedTarget();
	// Fires OnSelectedTargetChanged only on a genuine change, so the optimistic client set and the
	// replicated OnRep don't double-fire the ring.
	void BroadcastTargetIfChanged(AActor* NewTarget);
	TWeakObjectPtr<AActor> LastBroadcastTarget;

	// Pushes the victim away from the attacker using PendingKnockbackDistance (then clears it).
	void ApplyPendingKnockback(AActor* TargetActor);
};
