/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Animation/MT2CharacterAnimInstance.h"

#include "Animation/AnimSequence.h"
#include "Characters/MT2CharacterBase.h"
#include "Characters/MT2PlayerCharacter.h"
#include "Fishing/MT2FishingComponent.h"
#include "Components/MT2HealthComponent.h"
#include "Components/MT2MovementSpeedComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

UAnimSequence* UMT2CharacterAnimInstance::GetComboAnimation(FName ComboSet, int32 ComboIndex) const
{
	if (UAnimSequence* Combo = GetAnimation(ComboSet, TEXT("combo"), ComboIndex))
	{
		return Combo;
	}
	return GetAnimation(ComboSet, TEXT("attack"), ComboIndex);
}

UAnimSequence* UMT2CharacterAnimInstance::GetSkillAnimation(FName SkillName) const
{
	return GetAnimation(TEXT("skill"), SkillName, 0);
}

UAnimSequence* UMT2CharacterAnimInstance::GetHorseAnimation(FName MotionName) const
{
	FString SetName;
	FString ActionName;
	if (MotionName.ToString().Split(TEXT("."), &SetName, &ActionName))
	{
		return GetAnimation(FName(*SetName), FName(*ActionName), 0);
	}
	return GetAnimation(TEXT("horse"), MotionName, 0);
}

UAnimSequence* UMT2CharacterAnimInstance::FindAnimation(
	FName AnimationSet, FName Action, int32 VariantIndex) const
{
	const FMT2AnimationActionSet* Set = AnimationSets.Find(AnimationSet);
	const FMT2AnimationSequenceSet* Variants = Set ? Set->Actions.Find(Action) : nullptr;
	if (!Variants || Variants->Animations.IsEmpty())
	{
		return nullptr;
	}
	return Variants->Animations[FMath::Abs(VariantIndex) % Variants->Animations.Num()];
}

UAnimSequence* UMT2CharacterAnimInstance::GetAnimation(
	FName AnimationSet, FName Action, int32 VariantIndex) const
{
	const FName RequestedSet = AnimationSet.IsNone() ? ActiveAnimationSet : AnimationSet;
	if (UAnimSequence* Sequence = FindAnimation(RequestedSet, Action, VariantIndex))
	{
		return Sequence;
	}
	// Weapon-specific horse folders contain attacks and skills, while locomotion lives in the
	// shared horse folder. This mirrors the old client's MODE_HORSE fallback before MODE_GENERAL.
	if (RequestedSet.ToString().StartsWith(TEXT("horse_")))
	{
		if (UAnimSequence* HorseSequence = FindAnimation(TEXT("horse"), Action, VariantIndex))
		{
			return HorseSequence;
		}
	}
	return RequestedSet != DefaultAnimationSet
		? FindAnimation(DefaultAnimationSet, Action, VariantIndex)
		: nullptr;
}

UAnimSequence* UMT2CharacterAnimInstance::GetActiveAnimation(FName Action, int32 VariantIndex) const
{
	return GetAnimation(ActiveAnimationSet, Action, VariantIndex);
}

bool UMT2CharacterAnimInstance::SetAnimationSet(FName AnimationSet)
{
	const FName RequestedSet = AnimationSet.IsNone() ? DefaultAnimationSet : AnimationSet;
	ActiveAnimationSet = RequestedSet;
	WaitAnimation = GetActiveAnimation(TEXT("wait"));
	WalkAnimation = GetActiveAnimation(TEXT("walk"));
	RunAnimation = GetActiveAnimation(TEXT("run"));
	for (UAnimSequence* LocomotionSequence : {WaitAnimation.Get(), WalkAnimation.Get(), RunAnimation.Get()})
	{
		if (LocomotionSequence)
		{
			LocomotionSequence->bEnableRootMotion = false;
			LocomotionSequence->bForceRootLock = false;
		}
	}
	return AnimationSets.Contains(RequestedSet);
}

bool UMT2CharacterAnimInstance::PlayAnimationAction(FName Action, int32 VariantIndex, float PlayRate)
{
	UAnimSequence* Sequence = GetActiveAnimation(Action, VariantIndex);
	if (!Sequence)
	{
		return false;
	}
	return PlaySlotAnimationAsDynamicMontage(
		Sequence, TEXT("DefaultSlot"), 0.2f, 0.2f, FMath::Max(PlayRate, 0.01f), 1) != nullptr;
}

void UMT2CharacterAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	// Gameplay movement comes from CharacterMovement and .msa accumulation. Keep the authored root
	// transform in the visual pose; UE root extraction/locking distorts Granny locomotion in PIE.
	SetRootMotionMode(ERootMotionMode::NoRootMotionExtraction);
	SetAnimationSet(DefaultAnimationSet);
}

void UMT2CharacterAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	const ACharacter* Character = Cast<ACharacter>(TryGetPawnOwner());
	if (!Character)
	{
		GroundSpeed = 0.0f;
		Direction = 0.0f;
		bIsMoving = false;
		bIsFalling = false;
		bIsAccelerating = false;
		bShouldWalk = false;
		MovementPlayRate = 1.0f;
		bIsDead = false;
		return;
	}
	if (const AMT2CharacterBase* MT2Character = Cast<AMT2CharacterBase>(Character))
	{
		if (MT2Character->GetAnimationSet() != ActiveAnimationSet)
		{
			SetAnimationSet(MT2Character->GetAnimationSet());
		}
	}

	const FVector Velocity = Character->GetVelocity();
	if (const auto* Player = Cast<AMT2PlayerCharacter>(Character); Player && Player->GetFishingComponent())
	{
		WaitAnimation = Player->GetFishingComponent()->IsFishing()
			? GetAnimation(TEXT("fishing"), TEXT("fishing_wait")) : GetActiveAnimation(TEXT("wait"));
	}
	GroundSpeed = Velocity.Size2D();
	bIsMoving = GroundSpeed > 1.0f;

	const FVector LocalVelocity = Character->GetActorTransform().InverseTransformVectorNoScale(Velocity);
	Direction = bIsMoving ? FMath::RadiansToDegrees(FMath::Atan2(LocalVelocity.Y, LocalVelocity.X)) : 0.0f;

	if (const UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
	{
		bIsFalling = Movement->IsFalling();
		bIsAccelerating = !Movement->GetCurrentAcceleration().IsNearlyZero();
	}

	if (const UMT2HealthComponent* Health = Character->FindComponentByClass<UMT2HealthComponent>())
	{
		bIsDead = Health->IsDead();
	}
	if (const UMT2MovementSpeedComponent* MovementSpeed =
		Character->FindComponentByClass<UMT2MovementSpeedComponent>())
	{
		bShouldWalk = MovementSpeed->ShouldUseWalkMovement();
		MovementPlayRate = bIsMoving
			? FMath::Max(MovementSpeed->GetMovementSpeedMultiplier(), 0.01f)
			: 1.0f;
	}
	else
	{
		bShouldWalk = false;
		MovementPlayRate = 1.0f;
	}
}
