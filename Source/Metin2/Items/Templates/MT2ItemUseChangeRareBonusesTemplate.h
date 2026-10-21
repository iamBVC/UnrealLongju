/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Items/Templates/MT2ItemBonusConsumableTemplate.h"
#include "MT2ItemUseChangeRareBonusesTemplate.generated.h"

/** Uniformly rerolls eligible rare types, preserving their count and all normal attributes. */
UCLASS(Blueprintable)
class METIN2_API UMT2ItemUseChangeRareBonusesTemplate : public UMT2ItemBonusConsumableTemplate
{
	GENERATED_BODY()

public:
	virtual EMT2ItemBonusApplyResult ApplyToEquipment(FMT2ItemSlot& StagedTarget,
		const UMT2ItemTemplate& TargetTemplate, const UMT2ItemBonusSettings& Settings) const override;
	virtual bool IsBonusRandomizer() const override { return true; }
};
