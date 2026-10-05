/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Animation/AnimInstance.h"
#include "CoreMinimal.h"
#include "MT2CharacterAnimInstance.generated.h"

class UAnimSequence;

USTRUCT(BlueprintType)
struct METIN2_API FMT2AnimationSequenceSet
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation Assets")
	TArray<TObjectPtr<UAnimSequence>> Animations;
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2AnimationActionSet
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation Assets")
	TMap<FName, FMT2AnimationSequenceSet> Actions;
};

UCLASS(Abstract, Blueprintable, BlueprintType)
class METIN2_API UMT2CharacterAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "Animation")
	bool SetAnimationSet(FName AnimationSet);

	UFUNCTION(BlueprintPure, Category = "Animation")
	FName GetActiveAnimationSet() const { return ActiveAnimationSet; }

	UFUNCTION(BlueprintPure, Category = "Animation Assets")
	UAnimSequence* GetAnimation(FName AnimationSet, FName Action, int32 VariantIndex = 0) const;

	UFUNCTION(BlueprintPure, Category = "Animation Assets")
	UAnimSequence* GetActiveAnimation(FName Action, int32 VariantIndex = 0) const;

	UFUNCTION(BlueprintCallable, Category = "Animation")
	bool PlayAnimationAction(FName Action, int32 VariantIndex = 0, float PlayRate = 1.0f);

	UFUNCTION(BlueprintPure, Category = "Animation Assets|Combat")
	UAnimSequence* GetComboAnimation(FName ComboSet, int32 ComboIndex) const;

	UFUNCTION(BlueprintPure, Category = "Animation Assets|Skills")
	UAnimSequence* GetSkillAnimation(FName SkillName) const;

	UFUNCTION(BlueprintPure, Category = "Animation Assets|Horse")
	UAnimSequence* GetHorseAnimation(FName MotionName) const;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation Assets")
	TMap<FName, FMT2AnimationActionSet> AnimationSets;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation Assets")
	FName DefaultAnimationSet = TEXT("general");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation Assets|Locomotion")
	TObjectPtr<UAnimSequence> WaitAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation Assets|Locomotion")
	TObjectPtr<UAnimSequence> WalkAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation Assets|Locomotion")
	TObjectPtr<UAnimSequence> RunAnimation;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Movement")
	float GroundSpeed = 0.0f;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Movement")
	float Direction = 0.0f;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Movement")
	bool bIsMoving = false;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Movement")
	bool bIsFalling = false;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Movement")
	bool bIsAccelerating = false;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Movement")
	bool bShouldWalk = false;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Movement")
	float MovementPlayRate = 1.0f;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "State")
	bool bIsDead = false;

private:
	UAnimSequence* FindAnimation(FName AnimationSet, FName Action, int32 VariantIndex) const;

	UPROPERTY(Transient)
	FName ActiveAnimationSet = TEXT("general");
};
