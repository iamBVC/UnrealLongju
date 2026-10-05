/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Mobs/MT2LootTypes.h"

TArray<FMT2GeneratedLootItem> MT2Loot::Roll(const TArray<FMT2LootEntry>& Entries)
{
	TMap<int32, int32> CountsByVnum;
	TMap<int32, TArray<const FMT2LootEntry*>> WeightedGroups;
	for (const FMT2LootEntry& Entry : Entries)
	{
		if (Entry.ItemVnum <= 0)
		{
			continue;
		}

		if (Entry.RollType == EMT2LootRollType::WeightedKill)
		{
			WeightedGroups.FindOrAdd(Entry.RollGroup).Add(&Entry);
			continue;
		}
		if (FMath::FRandRange(0.0f, 100.0f) > FMath::Clamp(Entry.ChancePercent, 0.0f, 100.0f))
		{
			continue;
		}

		const int32 MinCount = FMath::Max(Entry.MinCount, 1);
		const int32 Count = FMath::RandRange(MinCount, FMath::Max(Entry.MaxCount, MinCount));
		CountsByVnum.FindOrAdd(Entry.ItemVnum) += Count;
	}

	for (const TPair<int32, TArray<const FMT2LootEntry*>>& Pair : WeightedGroups)
	{
		const TArray<const FMT2LootEntry*>& Group = Pair.Value;
		if (Group.IsEmpty()) continue;
		const int32 KillAverage = FMath::Max(Group[0]->KillAverage, 1);
		if (FMath::RandRange(1, KillAverage) != 1) continue;
		int32 TotalWeight = 0;
		for (const FMT2LootEntry* Entry : Group) TotalWeight += FMath::Max(Entry->Weight, 1);
		int32 RollValue = FMath::RandRange(1, FMath::Max(TotalWeight, 1));
		for (const FMT2LootEntry* Entry : Group)
		{
			RollValue -= FMath::Max(Entry->Weight, 1);
			if (RollValue <= 0)
			{
				const int32 MinCount = FMath::Max(Entry->MinCount, 1);
				CountsByVnum.FindOrAdd(Entry->ItemVnum) +=
					FMath::RandRange(MinCount, FMath::Max(Entry->MaxCount, MinCount));
				break;
			}
		}
	}

	TArray<FMT2GeneratedLootItem> Result;
	for (const TPair<int32, int32>& Pair : CountsByVnum)
	{
		FMT2GeneratedLootItem& Item = Result.AddDefaulted_GetRef();
		Item.ItemVnum = Pair.Key;
		Item.Count = Pair.Value;
	}
	return Result;
}
