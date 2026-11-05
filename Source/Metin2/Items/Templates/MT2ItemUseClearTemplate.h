#pragma once

#include "Items/Templates/MT2ItemUseTemplate.h"
#include "MT2ItemUseClearTemplate.generated.h"

/**
 * Legacy item-proto variant USE_CLEAR (ordinal 12).
 * Removes harmful affects and consumes once even if no harmful affect is present.
 * The status-effect component owns removal rules and effect teardown.
 */
UCLASS(Blueprintable)
class METIN2_API UMT2ItemUseClearTemplate : public UMT2ItemUseTemplate
{
	GENERATED_BODY()

public:
	UMT2ItemUseClearTemplate();
	virtual EMT2ItemUseExecution ExecuteUse(AMT2PlayerCharacter& Character,
		UMT2InventoryComponent& Inventory, int32 InventorySlot) const override;
};

