#pragma once

#include "Items/Templates/MT2ItemUseTemplate.h"
#include "MT2ItemUsePotionNoDelayTemplate.generated.h"

/**
 * Instant HP/MP recovery: flat quantities receive Potion Effect Bonus; percent quantities do
 * not. Each step uses legacy integer truncation and caps at maximum. Inventory consumes once.
 * TODO(OWN-02): arena quota/eligibility, special bottle timing and dungeon/war potion accounting
 * require their system-owned services; this class currently implements core resource recovery.
 */
UCLASS(Blueprintable)
class METIN2_API UMT2ItemUsePotionNoDelayTemplate : public UMT2ItemUseTemplate
{
	GENERATED_BODY()

public:
	UMT2ItemUsePotionNoDelayTemplate();
	virtual EMT2ItemUseExecution ExecuteUse(AMT2PlayerCharacter& Character,
		UMT2InventoryComponent& Inventory, int32 InventorySlot) const override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Use|Recovery")
	int32 HealthRecoveryPercent = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Use|Recovery")
	int32 ManaRecoveryPercent = 0;
};

