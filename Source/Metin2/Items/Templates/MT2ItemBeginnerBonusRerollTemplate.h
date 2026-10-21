#pragma once

#include "Items/Templates/MT2ItemUseChangeAttributeTemplate.h"
#include "MT2ItemBeginnerBonusRerollTemplate.generated.h"

/** Beginner enchantment: normal attribute reroll restricted to low-level weapons/body armor.
 * Legacy items 71151/76023 share this policy; the importer assigns their native parent.
 * Reuses normal reroll count/rare-attribute preservation and authoritative consumption.
 * TODO(OWN-01C): shared reroll cooldown policy remains part of the ordinary reroll audit.
 */
UCLASS(Blueprintable)
class METIN2_API UMT2ItemBeginnerBonusRerollTemplate : public UMT2ItemUseChangeAttributeTemplate
{
	GENERATED_BODY()
public:
	virtual EMT2ItemBonusApplyResult ApplyToEquipment(FMT2ItemSlot& StagedTarget,
		const UMT2ItemTemplate& TargetTemplate, const UMT2ItemBonusSettings& Settings) const override;
};
