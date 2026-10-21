/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Items/MT2ItemTypes.h"
#include "MT2ItemBonusSettings.generated.h"

UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EMT2ItemBonusTarget : uint8
{
	None = 0,
	Weapon = 1 << 0,
	Body = 1 << 1,
	Wrist = 1 << 2,
	Foots = 1 << 3,
	Neck = 1 << 4,
	Head = 1 << 5,
	Shield = 1 << 6,
	Ear = 1 << 7
};
ENUM_CLASS_FLAGS(EMT2ItemBonusTarget);

UENUM(BlueprintType)
enum class EMT2ItemBonusValueFormat : uint8
{
	Flat,
	Percent,
	Boolean
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2ItemBonusDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bonus")
	EMT2ItemBonusType ApplyType = EMT2ItemBonusType::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bonus")
	FString DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bonus")
	EMT2ItemBonusValueFormat ValueFormat = EMT2ItemBonusValueFormat::Flat;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bonus", meta = (ClampMin = "0"))
	int32 SelectionWeight = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bonus", meta = (Bitmask, BitmaskEnum = "/Script/Metin2.EMT2ItemBonusTarget"))
	int32 EligibleItemFlags = 0;

	// Maximum available bonus level for each eligible equipment type.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bonus", meta=(SemanticQuantityMap))
	TMap<EMT2ItemBonusTarget, int32> MaxLevelByItemType;

	// Values for attribute levels 1..5.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bonus", meta = (EditFixedSize))
	TArray<int32> Values;
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2NormalBonusAdditionChances
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Addition Chances", meta = (ClampMin = "0", ClampMax = "100"))
	int32 FirstAttributeSuccessPercent = 100;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Addition Chances", meta = (ClampMin = "0", ClampMax = "100"))
	int32 SecondAttributeSuccessPercent = 80;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Addition Chances", meta = (ClampMin = "0", ClampMax = "100"))
	int32 ThirdAttributeSuccessPercent = 60;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Addition Chances", meta = (ClampMin = "0", ClampMax = "100"))
	int32 FourthAttributeSuccessPercent = 50;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Addition Chances", meta = (ClampMin = "0", ClampMax = "100"))
	int32 FifthAttributeSuccessPercent = 30;

	int32 GetSuccessPercent(int32 ExistingNormalAttributeCount) const;
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2DamageAddonFormula
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Formula")
	float SkillDamageMean = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Formula", meta = (ClampMin = "0"))
	float SkillDamageDeviation = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Formula")
	int32 SkillDamageMin = -30;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Formula")
	int32 SkillDamageMax = 30;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Formula")
	int32 SkillDamageThreshold = 20;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Formula")
	float AverageDamageSkillMultiplier = -2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Formula")
	FIntPoint AverageDamageNoiseRange = FIntPoint(-8, 8);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Formula")
	FIntPoint AverageDamageLowSkillExtraRange = FIntPoint(1, 4);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Formula")
	FIntPoint AverageDamageHighSkillExtraRange = FIntPoint(1, 5);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Formula")
	EMT2ItemBonusType SkillDamageApplyType = EMT2ItemBonusType::SkillDamage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Formula")
	EMT2ItemBonusType AverageDamageApplyType = EMT2ItemBonusType::AverageDamage;
};

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "MT2 Item Bonuses"))
class METIN2_API UMT2ItemBonusSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	const FMT2ItemBonusDefinition* FindDefinition(int32 ApplyType, EMT2ItemBonusKind Kind) const;
	FString FormatBonus(int32 ApplyType, int32 Value) const;

	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Rules|Normal Bonus Addition", meta = (ShowOnlyInnerProperties))
	FMT2NormalBonusAdditionChances NormalBonusAdditionChances;

	/** Equipment requirement, not the level of the player using the beginner consumable. */
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Rules|Beginner Bonus Items", meta = (ClampMin = "0"))
	int32 BeginnerBonusMaximumRequiredLevel = 40;

	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Rules", meta = (ClampMin = "1", ClampMax = "5"))
	int32 NormalBonusAddLimit = 4;

	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Rules", meta = (ClampMin = "1", ClampMax = "5"))
	int32 NormalBonusMaximum = 5;

	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Rules", meta = (ClampMin = "1", ClampMax = "2"))
	int32 RareBonusMaximum = 2;

	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Rules")
	FMT2ItemAddonSelection DamageAddonType = FMT2ItemAddonSelection::FromLegacy(-1);

	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Rules", meta = (EditFixedSize))
	TArray<int32> AddNormalValueLevelWeights = {40, 50, 10, 0, 0};

	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Rules", meta = (EditFixedSize))
	TArray<int32> ChangeNormalValueLevelWeights = {0, 10, 40, 35, 15};

	/** Enchant Item (b): relative weights for attribute value levels one through five. */
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Rules|Level Chest Reroll", meta = (EditFixedSize))
	TArray<int32> LevelChestRerollValueLevelWeights = {0, 10, 50, 39, 1};

	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Rules", meta = (EditFixedSize))
	TArray<int32> RareValueLevelWeights = {0, 0, 0, 0, 100};

	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Rules")
	FMT2DamageAddonFormula DamageAddonFormula;

	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Definitions")
	TArray<FMT2ItemBonusDefinition> NormalBonuses;

	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Definitions")
	TArray<FMT2ItemBonusDefinition> RareBonuses;
};
