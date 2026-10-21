#include "MT2ItemLevelChestBonusRerollTemplate.h"

#include "Items/MT2ItemBonusSettings.h"

const TArray<int32>& UMT2ItemLevelChestBonusRerollTemplate::GetValueLevelWeights(
	const UMT2ItemBonusSettings& Settings) const
{
	return Settings.LevelChestRerollValueLevelWeights;
}
