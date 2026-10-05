/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Skills/MT2SkillTypes.h"

#include "Skills/MT2SkillComponent.h"

EMT2SkillMastery MT2SkillMastery::FromLevel(int32 Level)
{
	if (Level >= 40)
	{
		return EMT2SkillMastery::PerfectMaster;
	}
	if (Level >= 30)
	{
		return EMT2SkillMastery::GrandMaster;
	}
	if (Level >= 20)
	{
		return EMT2SkillMastery::Master;
	}
	return EMT2SkillMastery::Normal;
}

FString MT2SkillMastery::LevelLabel(int32 Level)
{
	if (Level >= 40) return TEXT("P");            // Perfect Master
	if (Level >= 30) return FString::Printf(TEXT("G%d"), Level - 29); // G1..G10
	if (Level >= 20) return FString::Printf(TEXT("M%d"), Level - 19); // M1..M10
	return FString::Printf(TEXT("Lv%d"), Level);  // Normal training levels
}

int32 FMT2SkillLevelContainer::GetSkillLevel(int32 SkillVnum) const
{
	const FMT2SkillLevelEntry* Entry = Entries.FindByPredicate(
		[SkillVnum](const FMT2SkillLevelEntry& Candidate) { return Candidate.SkillVnum == SkillVnum; });
	return Entry ? Entry->Level : 0;
}

void FMT2SkillLevelContainer::SetOwner(UMT2SkillComponent* InOwner)
{
	Owner = InOwner;
}

bool FMT2SkillLevelContainer::SetSkillLevel(int32 SkillVnum, int32 NewLevel)
{
	if (SkillVnum <= 0)
	{
		return false;
	}

	const int32 ClampedLevel = FMath::Clamp(NewLevel, 0, MT2SkillLimits::MaxSkillLevel);
	const int32 ExistingIndex = Entries.IndexOfByPredicate(
		[SkillVnum](const FMT2SkillLevelEntry& Candidate) { return Candidate.SkillVnum == SkillVnum; });

	if (ExistingIndex != INDEX_NONE)
	{
		if (ClampedLevel == 0)
		{
			Entries.RemoveAt(ExistingIndex);
			MarkArrayDirty();
			return true;
		}

		FMT2SkillLevelEntry& ExistingEntry = Entries[ExistingIndex];
		if (ExistingEntry.Level == ClampedLevel)
		{
			return false;
		}

		ExistingEntry.Level = ClampedLevel;
		MarkItemDirty(ExistingEntry);
		return true;
	}

	if (ClampedLevel == 0)
	{
		return false;
	}

	FMT2SkillLevelEntry& NewEntry = Entries.AddDefaulted_GetRef();
	NewEntry.SkillVnum = SkillVnum;
	NewEntry.Level = ClampedLevel;
	MarkItemDirty(NewEntry);
	return true;
}

void FMT2SkillLevelContainer::PostReplicatedReceive(
	const FFastArraySerializer::FPostReplicatedReceiveParameters& Parameters)
{
	if (UMT2SkillComponent* SkillComponent = Owner.Get())
	{
		SkillComponent->NotifySkillLevelsChanged();
	}
}
