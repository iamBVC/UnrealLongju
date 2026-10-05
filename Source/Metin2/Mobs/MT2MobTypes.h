/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "MT2MobTypes.generated.h"

class UAnimSequence;

UENUM(BlueprintType)
enum class EMT2MobRank : uint8
{
	Pawn,
	SpecialPawn,
	Knight,
	SpecialKnight,
	Boss,
	King
};

UENUM(BlueprintType)
enum class EMT2MobType : uint8
{
	Monster,
	NPC,
	Stone,
	Warp,
	Door,
	Building,
	Player,
	PolymorphPlayer,
	Horse,
	Goto
};

UENUM(BlueprintType)
enum class EMT2MobBattleType : uint8
{
	Melee,
	Range,
	Magic,
	Special,
	Power,
	Tanker,
	SuperPower,
	SuperTanker
};

UENUM(BlueprintType)
enum class EMT2MobSize : uint8
{
	Reserved,
	Small,
	Medium,
	Large
};

UENUM(BlueprintType, meta = (Bitflags))
enum class EMT2MobAIFlag : uint8
{
	Aggressive = 0 UMETA(DisplayName = "Aggressive"),
	NoMove = 1 UMETA(DisplayName = "Cannot Move"),
	Coward = 2 UMETA(DisplayName = "Coward"),
	NoAttackShinsoo = 3,
	NoAttackChunjo = 4,
	NoAttackJinno = 5,
	AttackMobs = 6,
	Berserk = 7,
	StoneSkin = 8,
	GodSpeed = 9,
	DeathBlow = 10,
	Revive = 11
};

UENUM(BlueprintType)
enum class EMT2MobMotion : uint8
{
	Wait,
	Walk,
	Run,
	NormalAttack,
	FrontDamage,
	FrontDead,
	FrontKnockdown,
	FrontStandup,
	BackDamage,
	BackDead,
	BackKnockdown,
	Special1,
	Special2,
	Special3,
	Special4,
	Special5
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2MobSkillDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 SkillVnum = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 Level = 0;
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2MobMotionVariant
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSoftObjectPtr<UAnimSequence> Animation;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "1"))
	int32 Weight = 100;
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2MobMotionVariants
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FMT2MobMotionVariant> Items;
};

// How many of one mob a spawn group contains. group.txt has no count column - it repeats a mob on
// consecutive member lines ("Leader Wildhund 101 / 1 Wildhund 101 / 2 Wildhund 101" = 3 Wildhunds),
// so the importer folds those repeats into a count.
USTRUCT(BlueprintType)
struct METIN2_API FMT2MetinSpawnEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metin")
	int32 MobVnum = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metin", meta = (ClampMin = "1"))
	int32 Count = 1;
};

// One spawn group a metin stone can summon: the old group.txt "Group { Vnum N ... }" block. The
// leader is member[0] of the old CMobGroup and IS spawned along with the numbered members.
USTRUCT(BlueprintType)
struct METIN2_API FMT2MetinSpawnGroup
{
	GENERATED_BODY()

	// group.txt Vnum (what the old SpawnGroup call took).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metin")
	int32 GroupVnum = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metin")
	FString GroupName;

	// Leader first, then the members, with repeats folded into Count.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metin")
	TArray<FMT2MetinSpawnEntry> Entries;
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2MobMotionSet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TMap<EMT2MobMotion, FMT2MobMotionVariants> Motions;
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2MobDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 Vnum = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FString InternalName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FString DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FString ResourceName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FString SourceFolder;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	EMT2MobRank Rank = EMT2MobRank::Pawn;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	EMT2MobType Type = EMT2MobType::Monster;

	// mob_proto bOnClickType (0 none, 1 shop, 2 talk) - drives NPC interaction.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mob")
	int32 OnClickType = 0;

	// Metin stones only: the spawn groups this stone can summon, resolved by the importer from the
	// proto's ATTACK_SPEED..MOVING_SPEED vnum range against group.txt.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mob")
	TArray<FMT2MetinSpawnGroup> MetinSpawnGroups;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	EMT2MobBattleType BattleType = EMT2MobBattleType::Melee;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	EMT2MobSize Size = EMT2MobSize::Medium;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visuals", meta = (ClampMin = "1"))
	int32 ScalePercent = 100;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Progression", meta = (ClampMin = "1"))
	int32 Level = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 Empire = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI", meta = (Bitmask, BitmaskEnum = "/Script/Metin2.EMT2MobAIFlag"))
	int32 AIFlags = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI")
	int32 ExtendedAIFlags = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flags")
	int32 RaceFlags = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flags")
	int32 ImmuneFlags = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")
	int32 Strength = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")
	int32 Dexterity = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")
	int32 Constitution = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")
	int32 Intelligence = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats", meta = (ClampMin = "1"))
	int32 MaxHealth = 100;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")
	int32 Defense = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	int32 DamageMin = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	int32 DamageMax = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float DamageMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	int32 AttackSpeed = 100;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
	int32 MovementSpeed = 100;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI", meta = (Units = "cm"))
	float AttackRange = 175.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (Units = "cm"))
	float HitRange = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI", meta = (Units = "cm"))
	float AggressiveSight = 2000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI")
	int32 AggressiveHealthPercent = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Regeneration", meta = (Units = "s"))
	float RegenerationCycle = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Regeneration")
	float RegenerationPercent = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rewards")
	int64 ExperienceReward = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rewards")
	int64 NewWorldExperienceReward = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rewards")
	int64 GoldMin = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rewards")
	int64 GoldMax = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rewards")
	int32 DropItemVnum = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawning")
	int32 ResurrectionVnum = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawning")
	int32 SummonVnum = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	TArray<int32> Enchants;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	TArray<int32> Resistances;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	TArray<int32> ElementalAttackValues;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skills")
	TArray<FMT2MobSkillDefinition> Skills;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Special")
	int32 BerserkHealthPercent = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Special")
	int32 StoneSkinHealthPercent = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Special")
	int32 GodSpeedHealthPercent = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Special")
	int32 DeathBlowHealthPercent = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Special")
	int32 ReviveHealthPercent = 0;
};
