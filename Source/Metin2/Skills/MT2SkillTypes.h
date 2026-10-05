/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "MT2SkillTypes.generated.h"

class UMT2SkillComponent;

namespace MT2SkillLimits
{
	inline constexpr int32 MaxSkillLevel = 40;
	inline constexpr int32 SkillGroupSelectLevel = 5;
}

// Why a cast was refused. The old client raises these as OnCannotUseSkill(vid, "<name>") and prints
// localeInfo.USE_SKILL_ERROR_TAIL_DICT[name] over the character; we surface the same text in chat.
UENUM(BlueprintType)
enum class EMT2SkillDenyReason : uint8
{
	NeedTarget,
	NotMatchableWeapon,
	WaitCooltime,
	NotEnoughSP,
	NotEnoughHP,
	MustRide,
	CannotUseMounted
};

// Which end of a skill formula's random range to evaluate. The old client forces the poly's random
// terms to their min and max (CPoly RANDOM_TYPE_FORCE_MIN/MAX) to print the tooltip's "min - max";
// an actual cast rolls between them.
UENUM(BlueprintType)
enum class EMT2SkillFormulaValue : uint8
{
	Min,
	Max,
	Random
};

// Old server master types, derived from level exactly like CHARACTER::SetSkillLevel:
// 0-19 Normal, 20-29 Master, 30-39 Grand Master, 40 Perfect Master.
UENUM(BlueprintType)
enum class EMT2SkillMastery : uint8
{
	Normal,
	Master,
	GrandMaster,
	PerfectMaster
};

namespace MT2SkillMastery
{
	METIN2_API EMT2SkillMastery FromLevel(int32 Level);

	// Player-facing grade label for a raw skill level: "M1".."M10", "G1".."G10", "P", or "Lv{n}".
	METIN2_API FString LevelLabel(int32 Level);
}

// Outcome of reading one skill book (old char_skill.cpp LearnSkillByBook, master path).
UENUM(BlueprintType)
enum class EMT2SkillBookResult : uint8
{
	Denied,      // not eligible: skill not at Master mastery, no book, or no exp to spend
	Failed,      // exp was spent but the read made no progress (the ~65% miss)
	Progressed,  // one successful read closer to the next mastery level
	LeveledUp    // advanced M(n) -> M(n+1), or M10 -> G1
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2SkillLevelEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	// Old-game skill vnum (matches UMT2SkillDefinition::Vnum).
	UPROPERTY(BlueprintReadOnly, Category = "Skills")
	int32 SkillVnum = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Skills")
	int32 Level = 0;
};

USTRUCT()
struct METIN2_API FMT2SkillLevelContainer : public FFastArraySerializer
{
	GENERATED_BODY()

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParameters)
	{
		return FastArrayDeltaSerialize<FMT2SkillLevelEntry, FMT2SkillLevelContainer>(Entries, DeltaParameters, *this);
	}

	int32 GetSkillLevel(int32 SkillVnum) const;
	bool SetSkillLevel(int32 SkillVnum, int32 NewLevel);
	const TArray<FMT2SkillLevelEntry>& GetEntries() const { return Entries; }
	void SetOwner(UMT2SkillComponent* InOwner);
	void PostReplicatedReceive(const FFastArraySerializer::FPostReplicatedReceiveParameters& Parameters);

private:
	UPROPERTY()
	TArray<FMT2SkillLevelEntry> Entries;

	TWeakObjectPtr<UMT2SkillComponent> Owner;
};

template<>
struct TStructOpsTypeTraits<FMT2SkillLevelContainer> : TStructOpsTypeTraitsBase2<FMT2SkillLevelContainer>
{
	enum
	{
		WithNetDeltaSerializer = true
	};
};
