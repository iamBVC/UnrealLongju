/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Player/MT2PlayerTypes.h"
#include "MT2CharacterAppearanceSettings.generated.h"

class USkeletalMesh;
class UTexture2D;
class UAnimInstance;

USTRUCT(BlueprintType)
struct METIN2_API FMT2CharacterAppearanceAsset
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Character")
	FMT2CharacterAppearance Appearance;

	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Character")
	TSoftObjectPtr<USkeletalMesh> Mesh;

	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Character")
	TSoftObjectPtr<UTexture2D> DiffuseTexture;

	// Character-window portrait. Keeping this as a serialized soft reference makes the cooker
	// include it in shipping builds; raw LoadObject path strings used by the old widget did not.
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Character")
	TSoftObjectPtr<UTexture2D> FaceTexture;

	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Character")
	TSoftObjectPtr<USkeletalMesh> DefaultHairMesh;

	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Character")
	TSoftObjectPtr<UTexture2D> DefaultHairTexture;
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2CharacterAnimationProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Character")
	EMT2CharacterRace Race = EMT2CharacterRace::Warrior;

	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Character")
	EMT2CharacterSex Sex = EMT2CharacterSex::Male;

	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Character")
	TSoftClassPtr<UAnimInstance> AnimInstanceClass;
};

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "MT2 Character Appearance"))
class METIN2_API UMT2CharacterAppearanceSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UMT2CharacterAppearanceSettings();

	const FMT2CharacterAppearanceAsset* FindAppearance(const FMT2CharacterAppearance& Appearance) const;
	const FMT2CharacterAnimationProfile* FindAnimationProfile(
		EMT2CharacterRace Race, EMT2CharacterSex Sex) const;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }

private:
	UPROPERTY(EditAnywhere, Config, Category = "Character")
	TArray<FMT2CharacterAppearanceAsset> Appearances;

	UPROPERTY(EditAnywhere, Config, Category = "Animation")
	TArray<FMT2CharacterAnimationProfile> AnimationProfiles;
};
