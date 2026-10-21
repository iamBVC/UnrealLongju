/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Items/Templates/MT2ItemUseChangeRareBonusesTemplate.h"

#include "Items/MT2ItemBonusSettings.h"
#include "Items/MT2ItemBonusUtils.h"
#include "Items/MT2ItemTemplate.h"

EMT2ItemBonusApplyResult UMT2ItemUseChangeRareBonusesTemplate::ApplyToEquipment(
	FMT2ItemSlot& StagedTarget, const UMT2ItemTemplate& TargetTemplate,
	const UMT2ItemBonusSettings& Settings) const
{
	const EMT2ItemBonusTarget TargetType = MT2ItemBonusUtils::ResolveTarget(&TargetTemplate);
	const int32 RareCount = MT2ItemBonusUtils::CountBonuses(StagedTarget, EMT2ItemBonusKind::Rare);
	if (TargetType == EMT2ItemBonusTarget::None || RareCount <= 0)
	{
		return EMT2ItemBonusApplyResult::Invalid;
	}

	const TArray<FMT2ItemBonus> OriginalBonuses = StagedTarget.GetBonuses();
	StagedTarget.EditBonuses().RemoveAll([](const FMT2ItemBonus& Bonus)
	{
		return Bonus.Kind == EMT2ItemBonusKind::Rare;
	});
	for (int32 Index = 0; Index < RareCount; ++Index)
	{
		if (!MT2ItemBonusUtils::AddRandomBonus(StagedTarget, Settings.RareBonuses,
			Settings.RareValueLevelWeights, TargetType, EMT2ItemBonusKind::Rare, nullptr,
			MT2ItemBonusUtils::ETypeSelection::Uniform))
		{
			break;
		}
	}
	if (MT2ItemBonusUtils::CountBonuses(StagedTarget, EMT2ItemBonusKind::Rare) != RareCount)
	{
		StagedTarget.EditBonuses() = OriginalBonuses;
		return EMT2ItemBonusApplyResult::Invalid;
	}

	return EMT2ItemBonusApplyResult::Success;
}
