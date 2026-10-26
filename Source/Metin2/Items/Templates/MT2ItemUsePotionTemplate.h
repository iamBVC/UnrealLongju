#pragma once

#include "Items/Templates/MT2ItemRecoveryTemplate.h"
#include "MT2ItemUsePotionTemplate.generated.h"

/**
 * Queues the full integer-scaled HP/MP quantity after checking current plus pending recovery.
 * The shared status-effect processor drains the pool; the template never owns player state.
 * TODO(OWN-02S): arena quotas and dungeon/war potion accounting require system services.
 */
UCLASS(Blueprintable)
class METIN2_API UMT2ItemUsePotionTemplate : public UMT2ItemRecoveryTemplate
{
	GENERATED_BODY()

public:
	UMT2ItemUsePotionTemplate();
	virtual EMT2ItemUseExecution ExecuteUse(AMT2PlayerCharacter& Character,
		UMT2InventoryComponent& Inventory, int32 InventorySlot) const override;
};

