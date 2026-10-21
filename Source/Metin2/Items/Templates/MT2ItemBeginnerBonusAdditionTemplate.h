#pragma once

#include "Items/Templates/MT2ItemUseAddAttributeTemplate.h"
#include "MT2ItemBeginnerBonusAdditionTemplate.generated.h"

/** Beginner blessing: normal attribute addition restricted to low-level weapons/body armor.
 * Legacy items 71152/76024 share this policy; identity decoding belongs to the importer.
 * Uses ordinary addition chances and consumption, with no mutable state on the template.
 */
UCLASS(Blueprintable)
class METIN2_API UMT2ItemBeginnerBonusAdditionTemplate : public UMT2ItemUseAddAttributeTemplate
{
	GENERATED_BODY()
public:
	virtual EMT2ItemBonusApplyResult ApplyToEquipment(FMT2ItemSlot& StagedTarget,
		const UMT2ItemTemplate& TargetTemplate, const UMT2ItemBonusSettings& Settings) const override;
};
