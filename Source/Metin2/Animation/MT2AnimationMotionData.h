/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "MT2AnimationMotionData.generated.h"

class UParticleSystem;

USTRUCT(BlueprintType)
struct METIN2_API FMT2MotionEffectEvent
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MT2 Motion|Effects", meta = (Units = "s"))
	float TimeSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MT2 Motion|Effects")
	TSoftObjectPtr<UParticleSystem> Effect;

	// Converted from the source client's -Y-forward basis into UE local coordinates.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MT2 Motion|Effects")
	FVector RelativeLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MT2 Motion|Effects")
	FName BoneName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MT2 Motion|Effects")
	bool bIndependent = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MT2 Motion|Effects")
	bool bAttach = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MT2 Motion|Effects")
	bool bFollow = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MT2 Motion|Effects")
	FString SourceEffectPath;
};

// Runtime copy of the movement and combo timing authored in the source .msa file. The GR2 bone
// animation does not contain Metin2's model-matrix accumulation and must never drive UE root motion.
UCLASS(BlueprintType)
class METIN2_API UMT2AnimationMotionData : public UAssetUserData
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MT2 Motion", meta = (Units = "s"))
	float MotionDuration = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MT2 Motion")
	FVector Accumulation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MT2 Motion")
	bool bHasAccumulation = false;

	// .msa AttackingData: how hard this hit shoves the victim. Combo finishers are authored much
	// higher than normal hits (two-hand sword: 3/5/5 then 20 on combo_04). 0 = no push.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MT2 Motion|Attack")
	float ExternalForce = 0.0f;

	// .msa AttackingData HittingType: 1 = blow/knockback hit, 2 = normal hit.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MT2 Motion|Attack")
	int32 HittingType = 0;

	// Seconds into the motion at which each attack lands, one per .msa MotionEventType 4
	// (SPECIAL_ATTACKING) event's StartingTime. Empty for non-attack motions (buffs). More than one
	// entry = a multi-hit skill (e.g. Samyeon's three slashes). See
	// Docs/OldGameResearch/SkillSystem.md ("Skill damage timing").
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MT2 Motion|Attack")
	TArray<float> AttackHitTimes;

	// Old MotionEventType 1 events, preserving exact start time, attachment bone, local offset,
	// independence and follow behavior from the .msa file.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MT2 Motion|Effects")
	TArray<FMT2MotionEffectEvent> EffectEvents;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MT2 Motion|Combo", meta = (Units = "s"))
	float PreInputTime = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MT2 Motion|Combo", meta = (Units = "s"))
	float DirectInputTime = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MT2 Motion|Combo", meta = (Units = "s"))
	float InputLimitTime = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MT2 Motion|Combo", meta = (Units = "s"))
	float LinkTime = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MT2 Motion|Combo")
	bool bHasComboInputData = false;
};
