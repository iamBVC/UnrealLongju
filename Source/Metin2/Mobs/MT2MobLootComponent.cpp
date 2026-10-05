/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Mobs/MT2MobLootComponent.h"

#include "Loot/MT2LootTable.h"
#include "Config/MT2GameplaySettings.h"
#include "Core/MT2ActorUtils.h"
#include "Mobs/MT2MobTypes.h"
#include "Mobs/MT2MobRuntimeSettings.h"
#include "Items/MT2WorldItem.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Characters/MT2PlayerCharacter.h"
#include "Party/MT2Party.h"
#include "Player/MT2PlayerController.h"
#include "Player/MT2PlayerState.h"

namespace
{
	int32 GetRewardApplyBonus(const AMT2PlayerState* State, EMT2ItemBonusType Type)
	{
		const AMT2PlayerCharacter* Character = State
			? Cast<AMT2PlayerCharacter>(State->GetPawn()) : nullptr;
		return Character ? FMath::Max(Character->GetItemApplyBonus(static_cast<int32>(Type)), 0) : 0;
	}

	int64 ApplyPercentBonus(int64 Amount, int32 Percent)
	{
		if (Amount <= 0)
		{
			return 0;
		}
		const double Multiplier = 1.0 + static_cast<double>(FMath::Max(Percent, 0)) / 100.0;
		return static_cast<int64>(FMath::Min(
			static_cast<double>(Amount) * Multiplier, static_cast<double>(MAX_int64)));
	}

	bool RollPercentBonus(const AMT2PlayerState* State, EMT2ItemBonusType Type)
	{
		const int32 Chance = FMath::Clamp(GetRewardApplyBonus(State, Type), 0, 100);
		return Chance > 0 && FMath::RandRange(1, 100) <= Chance;
	}

	int64 ApplyRewardMultiplier(int64 Amount, float Multiplier, int64 MaximumAmount)
	{
		if (Amount <= 0 || Multiplier <= 0.0f)
		{
			return 0;
		}
		if (!FMath::IsFinite(Multiplier))
		{
			return FMath::Min(Amount, MaximumAmount);
		}
		const double ScaledAmount = static_cast<double>(Amount) * static_cast<double>(Multiplier);
		return static_cast<int64>(FMath::Min(ScaledAmount, static_cast<double>(MaximumAmount)));
	}

	int64 AddRewardAmounts(int64 Left, int64 Right)
	{
		Left = FMath::Max<int64>(Left, 0);
		Right = FMath::Max<int64>(Right, 0);
		return Left > MAX_int64 - Right ? MAX_int64 : Left + Right;
	}

	int64 RandomRewardAmount(int64 Minimum, int64 Maximum)
	{
		Minimum = FMath::Max<int64>(Minimum, 0);
		Maximum = FMath::Max(Maximum, Minimum);
		if (Minimum == Maximum)
		{
			return Minimum;
		}
		const uint64 RandomValue =
			(static_cast<uint64>(static_cast<uint32>(FMath::Rand())) << 32) |
			static_cast<uint32>(FMath::Rand());
		const uint64 Range = static_cast<uint64>(Maximum - Minimum) + 1;
		return Minimum + static_cast<int64>(RandomValue % Range);
	}

	struct FRewardEntry
	{
		AActor* Attacker = nullptr;
		AMT2PlayerState* State = nullptr;
		float Damage = 0.0f;
	};

	struct FRewardGroup
	{
		AMT2Party* Party = nullptr;
		AMT2PlayerState* SoloState = nullptr;
		AActor* PrimaryAttacker = nullptr;
		float Damage = 0.0f;
		TArray<AMT2PlayerState*> Recipients;
	};

	TArray<FRewardEntry> BuildRewardEntries(
		const TArray<FMT2DamageShare>& DamageShares, const AActor* Victim, float& OutTotalDamage)
	{
		OutTotalDamage = 0.0f;
		const float RewardRangeSquared =
			FMath::Square(GetDefault<UMT2MobRuntimeSettings>()->RewardRange);
		TArray<FRewardEntry> Entries;
		for (const FMT2DamageShare& Share : DamageShares)
		{
			AActor* Attacker = Share.Attacker.Get();
			AMT2PlayerState* State = MT2ActorUtils::ResolvePlayerState(Attacker);
			// No player state = a mob helped kill it; the old game skips those (IsNPC()).
			if (!Attacker || !State || Share.TotalDamage <= 0.0f)
			{
				continue;
			}
			if (FVector::DistSquared(Victim->GetActorLocation(), Attacker->GetActorLocation())
				> RewardRangeSquared)
			{
				continue;
			}
			FRewardEntry& Entry = Entries.AddDefaulted_GetRef();
			Entry.Attacker = Attacker;
			Entry.State = State;
			Entry.Damage = Share.TotalDamage;
			OutTotalDamage += Share.TotalDamage;
		}
		// Descending damage, matching the old std::priority_queue walk.
		Entries.Sort([](const FRewardEntry& A, const FRewardEntry& B) { return A.Damage > B.Damage; });
		return Entries;
	}

	TArray<FRewardGroup> BuildRewardGroups(const TArray<FRewardEntry>& Entries)
	{
		TArray<FRewardGroup> Groups;
		for (const FRewardEntry& Entry : Entries)
		{
			AMT2Party* Party = Entry.State ? Entry.State->GetParty() : nullptr;
			FRewardGroup* Group = Groups.FindByPredicate(
				[Party, &Entry](const FRewardGroup& Candidate)
				{
					return Party ? Candidate.Party == Party
						: (!Candidate.Party && Candidate.SoloState == Entry.State);
				});
			if (!Group)
			{
				Group = &Groups.AddDefaulted_GetRef();
				Group->Party = Party;
				Group->SoloState = Party ? nullptr : Entry.State;
				Group->PrimaryAttacker = Entry.Attacker;
				if (Party)
				{
					for (AMT2PlayerState* Member : Party->GetMemberStates())
					{
						if (IsValid(Member)) Group->Recipients.Add(Member);
					}
				}
				else if (Entry.State)
				{
					Group->Recipients.Add(Entry.State);
				}
			}
			Group->Damage += Entry.Damage;
		}
		Groups.RemoveAll([](const FRewardGroup& Group) { return Group.Recipients.IsEmpty(); });
		Groups.Sort([](const FRewardGroup& A, const FRewardGroup& B)
		{
			return A.Damage > B.Damage;
		});
		return Groups;
	}

	void PayEqualExperience(FRewardGroup& Group, int64 Amount, const FVector& RewardOrigin)
	{
		if (Amount <= 0 || Group.Recipients.IsEmpty())
		{
			return;
		}

		TArray<AMT2PlayerState*> EligibleRecipients;
		if (!Group.Party)
		{
			EligibleRecipients = Group.Recipients;
		}
		else
		{
			const APawn* SharingOrigin = Cast<APawn>(Group.PrimaryAttacker);
			if (!SharingOrigin)
			{
				if (const AController* Controller = Cast<AController>(Group.PrimaryAttacker))
				{
					SharingOrigin = Controller->GetPawn();
				}
			}
			const float ShareRange = UMT2GameplaySettings::Get().PartyExperienceShareRange;
			const float ShareRangeSquared = FMath::Square(FMath::Max(ShareRange, 0.0f));
			for (AMT2PlayerState* Recipient : Group.Recipients)
			{
				const APawn* RecipientPawn = Recipient ? Recipient->GetPawn() : nullptr;
				if (RecipientPawn && SharingOrigin &&
					(ShareRange <= 0.0f || FVector::DistSquared2D(
						RecipientPawn->GetActorLocation(), SharingOrigin->GetActorLocation())
						<= ShareRangeSquared))
				{
					EligibleRecipients.Add(Recipient);
				}
			}
		}
		if (EligibleRecipients.IsEmpty())
		{
			return;
		}

		const int64 BaseShare = Amount / EligibleRecipients.Num();
		int64 Remainder = Amount % EligibleRecipients.Num();
		for (AMT2PlayerState* Recipient : EligibleRecipients)
		{
			const int64 BaseRecipientShare = BaseShare + (Remainder-- > 0 ? 1 : 0);
			int64 Share = ApplyPercentBonus(BaseRecipientShare,
				GetRewardApplyBonus(Recipient, EMT2ItemBonusType::MallExperienceBonus));
			// Original APPLY_EXP_DOUBLE_BONUS grants 30% extra experience when its
			// percentage roll succeeds; despite the legacy name, it is not a 2x reward.
			if (RollPercentBonus(Recipient, EMT2ItemBonusType::DoubleExperienceChance))
			{
				Share = ApplyPercentBonus(Share, 30);
			}
			if (Recipient && Share > 0)
			{
				const bool bExperienceAdded = Recipient->AddExperience(Share);
				if (bExperienceAdded)
				{
					if (AMT2PlayerController* Controller =
						Cast<AMT2PlayerController>(Recipient->GetOwner()))
					{
						Controller->ClientSpawnExperienceOrbs(RewardOrigin, Share);
					}
				}
			}
		}
	}

	void DistributeExperience(
		TArray<FRewardGroup>& Groups, float TotalDamage, int64 ExperiencePool,
		const FVector& RewardOrigin)
	{
		// Old DistributeExp: the top damager takes a flat 20% off the pool first, then the
		// remaining 80% is split strictly in proportion to damage dealt - the top damager
		// included. Percentages stay relative to the original total damage, so the shares always
		// add back up to the pool.
		const int64 TopBonus = ExperiencePool / 5;
		const int64 Remaining = ExperiencePool - TopBonus;

		for (int32 Index = 0; Index < Groups.Num(); ++Index)
		{
			FRewardGroup& Group = Groups[Index];
			const float Percent = FMath::Min(Group.Damage / TotalDamage, 1.0f);
			int64 Experience = static_cast<int64>(Remaining * Percent);
			if (Index == 0)
			{
				Experience += TopBonus;
			}
			PayEqualExperience(Group, Experience, RewardOrigin);
		}
	}

	AMT2PlayerState* ClaimDropRecipient(FRewardGroup& Group)
	{
		if (Group.Party)
		{
			return Group.Party->ClaimNextLootRecipient();
		}
		return Group.Recipients.IsEmpty() ? nullptr : Group.Recipients[0];
	}
}

UMT2MobLootComponent::UMT2MobLootComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UMT2MobLootComponent::Configure(const FMT2MobDefinition& Definition)
{
	ImportedGoldMin = FMath::Max<int64>(Definition.GoldMin, 0);
	ImportedGoldMax = FMath::Max(Definition.GoldMax, ImportedGoldMin);
	bRewardsGenerated = false;
}

void UMT2MobLootComponent::SetLootTable(TSubclassOf<UMT2LootTable> InLootTable)
{
	LootTable = InLootTable;
	bRewardsGenerated = false;
}

void UMT2MobLootComponent::AddBonusExperience(int64 Amount)
{
	if (Amount > 0)
	{
		BonusExperience = AddRewardAmounts(BonusExperience, Amount);
	}
	else if (Amount < 0)
	{
		BonusExperience = Amount <= -BonusExperience ? 0 : BonusExperience + Amount;
	}
}

int64 UMT2MobLootComponent::GetExperienceReward() const
{
	const UMT2LootTable* Table = LootTable.GetDefaultObject();
	return Table ? Table->GetGuaranteedAmount(EMT2LootCrateRewardKind::Experience) : 0;
}

int64 UMT2MobLootComponent::GetExperiencePool() const
{
	return AddRewardAmounts(GetExperienceReward(), BonusExperience);
}

int64 UMT2MobLootComponent::GetGoldMin() const
{
	const UMT2LootTable* Table = LootTable.GetDefaultObject();
	if (!Table) return ImportedGoldMin;
	for (const FMT2LootTableEntry& Entry : Table->Rewards)
	{
		if (Entry.Kind == EMT2LootCrateRewardKind::Yang &&
			Entry.RollMode == EMT2LootRollMode::Guaranteed)
		{
			return FMath::Max<int64>(Entry.MinimumAmount, 0);
		}
	}
	return ImportedGoldMin;
}

int64 UMT2MobLootComponent::GetGoldMax() const
{
	const UMT2LootTable* Table = LootTable.GetDefaultObject();
	if (!Table) return ImportedGoldMax;
	for (const FMT2LootTableEntry& Entry : Table->Rewards)
	{
		if (Entry.Kind == EMT2LootCrateRewardKind::Yang &&
			Entry.RollMode == EMT2LootRollMode::Guaranteed)
		{
			return FMath::Max<int64>(Entry.MaximumAmount, 0);
		}
	}
	return ImportedGoldMax;
}

void UMT2MobLootComponent::GenerateRewards(
	const TArray<FMT2DamageShare>& DamageShares, bool bIncludeDrops)
{
	if (bRewardsGenerated || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	bRewardsGenerated = true;
	float TotalDamage = 0.0f;
	const TArray<FRewardEntry> Entries = BuildRewardEntries(DamageShares, GetOwner(), TotalDamage);
	// Old DistributeExp: with nothing in the damage map nobody is rewarded and nothing drops.
	if (Entries.IsEmpty() || TotalDamage <= 0.0f)
	{
		return;
	}
	// Entries are damage-sorted, so the top damager is the old pkChrMostAttacked - the one Reward()
	// treats as "the killer" for gold, quests and alignment, regardless of who landed the last hit.
	TArray<FRewardGroup> RewardGroups = BuildRewardGroups(Entries);
	if (RewardGroups.IsEmpty())
	{
		return;
	}
	AActor* Killer = RewardGroups[0].PrimaryAttacker;
	AMT2PlayerState* KillerState = MT2ActorUtils::ResolvePlayerState(Killer);
	const UMT2GameplaySettings& GameplaySettings = UMT2GameplaySettings::Get();
	const int32 ItemDropBonus = GetRewardApplyBonus(
		KillerState, EMT2ItemBonusType::MallItemDropBonus);
	// Legacy APPLY_MALL_ITEMBONUS uses 50 for the standard 2x Thief's Gloves effect.
	const float ItemDropChanceMultiplier = GameplaySettings.MobItemDropChanceMultiplier *
		(1.0f + ItemDropBonus / 50.0f);

	FMT2MobRewardBundle Rewards;
	Rewards.Killer = Killer;
	Rewards.Experience = BonusExperience;
	bool bRolledYangReward = false;
	const UMT2LootTable* Table = LootTable.GetDefaultObject();
	if (Table)
	{
		for (const FMT2LootTableResult& Result :
			Table->Roll(false, ItemDropChanceMultiplier))
		{
			if (Result.Kind == EMT2LootCrateRewardKind::Experience)
			{
				Rewards.Experience = AddRewardAmounts(Rewards.Experience, Result.Amount);
			}
			else if (bIncludeDrops && Result.Kind == EMT2LootCrateRewardKind::Yang)
			{
				bRolledYangReward = true;
				Rewards.Gold = AddRewardAmounts(Rewards.Gold, Result.Amount);
			}
			else if (bIncludeDrops && Result.Kind == EMT2LootCrateRewardKind::Item &&
				!Result.Item.IsEmpty())
			{
				FMT2GeneratedLootItem& Item = Rewards.Items.AddDefaulted_GetRef();
				Item.ItemVnum = Result.Item.Vnum;
				Item.Count = Result.Item.Count;
				Item.ItemData = Result.Item;
			}
		}
	}
	if (bIncludeDrops && RollPercentBonus(KillerState, EMT2ItemBonusType::ItemDropBonus))
	{
		// APPLY_ITEM_DROP_BONUS is a chance to receive a second copy of each
		// successful item roll, not another pass over the whole loot table.
		const TArray<FMT2GeneratedLootItem> RolledItems = Rewards.Items;
		Rewards.Items.Append(RolledItems);
	}
	if (bIncludeDrops && !bRolledYangReward && ImportedGoldMax > 0)
	{
		Rewards.Gold = RandomRewardAmount(ImportedGoldMin, ImportedGoldMax);
	}
	Rewards.Gold = FMath::Clamp<int64>(ApplyPercentBonus(
		Rewards.Gold, GetRewardApplyBonus(KillerState, EMT2ItemBonusType::MallYangBonus)),
		0, MAX_int64);
	if (Rewards.Gold > 0 &&
		RollPercentBonus(KillerState, EMT2ItemBonusType::DoubleYangChance))
	{
		Rewards.Gold = ApplyRewardMultiplier(Rewards.Gold, 2.0f, MAX_int64);
	}
	Rewards.Gold = ApplyRewardMultiplier(
		Rewards.Gold, GameplaySettings.MobGoldAmountMultiplier, MAX_int64);
	Rewards.Experience = ApplyRewardMultiplier(
		Rewards.Experience, GameplaySettings.MobExperienceAmountMultiplier, MAX_int64);

	if (bIncludeDrops && (Rewards.Items.Num() > 0 || Rewards.Gold > 0))
	{
		// The highest-damage party owns the rolled loot. Item ownership rotates through that party
		// across kills, while solo rewards retain the killer as owner.
		FRewardGroup& WinningGroup = RewardGroups[0];

		const FVector DropOrigin = GetOwner()->GetActorLocation();
		int32 DropIndex = 0;
		auto NextDropLocation = [&DropOrigin, &DropIndex]()
		{
			const float Radius = DropIndex > 0
				? 55.0f * FMath::Sqrt(static_cast<float>(DropIndex)) : 0.0f;
			const float Angle = static_cast<float>(DropIndex) * 2.39996323f;
			const FVector Offset(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.0f);
			++DropIndex;
			return DropOrigin + Offset;
		};

		for (int32 ItemIndex = 0; ItemIndex < Rewards.Items.Num(); ++ItemIndex)
		{
			AMT2PlayerState* Owner = ClaimDropRecipient(WinningGroup);
			const FMT2GeneratedLootItem& Drop = Rewards.Items[ItemIndex];
			FMT2ItemSlot Item(Drop.ItemVnum, Drop.ItemData);
			Item.Count = Drop.Count;
			AMT2WorldItem::SpawnWorldItemInstance(
				GetWorld(), NextDropLocation(), Item, GetOwner(),
				AMT2WorldItem::ResolvePlayerIdentity(Owner),
				Owner ? Owner->GetCharacterName() : FString(), 120.0f);
		}

		// The old server creates VNUM 1 with its count equal to the Yang amount. A party receives
		// separate, evenly divided piles so every member gets an equal share.
		if (Rewards.Gold > 0)
		{
			const int32 RecipientCount = WinningGroup.Recipients.Num();
			const int64 BaseShare = Rewards.Gold / RecipientCount;
			int64 Remainder = Rewards.Gold % RecipientCount;
			for (int32 Index = 0; Index < RecipientCount; ++Index)
			{
				AMT2PlayerState* Owner = ClaimDropRecipient(WinningGroup);
				const int64 Share = BaseShare + (Remainder-- > 0 ? 1 : 0);
				if (Share <= 0) continue;
				AMT2WorldItem::SpawnWorldYang(
					GetWorld(), NextDropLocation(), Share, GetOwner(),
					AMT2WorldItem::ResolvePlayerIdentity(Owner),
					Owner ? Owner->GetCharacterName() : FString(), 120.0f);
			}
		}
	}

	if (Rewards.Experience > 0)
	{
		FVector RewardOrigin = GetOwner()->GetActorLocation();
		FVector BoundsOrigin = RewardOrigin;
		FVector BoundsExtent = FVector::ZeroVector;
		// Collision is disabled before rewards are generated. Querying only colliding components
		// therefore returned an empty bound at world origin and sent every visual orb to (0,0,0).
		GetOwner()->GetActorBounds(false, BoundsOrigin, BoundsExtent);
		if (!BoundsExtent.IsNearlyZero())
		{
			RewardOrigin = BoundsOrigin + FVector(0.0f, 0.0f, BoundsExtent.Z * 0.75f);
		}
		DistributeExperience(
			RewardGroups, TotalDamage, Rewards.Experience, RewardOrigin);
	}
	OnRewardsReady.Broadcast(Rewards);
}
