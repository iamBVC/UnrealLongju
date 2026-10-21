/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Items/Templates/MT2ItemBonusConsumableTemplate.h"
#include "MT2ItemUseAddRareBonusTemplate.generated.h"

/** Applies one uniformly selected eligible rare type; value levels use the rare settings. */
UCLASS(Blueprintable)
class METIN2_API UMT2ItemUseAddRareBonusTemplate : public UMT2ItemBonusConsumableTemplate
{
	GENERATED_BODY()

public:
	virtual EMT2ItemBonusApplyResult ApplyToEquipment(FMT2ItemSlot& StagedTarget,
		const UMT2ItemTemplate& TargetTemplate, const UMT2ItemBonusSettings& Settings) const override;
};
