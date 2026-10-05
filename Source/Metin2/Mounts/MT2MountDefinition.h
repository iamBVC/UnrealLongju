/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MT2MountDefinition.generated.h"

class UAnimInstance;
class USkeletalMesh;

UENUM(BlueprintType)
enum class EMT2MountKind : uint8
{
	Horse,
	SpecialMount
};

// Shared data for one mount type. Gameplay code consumes this asset instead of branching on vnums.
UCLASS(BlueprintType)
class METIN2_API UMT2MountDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity", meta = (ClampMin = "1"))
	int32 Vnum = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	EMT2MountKind MountKind = EMT2MountKind::SpecialMount;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual")
	TSoftObjectPtr<USkeletalMesh> Mesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual")
	TSoftClassPtr<UAnimInstance> AnimationClass;

	// Mount mesh transform relative to the player's capsule; correct imported mesh axes here.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual")
	FTransform MountRelativeTransform = FTransform::Identity;

	// Additional authored correction after the rider is attached to the mount's saddle bone.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual")
	FTransform RiderRelativeTransform = FTransform::Identity;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.01"))
	float MovementSpeedMultiplier = 1.5f;

	// Original basic horses could move but not attack; armed horses could attack.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	bool bCanAttack = true;

	// Original military horses and selected special mounts enabled horse skills.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	bool bCanUseHorseSkills = false;
};
