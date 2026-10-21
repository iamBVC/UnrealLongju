/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Items/Templates/MT2ItemUseAddRareBonusTemplate.h"

#include "Items/MT2ItemBonusSettings.h"
#include "Items/MT2ItemBonusUtils.h"
#include "Items/MT2ItemTemplate.h"

EMT2ItemBonusApplyResult UMT2ItemUseAddRareBonusTemplate::ApplyToEquipment(
	FMT2ItemSlot& StagedTarget, const UMT2ItemTemplate& TargetTemplate,
	const UMT2ItemBonusSettings& Settings) const
{
	const EMT2ItemBonusTarget TargetType = MT2ItemBonusUtils::ResolveTarget(&TargetTemplate);
	if (TargetType == EMT2ItemBonusTarget::None ||
		MT2ItemBonusUtils::CountBonuses(StagedTarget, EMT2ItemBonusKind::Rare) >= Settings.RareBonusMaximum)
	{
		return EMT2ItemBonusApplyResult::Invalid;
	}

	return MT2ItemBonusUtils::AddRandomBonus(StagedTarget, Settings.RareBonuses,
		Settings.RareValueLevelWeights, TargetType, EMT2ItemBonusKind::Rare, nullptr,
		MT2ItemBonusUtils::ETypeSelection::Uniform)
		? EMT2ItemBonusApplyResult::Success
		: EMT2ItemBonusApplyResult::Invalid;
}
