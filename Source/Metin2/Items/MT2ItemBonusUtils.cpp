/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Items/MT2ItemBonusUtils.h"
#include "Items/Templates/MT2ItemArmorTemplate.h"
#include "Items/Templates/MT2ItemWeaponTemplate.h"


#include "Items/MT2ItemTemplate.h"

EMT2ItemBonusTarget MT2ItemBonusUtils::ResolveTarget(const UMT2ItemTemplate* Template)
{
	if (const UMT2ItemWeaponTemplate* Weapon = Cast<UMT2ItemWeaponTemplate>(Template))
	{
		return Weapon->WeaponSubType == EMT2WeaponSubType::Arrow
			? EMT2ItemBonusTarget::None : EMT2ItemBonusTarget::Weapon;
	}
	const UMT2ItemArmorTemplate* Armor = Cast<UMT2ItemArmorTemplate>(Template);
	if (!Armor)
	{
		return EMT2ItemBonusTarget::None;
	}
	switch (Armor->ArmorSubType)
	{
	case EMT2ArmorSubType::Body: return EMT2ItemBonusTarget::Body;
	case EMT2ArmorSubType::Head: return EMT2ItemBonusTarget::Head;
	case EMT2ArmorSubType::Shield: return EMT2ItemBonusTarget::Shield;
	case EMT2ArmorSubType::Wrist: return EMT2ItemBonusTarget::Wrist;
	case EMT2ArmorSubType::Foots: return EMT2ItemBonusTarget::Foots;
	case EMT2ArmorSubType::Neck: return EMT2ItemBonusTarget::Neck;
	case EMT2ArmorSubType::Ear: return EMT2ItemBonusTarget::Ear;
	default: return EMT2ItemBonusTarget::None;
	}
}

bool MT2ItemBonusUtils::IsBeginnerBonusTarget(const UMT2ItemTemplate& Template, int32 MaximumRequiredLevel)
{
	const EMT2ItemBonusTarget Target = ResolveTarget(&Template);
	if (Target != EMT2ItemBonusTarget::Weapon && Target != EMT2ItemBonusTarget::Body)
	{
		return false;
	}
	// Check every level requirement: another limit must not hide a later, higher one.
	return !Template.Limits.ContainsByPredicate([MaximumRequiredLevel](const FMT2ItemLimit& Limit)
	{
		return Limit.Type == EMT2ItemLimitType::Level && Limit.Value > FMath::Max(MaximumRequiredLevel, 0);
	});
}

int32 MT2ItemBonusUtils::RollWeightedIndex(const TArray<int32>& Weights)
{
	int32 Total = 0;
	for (const int32 Weight : Weights)
	{
		Total += FMath::Max(Weight, 0);
	}
	if (Total <= 0)
	{
		return INDEX_NONE;
	}
	int32 Roll = FMath::RandRange(1, Total);
	for (int32 Index = 0; Index < Weights.Num(); ++Index)
	{
		Roll -= FMath::Max(Weights[Index], 0);
		if (Roll <= 0)
		{
			return Index;
		}
	}
	return Weights.Num() - 1;
}

int32 MT2ItemBonusUtils::CountBonuses(const FMT2ItemSlot& Item, EMT2ItemBonusKind Kind)
{
	int32 Count = 0;
	for (const FMT2ItemBonus& Bonus : Item.GetBonuses())
	{
		// Legacy slot occupancy depends on the type, even when its value is zero.
		Count += Bonus.Type != EMT2ItemBonusType::None && Bonus.Kind == Kind ? 1 : 0;
	}
	return Count;
}

bool MT2ItemBonusUtils::HasBonusType(
	const TArray<FMT2ItemBonus>& Bonuses, int32 Type, EMT2ItemBonusKind Kind)
{
	return Bonuses.ContainsByPredicate([Type, Kind](const FMT2ItemBonus& Bonus)
	{
		return Bonus.Type != EMT2ItemBonusType::None && Bonus.GetTypeId() == Type && Bonus.Kind == Kind;
	});
}

bool MT2ItemBonusUtils::AddRandomBonus(
	FMT2ItemSlot& Item, const TArray<FMT2ItemBonusDefinition>& Definitions,
	const TArray<int32>& LevelWeights, EMT2ItemBonusTarget Target, EMT2ItemBonusKind Kind,
	const UMT2ItemTemplate* TargetTemplate, ETypeSelection TypeSelection)
{
	TArray<const FMT2ItemBonusDefinition*> Available;
	TArray<int32> DefinitionWeights;
	for (const FMT2ItemBonusDefinition& Definition : Definitions)
	{
		const int32* MaximumLevel = Definition.MaxLevelByItemType.Find(Target);
		// Legacy HasAttr also checks the item's immutable prototype applies. Rare attributes
		// deliberately use a separate duplicate set and may match a prototype/normal apply.
		const bool bIntrinsicNormalBonus = Kind == EMT2ItemBonusKind::Normal && TargetTemplate &&
			TargetTemplate->Applies.ContainsByPredicate([&Definition](const FMT2ItemApply& Apply)
			{
				return Apply.Type == Definition.ApplyType;
			});
		if (Definition.ApplyType == EMT2ItemBonusType::None ||
			(TypeSelection == ETypeSelection::Weighted && Definition.SelectionWeight <= 0) ||
			!MaximumLevel || *MaximumLevel <= 0 ||
			(Definition.EligibleItemFlags & static_cast<int32>(Target)) == 0 ||
			bIntrinsicNormalBonus ||
			HasBonusType(Item.GetBonuses(), static_cast<int32>(Definition.ApplyType), Kind))
		{
			continue;
		}
		Available.Add(&Definition);
		// Legacy rare attributes sample eligible types uniformly; dwProb is a normal-only policy.
		DefinitionWeights.Add(TypeSelection == ETypeSelection::Uniform ? 1 : Definition.SelectionWeight);
	}
	const int32 DefinitionIndex = RollWeightedIndex(DefinitionWeights);
	if (!Available.IsValidIndex(DefinitionIndex))
	{
		return false;
	}

	const FMT2ItemBonusDefinition& Definition = *Available[DefinitionIndex];
	const int32 RolledLevel = RollWeightedIndex(LevelWeights);
	if (RolledLevel == INDEX_NONE)
	{
		return false;
	}
	const int32 MaxLevel = FMath::Clamp(
		Definition.MaxLevelByItemType.FindRef(Target), 1, 5);
	const int32 LevelIndex = FMath::Clamp(RolledLevel, 0, MaxLevel - 1);
	if (!Definition.Values.IsValidIndex(LevelIndex) || Definition.Values[LevelIndex] == 0)
	{
		return false;
	}
	FMT2ItemBonus& Bonus = Item.EditBonuses().AddDefaulted_GetRef();
	Bonus.Type = Definition.ApplyType;
	Bonus.Value = Definition.Values[LevelIndex];
	Bonus.Kind = Kind;
	return true;
}

void MT2ItemBonusUtils::AddDamageAddon(FMT2ItemSlot& Item, const FMT2DamageAddonFormula& Formula)
{
	const float U1 = FMath::Max(FMath::FRand(), UE_SMALL_NUMBER);
	const float U2 = FMath::FRand();
	const float Gaussian = FMath::Sqrt(-2.0f * FMath::Loge(U1)) *
		FMath::Cos(2.0f * UE_PI * U2);
	// Legacy casts after adding 0.5: negative rolls truncate toward zero, not round.
	const int32 Skill = FMath::Clamp(
		FMath::TruncToInt(Formula.SkillDamageMean + Gaussian * Formula.SkillDamageDeviation + 0.5f),
		Formula.SkillDamageMin, Formula.SkillDamageMax);
	const int32 Noise = FMath::RandRange(
		Formula.AverageDamageNoiseRange.X, Formula.AverageDamageNoiseRange.Y) +
		FMath::RandRange(Formula.AverageDamageNoiseRange.X, Formula.AverageDamageNoiseRange.Y);
	const FIntPoint ExtraRange = FMath::Abs(Skill) <= Formula.SkillDamageThreshold
		? Formula.AverageDamageLowSkillExtraRange : Formula.AverageDamageHighSkillExtraRange;
	const int32 Average = FMath::RoundToInt(Formula.AverageDamageSkillMultiplier * Skill) +
		(FMath::Abs(Skill) <= Formula.SkillDamageThreshold ? FMath::Abs(Noise) : 0) +
		FMath::RandRange(ExtraRange.X, ExtraRange.Y);
	// Legacy AddAttribute omits zero values; zero rolls must not reserve generated slots.
	if (Average != 0)
	{
		FMT2ItemBonus& AverageBonus = Item.EditBonuses().AddDefaulted_GetRef();
		AverageBonus.Type = Formula.AverageDamageApplyType;
		AverageBonus.Value = Average;
	}
	if (Skill != 0)
	{
		FMT2ItemBonus& SkillBonus = Item.EditBonuses().AddDefaulted_GetRef();
		SkillBonus.Type = Formula.SkillDamageApplyType;
		SkillBonus.Value = Skill;
	}
}
