#pragma once

#include "Items/Templates/MT2ItemRecoveryTemplate.h"
#include "MT2ItemUsePotionContinueTemplate.generated.h"

/**
 * Fixed HP (preferred) or MP recovery per second for an authored duration. Reuse replaces the
 * same resource's effect and refreshes its duration, including when that resource is full.
 * The status-effect system owns ticking; templates never store active recovery state.
 * TODO(OWN-02S): dungeon/war potion accounting requires the corresponding system services.
 */
UCLASS(Blueprintable, HideCategories = ("Use|Buff"))
class METIN2_API UMT2ItemUsePotionContinueTemplate : public UMT2ItemRecoveryTemplate
{
	GENERATED_BODY()

public:
	UMT2ItemUsePotionContinueTemplate();
	virtual void Serialize(FStructuredArchive::FRecord Record) override;
	virtual void PostLoad() override;
	virtual EMT2ItemUseExecution ExecuteUse(AMT2PlayerCharacter& Character,
		UMT2InventoryComponent& Inventory, int32 InventorySlot) const override;

	/** Legacy VALUE2, in seconds. Full resources still spend this duration. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Use|Recovery", meta = (ClampMin = "0", Units = "s"))
	int32 RecoveryDurationSeconds = 0;

private:
	// Native load state, never inherited as an authored Blueprint default or persisted itself.
	bool bMigrateRecoveryDuration = false;
};

