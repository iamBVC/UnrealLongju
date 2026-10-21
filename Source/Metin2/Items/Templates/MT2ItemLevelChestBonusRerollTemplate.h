#pragma once

#include "Items/Templates/MT2ItemUseChangeAttributeTemplate.h"
#include "MT2ItemLevelChestBonusRerollTemplate.generated.h"

/** Enchant Item (b), from level chests: ordinary reroll with its own value-level probabilities.
 * The importer maps legacy item 76014 to this class. Trade restrictions remain authored proto flags,
 * not a separate runtime binding mechanic. No beginner-only equipment/level restriction applies.
 * TODO(OWN-01C): shared ordinary-reroll cooldown and addon-slot parity remain separate audits.
 */
UCLASS(Blueprintable)
class METIN2_API UMT2ItemLevelChestBonusRerollTemplate : public UMT2ItemUseChangeAttributeTemplate
{
	GENERATED_BODY()
protected:
	virtual const TArray<int32>& GetValueLevelWeights(const UMT2ItemBonusSettings& Settings) const override;
};
