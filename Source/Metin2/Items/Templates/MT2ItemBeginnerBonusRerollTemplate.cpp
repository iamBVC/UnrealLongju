#include "MT2ItemBeginnerBonusRerollTemplate.h"

#include "Items/MT2ItemBonusSettings.h"
#include "Items/MT2ItemBonusUtils.h"

EMT2ItemBonusApplyResult UMT2ItemBeginnerBonusRerollTemplate::ApplyToEquipment(
	FMT2ItemSlot& StagedTarget, const UMT2ItemTemplate& TargetTemplate,
	const UMT2ItemBonusSettings& Settings) const
{
	if (!MT2ItemBonusUtils::IsBeginnerBonusTarget(TargetTemplate, Settings.BeginnerBonusMaximumRequiredLevel))
	{
		return EMT2ItemBonusApplyResult::Invalid;
	}
	return Super::ApplyToEquipment(StagedTarget, TargetTemplate, Settings);
}
