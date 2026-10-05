/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "MT2GameplayAbilityBasicAttack.generated.h"

UCLASS()
class METIN2_API UMT2GameplayAbilityBasicAttack : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UMT2GameplayAbilityBasicAttack();

	virtual bool CanActivateAbility(
		FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	virtual void ActivateAbility(
		FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

protected:
	// How long the swing keeps the State.Attacking tag applied (and thus movement locked) after the
	// hit is resolved. Mirrors the old client's isLock()-for-the-duration-of-the-attack-motion behavior.
	UPROPERTY(EditDefaultsOnly, Category = "Attack", meta = (ClampMin = "0.0"))
	float AttackLockDuration = 0.5f;

private:
	void FinishAttack(FGameplayAbilitySpecHandle Handle, FGameplayAbilityActorInfo ActorInfo, FGameplayAbilityActivationInfo ActivationInfo);

	FTimerHandle AttackLockTimerHandle;
};
