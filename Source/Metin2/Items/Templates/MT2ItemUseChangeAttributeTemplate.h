#pragma once

#include "Items/Templates/MT2ItemBonusConsumableTemplate.h"
#include "MT2ItemUseChangeAttributeTemplate.generated.h"

/**
 * Legacy USE_CHANGE_ATTRIBUTE item: rerolls the target's existing normal attributes, preserving
 * their count and applying the legacy damage-addon pair when configured for that equipment.
 */
UCLASS(Blueprintable)
class METIN2_API UMT2ItemUseChangeAttributeTemplate : public UMT2ItemBonusConsumableTemplate
{
	GENERATED_BODY()

public:
	virtual EMT2ItemBonusApplyResult ApplyToEquipment(FMT2ItemSlot& StagedTarget,
		const UMT2ItemTemplate& TargetTemplate, const UMT2ItemBonusSettings& Settings) const override;
	virtual bool IsBonusRandomizer() const override { return true; }

protected:
	/** Concrete variants select value probabilities without duplicating the reroll transaction. */
	virtual const TArray<int32>& GetValueLevelWeights(const UMT2ItemBonusSettings& Settings) const;
};

