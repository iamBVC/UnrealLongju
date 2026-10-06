/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Skills/MT2SkillSet.h"
#include "Config/MT2PathSettings.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Skills/MT2SkillDefinition.h"

const TCHAR* UMT2SkillSet::SkillSetSearchRoot() { return UMT2PathSettings::Path(TEXT("SkillRoot")); }

namespace
{
	TArray<UMT2SkillSet*> LoadAllSkillSets()
	{
		IAssetRegistry& AssetRegistry =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		AssetRegistry.WaitForCompletion();

		FARFilter Filter;
		Filter.ClassPaths.Add(UMT2SkillSet::StaticClass()->GetClassPathName());
		Filter.PackagePaths.Add(UMT2SkillSet::SkillSetSearchRoot());
		Filter.bRecursivePaths = true;

		TArray<FAssetData> Assets;
		AssetRegistry.GetAssets(Filter, Assets);
		TArray<UMT2SkillSet*> Result;
		Result.Reserve(Assets.Num());
		for (const FAssetData& Asset : Assets)
		{
			if (UMT2SkillSet* SkillSet = Cast<UMT2SkillSet>(Asset.GetAsset()))
			{
				Result.Add(SkillSet);
			}
		}
		return Result;
	}
}

UMT2SkillDefinition* UMT2SkillSet::FindSkill(int32 SkillVnum) const
{
	auto MatchesVnum = [SkillVnum](const TObjectPtr<UMT2SkillDefinition>& Skill)
	{
		return Skill && Skill->Vnum == SkillVnum;
	};
	if (const TObjectPtr<UMT2SkillDefinition>* Found = ActiveSkills.FindByPredicate(MatchesVnum))
	{
		return *Found;
	}
	if (const TObjectPtr<UMT2SkillDefinition>* Found = SupportSkills.FindByPredicate(MatchesVnum))
	{
		return *Found;
	}
	return nullptr;
}

UMT2SkillSet* UMT2SkillSet::FindSkillSet(EMT2CharacterRace Race, int32 GroupIndex)
{
	for (UMT2SkillSet* SkillSet : LoadAllSkillSets())
	{
		if (SkillSet->Race == Race && SkillSet->GroupIndex == GroupIndex)
		{
			return SkillSet;
		}
	}
	return nullptr;
}

UMT2SkillDefinition* UMT2SkillSet::FindSkillAcrossSets(int32 SkillVnum)
{
	for (UMT2SkillSet* SkillSet : LoadAllSkillSets())
	{
		if (UMT2SkillDefinition* Skill = SkillSet->FindSkill(SkillVnum))
		{
			return Skill;
		}
	}
	// Some legacy anti/guild definitions are imported but not displayed in a job's skill set.
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	FARFilter Filter;
	Filter.ClassPaths.Add(UMT2SkillDefinition::StaticClass()->GetClassPathName());
	Filter.PackagePaths.Add(UMT2SkillSet::SkillSetSearchRoot());
	Filter.bRecursivePaths = true;
	TArray<FAssetData> Assets;
	AssetRegistry.GetAssets(Filter, Assets);
	for (const FAssetData& Asset : Assets)
	{
		if (UMT2SkillDefinition* Skill = Cast<UMT2SkillDefinition>(Asset.GetAsset()); Skill && Skill->Vnum == SkillVnum)
		{
			return Skill;
		}
	}
	return nullptr;
}

TArray<int32> UMT2SkillSet::GetSkillVnumsInRange(int32 MinimumVnum, int32 MaximumVnum)
{
	TSet<int32> UniqueVnums;
	for (const UMT2SkillSet* SkillSet : LoadAllSkillSets())
	{
		auto Collect = [&UniqueVnums, MinimumVnum, MaximumVnum](
			const TArray<TObjectPtr<UMT2SkillDefinition>>& Skills)
		{
			for (const UMT2SkillDefinition* Skill : Skills)
			{
				if (Skill && Skill->Vnum >= MinimumVnum && Skill->Vnum <= MaximumVnum)
				{
					UniqueVnums.Add(Skill->Vnum);
				}
			}
		};
		Collect(SkillSet->ActiveSkills);
		Collect(SkillSet->SupportSkills);
	}
	TArray<int32> Result = UniqueVnums.Array();
	Result.Sort();
	return Result;
}
