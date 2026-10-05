/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Engine/DataAsset.h"
#include "UI/MT2AtlasTypes.h"
#include "MT2SkillDefinition.generated.h"

class UAnimSequence;
class UParticleSystem;
class UTexture2D;

// Persistent skill effects use the old client's attachment model. Most effects are attached to a
// body bone; weapon effects may instead follow the equipped weapon component directly.
UENUM(BlueprintType)
enum class EMT2SkillVisualTarget : uint8
{
	CharacterMesh,
	WeaponMesh
};

// Aura of the Sword is the one special case in the old client: it chooses a different looping
// effect for one-handed and two-handed weapon modes. Other effects use Any.
UENUM(BlueprintType)
enum class EMT2SkillVisualWeaponMode : uint8
{
	Any,
	OneHanded,
	TwoHanded
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2SkillPersistentVisual
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	TSoftObjectPtr<UParticleSystem> Effect;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	EMT2SkillVisualTarget Target = EMT2SkillVisualTarget::CharacterMesh;

	// Empty means the component root. Imported Granny bone names use underscores in UE.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	FName AttachSocket = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	FTransform RelativeTransform = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	EMT2SkillVisualWeaponMode WeaponMode = EMT2SkillVisualWeaponMode::Any;

	// Gyeonggong and Kwaesok only show their trails while the character is moving in the old client.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	bool bOnlyWhileMoving = false;
};

// Old client CPythonSkill skill categories (skilldesc TYPE column).
UENUM(BlueprintType)
enum class EMT2SkillType : uint8
{
	Active,
	Support,
	Guild,
	Horse
};

// skilltable attack-type column: decides which base damage feeds the "atk" formula variable.
UENUM(BlueprintType)
enum class EMT2SkillAttackType : uint8
{
	Normal,
	Melee,
	Range,
	Magic
};

// Old server ESkillFlags (skill.h), bit-identical so imported protos map 1:1.
UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EMT2SkillFlag : uint8
{
	None = 0 UMETA(Hidden),
	Attack = 0 UMETA(ToolTip = "SKILL_FLAG_ATTACK (1<<0)"),
	UseMeleeDamage = 1,
	ComputeAttackGrade = 2,
	SelfOnly = 3,
	UseMagicDamage = 4,
	UseHPAsCost = 5,
	ComputeMagicDamage = 6,
	Splash = 7,
	GivePenalty = 8,
	UseArrowDamage = 9,
	Penetrate = 10,
	IgnoreTargetRating = 11,
	AttackSlow = 12,
	AttackStun = 13,
	HPAbsorb = 14,
	SPAbsorb = 15,
	AttackFireContinuous = 16,
	RemoveBadAffect = 17,
	RemoveGoodAffect = 18,
	Crush = 19,
	AttackPoison = 20,
	Toggle = 21,
	DisableByPointUp = 22,
	CrushLong = 23,
	AttributeWind = 24,
	AttributeElec = 25,
	AttributeFire = 26
};

// Cast animation of one mastery grade (the old client plays a different motion per grade). Male
// and female bodies have separate rigs in the imported content (pc/ vs pc2/). Mobs do NOT use
// these: a mob casting this skill plays the skill motion from its own FMT2MobMotionSet, exactly
// like the old game resolved mob skill motions from the mob's msm instead of the player tables.
USTRUCT(BlueprintType)
struct METIN2_API FMT2SkillGradeAnimation
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TSoftObjectPtr<UAnimSequence> MaleAnimation;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TSoftObjectPtr<UAnimSequence> FemaleAnimation;
};

// One of the up-to-three (point, formula) pairs a skill applies (bPointOn/kPointPoly slots of
// CSkillProto). ApplyType uses the old POINT_* / EApplyTypes ordinals already shared with the
// item-apply and status-effect pipelines.
USTRUCT(BlueprintType)
struct METIN2_API FMT2SkillApplyDefinition
{
	GENERATED_BODY()

	// Raw old-game POINT_* name from skilltable (authoritative; e.g. "HP", "ATT_SPEED").
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Apply")
	FString PointOnName;

	// Best-effort EApplyTypes ordinal for the status-effect pipeline; 0 when no APPLY equivalent
	// exists (HP damage is executed through the damage path, not as an apply).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Apply")
	int32 ApplyType = 0;

	// Verbatim old-game poly (e.g. "-(1.1*atk + (0.3*atk + 0.5*str + wep)*k)"). Stat-dependent
	// formulas are evaluated at cast time; ValueByLevel below can pre-bake stat-free ones.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Apply")
	FString ValueFormula;

	// Pre-baked value per skill level (X = level) for formulas that only depend on k. Ignored when
	// the runtime evaluator resolves ValueFormula.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Apply")
	FRuntimeFloatCurve ValueByLevel;

	// Buff duration in seconds per skill level; a flat 0 means instant application (damage).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Apply")
	FString DurationFormula;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Apply")
	FRuntimeFloatCurve DurationByLevel;

	// Old dwAffectFlag granted while this apply's buff runs (AFF_* ordinal bit).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Apply")
	int32 AffectFlags = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Apply")
	bool bRemoveOnDeath = true;

	// Third proto slot only kicks in at Grand Master mastery (bPointOn3 rule).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Apply")
	bool bGrandMasterOnly = false;
};

// One skill of the old game, authored as a data asset (create instances of this class, or
// Blueprint subclasses when a skill needs bespoke logic later). Mirrors CSkillProto (server
// skilltable) merged with the client's skilldesc presentation columns.
UCLASS(BlueprintType, Blueprintable)
class METIN2_API UMT2SkillDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId(TEXT("Skill"), GetFName());
	}

	UFUNCTION(BlueprintPure, Category = "Skill")
	bool HasSkillFlag(EMT2SkillFlag Flag) const { return (Flags & (1 << static_cast<int32>(Flag))) != 0; }

	// The old k variable: SkillPowerPercent(level) * MaxLevel / 100.
	UFUNCTION(BlueprintPure, Category = "Skill")
	float GetPowerFactor(int32 SkillLevel) const;

	UFUNCTION(BlueprintPure, Category = "Skill")
	FText GetGradeDisplayName(int32 MasteryGrade) const;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity", meta = (ClampMin = "1"))
	int32 Vnum = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	FString InternalName;

	// Grade-dependent names from skilldesc (base / Master / Grand Master); index clamped.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	TArray<FText> GradeNames;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity", meta = (MultiLine = "true"))
	FText Description;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	EMT2SkillType SkillType = EMT2SkillType::Active;

	// Raw skilltable dwType: support=0, jobs=1..4, horse=5, anti-skills=6.
	// INDEX_NONE identifies assets that still need the legacy metadata refresh.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rules")
	int32 LegacySkillType = INDEX_NONE;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	EMT2SkillAttackType AttackType = EMT2SkillAttackType::Normal;

	// Skill icons are 32x32 cells of the old Skill{Job}.dds atlases (imported as T_skill{job}).
	// Active skills have one region per mastery grade ({icon}_01.._03.sub); support/horse skills
	// reuse a single region for every grade.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<UTexture2D> IconAtlas;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TArray<FMT2AtlasRect> IconRegionsByGrade;

	UFUNCTION(BlueprintPure, Category = "Skill")
	FMT2AtlasRect GetIconRegion(int32 MasteryGrade) const
	{
		if (IconRegionsByGrade.IsEmpty())
		{
			return FMT2AtlasRect(0.0f, 0.0f, 0.0f, 0.0f);
		}
		return IconRegionsByGrade[FMath::Clamp(MasteryGrade, 0, IconRegionsByGrade.Num() - 1)];
	}

	// Raw skilldesc ATTRIBUTE column (e.g. "ATTACK_SKILL|NEED_TARGET|WEAPON_LIMITATION") - the
	// client-side behavior flags; kept verbatim until the cast pipeline consumes them typed.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rules")
	FString ClientAttributeNames;

	// One old SKILL_ATTRIBUTE_* name (client PythonSkill.h), e.g. "NEED_TARGET", "SEARCH_TARGET",
	// "CAN_USE_FOR_ME". Matches whole names only: a plain Contains would make "TARGET" match
	// "NEED_TARGET", and "USE_HP" match "CAN_USE_IF_NOT_ENOUGH"-style neighbours.
	UFUNCTION(BlueprintPure, Category = "Skill")
	bool HasClientAttribute(const FString& AttributeName) const;

	// Raw skilldesc WEAPON column (e.g. "SWORD|TWO_HANDED"); empty = no weapon restriction.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rules")
	FString WeaponLimitationNames;

	// EMT2SkillFlag bitmask (1 << flag ordinal), matching the old dwFlag verbatim.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rules", meta = (Bitmask, BitmaskEnum = "/Script/Metin2.EMT2SkillFlag"))
	int32 Flags = 0;

	// Learnable level cap: 40 for job skills (global SKILL_MAX_LEVEL), the proto bMaxLevel for
	// support skills (Combo caps at 2, Riding at 20...).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rules", meta = (ClampMin = "1", ClampMax = "40"))
	int32 MaxLevel = 40;

	// The proto's raw bMaxLevel: the scale of the k variable (k = power% * PowerScale / 100), NOT
	// a level cap. Almost always 1 for player skills.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rules", meta = (ClampMin = "1"))
	int32 PowerScale = 1;

	// Character level required to learn (bLevelLimit).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rules", meta = (ClampMin = "0"))
	int32 LevelLimit = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rules", meta = (ClampMin = "0"))
	int32 PrerequisiteSkillVnum = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rules", meta = (ClampMin = "0"))
	int32 PrerequisiteSkillLevel = 0;

	// The old per-level skill power table (0-100, the percentage the UI shows). X axis = level.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Leveling")
	FRuntimeFloatCurve SkillPowerPercentByLevel;

	// SP (or HP with UseHPAsCost) cost per level; formula kept for the runtime evaluator.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Costs")
	FString SPCostFormula;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Costs")
	FRuntimeFloatCurve SPCostByLevel;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Costs")
	FString CooldownFormula;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Costs")
	FRuntimeFloatCurve CooldownByLevel;

	// Up to three applies like the old proto; the third is usually Grand Master only.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effects")
	TArray<FMT2SkillApplyDefinition> Applies;

	// Looping effects displayed for as long as this skill's replicated status effect is active.
	// Filled by the skill importer from playersettingmodule.py's EFFECT_AFFECT registrations.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effects|Visual")
	TArray<FMT2SkillPersistentVisual> PersistentVisuals;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Targeting", meta = (Units = "cm"))
	float TargetRange = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Targeting", meta = (Units = "cm"))
	float SplashRange = 0.0f;

	// lMaxHit: how many victims one cast may damage (0 = unlimited).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Targeting", meta = (ClampMin = "0"))
	int32 MaxHits = 0;

	// skilldesc motion index; grade count > 1 means each mastery grade has its own animation.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation", meta = (ClampMin = "0"))
	int32 MotionIndex = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation", meta = (ClampMin = "1"))
	int32 MotionGradeCount = 1;

	// Player cast animation per mastery grade (index 0 = Normal, then _2/_3/_4 grade motions).
	// Grades beyond the array reuse the last entry. Mobs use their own motion set instead.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	TArray<FMT2SkillGradeAnimation> CastAnimationsByGrade;

	UFUNCTION(BlueprintPure, Category = "Skill")
	TSoftObjectPtr<UAnimSequence> GetCastAnimation(int32 MasteryGrade, bool bFemale) const;
};
