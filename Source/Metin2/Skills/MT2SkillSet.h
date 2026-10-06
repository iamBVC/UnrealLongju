/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Player/MT2PlayerTypes.h"
#include "MT2SkillSet.generated.h"

class UMT2SkillDefinition;

// The skills of one race + specialization (the old game's "skill group" chosen at level 5):
// 4 races x 2 groups = 8 sets, mirroring playersettingmodule's SKILL_INDEX_DICT. Authored as
// data assets under /Game/Skills; resolved at runtime by race + group index.
UCLASS(BlueprintType)
class METIN2_API UMT2SkillSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId(TEXT("SkillSet"), GetFName());
	}

	UFUNCTION(BlueprintPure, Category = "Skill Set")
	UMT2SkillDefinition* FindSkill(int32 SkillVnum) const;

	UFUNCTION(BlueprintPure, Category = "Skill Set")
	bool ContainsSkill(int32 SkillVnum) const { return FindSkill(SkillVnum) != nullptr; }

	// Loads the set matching race + group by scanning SkillSetSearchRoot. Returns null when the
	// asset does not exist yet (skill sets are authored content).
	static UMT2SkillSet* FindSkillSet(EMT2CharacterRace Race, int32 GroupIndex);
	static UMT2SkillDefinition* FindSkillAcrossSets(int32 SkillVnum);
	static TArray<int32> GetSkillVnumsInRange(int32 MinimumVnum, int32 MaximumVnum);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	EMT2CharacterRace Race = EMT2CharacterRace::Warrior;

	// 1 or 2 - the old SetSkillGroup choice.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity", meta = (ClampMin = "1", ClampMax = "2"))
	int32 GroupIndex = 1;

	// Group name shown on the selection page (e.g. Warrior: Body / Mental).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	FText GroupDisplayName;

	// Ordered like the old skill window slots.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Skills")
	TArray<TObjectPtr<UMT2SkillDefinition>> ActiveSkills;

	// Job-independent skills (Combo, Leadership, Mining, Polymorph, Horse...). Shared content -
	// point all 8 sets at the same definitions.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Skills")
	TArray<TObjectPtr<UMT2SkillDefinition>> SupportSkills;

	static const TCHAR* SkillSetSearchRoot();
};
