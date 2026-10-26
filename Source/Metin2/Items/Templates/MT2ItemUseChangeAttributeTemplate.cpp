#include "MT2ItemUseChangeAttributeTemplate.h"

#include "Items/MT2ItemBonusSettings.h"
#include "Items/MT2ItemBonusUtils.h"
#include "Items/Templates/MT2ItemEquipmentTemplate.h"
#include "Items/MT2ItemTemplate.h"

EMT2ItemBonusApplyResult UMT2ItemUseChangeAttributeTemplate::ApplyToEquipment(
	FMT2ItemSlot& StagedTarget, const UMT2ItemTemplate& TargetTemplate,
	const UMT2ItemBonusSettings& Settings) const
{
	const EMT2ItemBonusTarget TargetType = MT2ItemBonusUtils::ResolveTarget(&TargetTemplate);
	const int32 NormalCount = MT2ItemBonusUtils::CountBonuses(StagedTarget, EMT2ItemBonusKind::Normal);
	if (TargetType == EMT2ItemBonusTarget::None || NormalCount <= 0)
	{
		return EMT2ItemBonusApplyResult::Invalid;
	}

	const TArray<FMT2ItemBonus> OriginalBonuses = StagedTarget.GetBonuses();
	StagedTarget.EditBonuses().RemoveAll([](const FMT2ItemBonus& Bonus)
	{
		return Bonus.Kind == EMT2ItemBonusKind::Normal;
	});

	const UMT2ItemEquipmentTemplate* EquipmentTarget = Cast<UMT2ItemEquipmentTemplate>(&TargetTemplate);
	if (EquipmentTarget && EquipmentTarget->BonusAddonType == Settings.DamageAddonType)
	{
		MT2ItemBonusUtils::AddDamageAddon(StagedTarget, Settings.DamageAddonFormula);
	}
	// Legacy regenerates the addon first; its nonzero pair can expand a one-slot item.
	const int32 RequiredCount = FMath::Max(NormalCount,
		MT2ItemBonusUtils::CountBonuses(StagedTarget, EMT2ItemBonusKind::Normal));
	while (MT2ItemBonusUtils::CountBonuses(StagedTarget, EMT2ItemBonusKind::Normal) < RequiredCount)
	{
		if (!MT2ItemBonusUtils::AddRandomBonus(StagedTarget, Settings.NormalBonuses,
			GetValueLevelWeights(Settings), TargetType, EMT2ItemBonusKind::Normal, &TargetTemplate))
		{
			break;
		}
	}
	if (MT2ItemBonusUtils::CountBonuses(StagedTarget, EMT2ItemBonusKind::Normal) != RequiredCount)
	{
		StagedTarget.EditBonuses() = OriginalBonuses;
		return EMT2ItemBonusApplyResult::Invalid;
	}

	return EMT2ItemBonusApplyResult::Success;
}

const TArray<int32>& UMT2ItemUseChangeAttributeTemplate::GetValueLevelWeights(
	const UMT2ItemBonusSettings& Settings) const
{
	return Settings.ChangeNormalValueLevelWeights;
}

