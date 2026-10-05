/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Abilities/MT2GameplayAbilityBasicAttack.h"

#include "Abilities/MT2GameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Combat/MT2CombatComponent.h"
#include "Characters/MT2CharacterBase.h"
#include "Engine/World.h"
#include "TimerManager.h"

UMT2GameplayAbilityBasicAttack::UMT2GameplayAbilityBasicAttack()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(MT2GameplayTags::Ability_Attack);
	SetAssetTags(AssetTags);

	// Owned for the lifetime of the activation, i.e. until FinishAttack() ends the ability - this is
	// what AMT2PlayerCharacter checks to block free movement input while a swing is in progress.
	ActivationOwnedTags.AddTag(MT2GameplayTags::State_Attacking);
}

bool UMT2GameplayAbilityBasicAttack::CanActivateAbility(
	FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	return ActorInfo && ActorInfo->AbilitySystemComponent.IsValid() &&
		!ActorInfo->AbilitySystemComponent->HasMatchingGameplayTag(MT2GameplayTags::Status_Dead);
}

void UMT2GameplayAbilityBasicAttack::ActivateAbility(
	FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AActor* AvatarActor = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	float EffectiveAttackLockDuration = AttackLockDuration;
	if (AvatarActor)
	{
		if (const AMT2CharacterBase* Character = Cast<AMT2CharacterBase>(AvatarActor))
		{
			UMT2CombatComponent* Combat = Character->GetCombatComponent();
			Combat->PerformBasicAttack();
			// End shortly before the next .msa input window so held attacks cannot lose an entire cycle.
			EffectiveAttackLockDuration = FMath::Max(Combat->GetCurrentAttackInputWindow() - 0.03f, 0.05f);
		}
	}

	UWorld* World = GetWorld();
	if (World && ActorInfo && EffectiveAttackLockDuration > 0.0f)
	{
		// Keep State.Attacking (and therefore the movement lock) applied for the rest of the swing
		// instead of ending the ability the instant damage resolves.
		FTimerDelegate FinishDelegate = FTimerDelegate::CreateUObject(
			this, &UMT2GameplayAbilityBasicAttack::FinishAttack, Handle, *ActorInfo, ActivationInfo);
		World->GetTimerManager().SetTimer(AttackLockTimerHandle, FinishDelegate, EffectiveAttackLockDuration, false);
	}
	else
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
	}
}

void UMT2GameplayAbilityBasicAttack::FinishAttack(
	FGameplayAbilitySpecHandle Handle, FGameplayAbilityActorInfo ActorInfo, FGameplayAbilityActivationInfo ActivationInfo)
{
	EndAbility(Handle, &ActorInfo, ActivationInfo, true, false);
}
