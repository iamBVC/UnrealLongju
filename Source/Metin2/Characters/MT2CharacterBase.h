/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "MT2CharacterBase.generated.h"

class UMT2CombatComponent;
class UMT2CombatStatsComponent;
class UMT2CharacterAppearanceComponent;
class UMT2HealthComponent;
class UMT2ManaComponent;
class UMT2MotionEffectComponent;
class UMT2StaminaComponent;
class UMT2MovementSpeedComponent;
class UMT2NameplateComponent;
class UMT2EquipmentComponent;
class UMT2StatusEffectComponent;
class UAbilitySystemComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;

UCLASS(Abstract, Blueprintable)
class METIN2_API AMT2CharacterBase : public ACharacter
{
	GENERATED_BODY()

public:
	AMT2CharacterBase(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Combat")
	UMT2CombatComponent* GetCombatComponent() const { return CombatComponent; }

	// Whether this character can be selected as a combat target and take damage. NPCs override to
	// false so they can never be attacked or auto-targeted (old game: NPCs are non-combatant mobs).
	UFUNCTION(BlueprintPure, Category = "Combat")
	virtual bool IsAttackable() const { return true; }

	// Whether knockback may move this character. Mirrors the old CRUSH check that skips victims
	// with AIFLAG_NOMOVE (stones, doors); those get hit but never slide.
	UFUNCTION(BlueprintPure, Category = "Combat")
	virtual bool CanBeKnockedBack() const { return true; }

	// Player characters override to true so player-vs-player hostility can use its dedicated policy.
	UFUNCTION(BlueprintPure, Category = "Combat")
	virtual bool IsPlayerFaction() const { return false; }

	// Old battle_is_attackable / IsAttackableInstance: since attacks hit everything they collide
	// with, this is what stops a swing from mowing down the attacker's own side.
	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsHostileTo(const AActor* Other) const;

	// PvP gate: whether THIS character may damage that other player-faction character. Base is false;
	// AMT2PlayerCharacter applies duel, empire, karma and aggressive-mode rules.
	virtual bool IsPvPEnabledAgainst(const AMT2CharacterBase* Other) const { return false; }

	// Slides this character away from Instigator by Distance (old CRUSH: Sync + Goto, i.e. a
	// travelled slide rather than a teleport). Server-authoritative, replicated to clients.
	UFUNCTION(NetMulticast, Unreliable, Category = "Combat")
	void MulticastKnockback(FVector_NetQuantizeNormal Direction, float Distance);

	void ApplyKnockback(const AActor* Instigator, float Distance);

	UFUNCTION(BlueprintPure, Category = "Stats|Combat")
	UMT2CombatStatsComponent* GetCombatStatsComponent() const { return CombatStatsComponent; }

	UFUNCTION(BlueprintPure, Category = "Appearance")
	UMT2CharacterAppearanceComponent* GetAppearanceComponent() const { return AppearanceComponent; }

	UFUNCTION(BlueprintPure, Category = "Attributes")
	UMT2HealthComponent* GetHealthComponent() const { return HealthComponent; }

	UFUNCTION(BlueprintPure, Category = "Attributes")
	UMT2ManaComponent* GetManaComponent() const { return ManaComponent; }

	UFUNCTION(BlueprintPure, Category = "Attributes")
	UMT2StaminaComponent* GetStaminaComponent() const { return StaminaComponent; }

	UFUNCTION(BlueprintPure, Category = "Movement")
	UMT2MovementSpeedComponent* GetMovementSpeedComponent() const { return MovementSpeedComponent; }

	UFUNCTION(BlueprintPure, Category = "Effects")
	UMT2MotionEffectComponent* GetMotionEffectComponent() const { return MotionEffectComponent; }

	UFUNCTION(BlueprintPure, Category = "Movement")
	bool IsWalkRequested() const { return bWalkRequested; }

	UFUNCTION(BlueprintCallable, Category = "Movement")
	void SetWalkRequested(bool bNewWalkRequested);

	UFUNCTION(BlueprintPure, Category = "Equipment")
	UMT2EquipmentComponent* GetEquipmentComponent() const { return EquipmentComponent; }

	UFUNCTION(BlueprintPure, Category = "Status Effects")
	UMT2StatusEffectComponent* GetStatusEffectComponent() const { return StatusEffectComponent; }

	UFUNCTION(BlueprintPure, Category = "UI")
	UMT2NameplateComponent* GetNameplateComponent() const { return NameplateComponent; }

	UFUNCTION(BlueprintPure, Category = "Animation")
	FName GetAnimationSet() const { return AnimationSet; }

	UFUNCTION(BlueprintCallable, Category = "Animation")
	void SetAnimationSet(FName NewAnimationSet);

	UFUNCTION(BlueprintCallable, Category = "Animation")
	void SetWeaponAnimationSet(int32 WeaponSubType, bool bMounted);

	// Authoritative action playback shared by attacks and skills. The action identity is sent instead
	// of an asset reference so every client resolves it from its race/gender animation profile.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Animation")
	void PlayReplicatedAnimationAction(
		FName Action, int32 VariantIndex = 0, float PlayRate = 1.0f,
		FName AnimationSetOverride = NAME_None, float MovementLockDuration = 0.0f,
		bool bOwnerPredicted = false);

protected:
	void InitializeAbilityComponents(UAbilitySystemComponent* AbilitySystemComponent);

private:
	UFUNCTION()
	void OnRep_WalkRequested();

	UFUNCTION()
	void OnRep_AnimationSet();

	void ApplyAnimationSetToInstance();
	void RestoreMovementOrientationAfterAction();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayAnimationAction(
		FName AnimationSetName, FName Action, int32 VariantIndex, float PlayRate,
		float MovementLockDuration, bool bOwnerPredicted);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2CombatComponent> CombatComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats|Combat", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2CombatStatsComponent> CombatStatsComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Appearance", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2CharacterAppearanceComponent> AppearanceComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2HealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2ManaComponent> ManaComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attributes", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2StaminaComponent> StaminaComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2MovementSpeedComponent> MovementSpeedComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Equipment", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2EquipmentComponent> EquipmentComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Status Effects", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2StatusEffectComponent> StatusEffectComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Effects", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2MotionEffectComponent> MotionEffectComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2NameplateComponent> NameplateComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Equipment", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USkeletalMeshComponent> HairMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Equipment", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> WeaponMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Equipment", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> LeftWeaponMeshComponent;

	UPROPERTY(ReplicatedUsing = OnRep_WalkRequested)
	bool bWalkRequested = false;

	UPROPERTY(ReplicatedUsing = OnRep_AnimationSet)
	FName AnimationSet = TEXT("general");

	FTimerHandle ReplicatedActionMovementTimer;
};
