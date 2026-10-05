/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Loot/MT2LootTable.h"

namespace
{
	bool IsValidEntry(const FMT2LootTableEntry& Entry)
	{
		if (Entry.Kind != EMT2LootCrateRewardKind::Item)
		{
			return Entry.MaximumAmount > 0;
		}
		const UMT2ItemTemplate* Template = Entry.ItemTemplate
			? Entry.ItemTemplate.GetDefaultObject() : nullptr;
		const int32 ItemVnum = Entry.ItemVnum > 0
			? Entry.ItemVnum : (Template ? Template->Vnum : 0);
		return Template && ItemVnum >= Template->Vnum &&
			ItemVnum <= Template->Vnum + FMath::Max(Template->VnumRange, 0) &&
			Entry.MaximumAmount > 0;
	}

	void AddResult(const FMT2LootTableEntry& Entry, TArray<FMT2LootTableResult>& Results)
	{
		if (!IsValidEntry(Entry))
		{
			return;
		}
		const int64 Minimum = FMath::Max<int64>(Entry.MinimumAmount, 0);
		const int64 Maximum = FMath::Max(Entry.MaximumAmount, Minimum);
		const int64 Amount = FMath::RandRange(Minimum, Maximum);
		if (Amount <= 0)
		{
			return;
		}

		FMT2LootTableResult& Result = Results.AddDefaulted_GetRef();
		Result.Kind = Entry.Kind;
		Result.Amount = Amount;
		if (Entry.Kind == EMT2LootCrateRewardKind::Item)
		{
			const UMT2ItemTemplate* Template = Entry.ItemTemplate.GetDefaultObject();
			FMT2ItemInstanceData ItemData = Entry.ItemData;
			Template->InitializeGeneratedInstance(ItemData);
			Result.Item = FMT2ItemSlot(
				Entry.ItemVnum > 0 ? Entry.ItemVnum : Template->Vnum, ItemData);
			Result.Item.Count = FMath::Clamp<int64>(Amount, 1, MAX_int32);
		}
	}
}

bool UMT2LootTable::HasValidRewards() const
{
	return Rewards.ContainsByPredicate([](const FMT2LootTableEntry& Entry)
	{
		return IsValidEntry(Entry);
	});
}

TArray<FMT2LootTableResult> UMT2LootTable::Roll(
	bool bAllowGuaranteedItems, float DropChanceMultiplier) const
{
	DropChanceMultiplier = FMath::Max(DropChanceMultiplier, 0.0f);
	TArray<FMT2LootTableResult> Results;
	TMap<int32, TArray<const FMT2LootTableEntry*>> WeightedGroups;
	for (const FMT2LootTableEntry& Entry : Rewards)
	{
		if (!IsValidEntry(Entry))
		{
			continue;
		}
		if (!bAllowGuaranteedItems && Entry.Kind == EMT2LootCrateRewardKind::Item &&
			Entry.RollMode == EMT2LootRollMode::Guaranteed)
		{
			continue;
		}
		if (Entry.RollMode == EMT2LootRollMode::WeightedGroup)
		{
			WeightedGroups.FindOrAdd(Entry.RollGroup).Add(&Entry);
			continue;
		}
		const float EntryChanceMultiplier = Entry.Kind == EMT2LootCrateRewardKind::Item
			? DropChanceMultiplier : 1.0f;
		if (Entry.RollMode == EMT2LootRollMode::Guaranteed ||
			FMath::FRandRange(0.0f, 100.0f) <=
			FMath::Clamp(Entry.ChancePercent * EntryChanceMultiplier, 0.0f, 100.0f))
		{
			AddResult(Entry, Results);
		}
	}

	for (const TPair<int32, TArray<const FMT2LootTableEntry*>>& Pair : WeightedGroups)
	{
		const TArray<const FMT2LootTableEntry*>& Group = Pair.Value;
		const bool bItemGroup = !Group.IsEmpty() && Group.ContainsByPredicate(
			[](const FMT2LootTableEntry* Entry)
			{
				return Entry && Entry->Kind == EMT2LootCrateRewardKind::Item;
			});
		const float GroupMultiplier = bItemGroup ? DropChanceMultiplier : 1.0f;
		const float GroupChance = Group.IsEmpty() ? 0.0f :
			FMath::Clamp(100.0f / FMath::Max(Group[0]->KillAverage, 1) *
				GroupMultiplier, 0.0f, 100.0f);
		if (Group.IsEmpty() || FMath::FRandRange(0.0f, 100.0f) > GroupChance)
		{
			continue;
		}
		int32 TotalWeight = 0;
		for (const FMT2LootTableEntry* Entry : Group)
		{
			TotalWeight += FMath::Max(Entry->Weight, 0);
		}
		if (TotalWeight <= 0)
		{
			continue;
		}
		int32 RollValue = FMath::RandRange(1, TotalWeight);
		for (const FMT2LootTableEntry* Entry : Group)
		{
			RollValue -= FMath::Max(Entry->Weight, 0);
			if (RollValue <= 0)
			{
				AddResult(*Entry, Results);
				break;
			}
		}
	}
	return Results;
}

int64 UMT2LootTable::GetGuaranteedAmount(EMT2LootCrateRewardKind Kind) const
{
	int64 Total = 0;
	for (const FMT2LootTableEntry& Entry : Rewards)
	{
		if (Entry.Kind == Kind && Entry.RollMode == EMT2LootRollMode::Guaranteed)
		{
			const int64 Amount = FMath::Max<int64>(Entry.MinimumAmount, 0);
			Total = Total > MAX_int64 - Amount ? MAX_int64 : Total + Amount;
		}
	}
	return Total;
}
