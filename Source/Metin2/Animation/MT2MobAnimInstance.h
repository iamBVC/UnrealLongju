/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Animation/MT2CharacterAnimInstance.h"
#include "Mobs/MT2MobTypes.h"
#include "MT2MobAnimInstance.generated.h"

UCLASS(Abstract, Blueprintable, BlueprintType)
class METIN2_API UMT2MobAnimInstance : public UMT2CharacterAnimInstance
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation Assets|Mob")
	TMap<EMT2MobMotion, TObjectPtr<UAnimSequence>> MotionAnimations;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Mob")
	bool bIsAttacking = false;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Mob")
	bool bIsReturningHome = false;
};
