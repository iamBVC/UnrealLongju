#pragma once

#include "Items/Templates/MT2ItemUseTemplate.h"
#include "MT2ItemUseAbilityUpTemplate.generated.h"

/**
 * Legacy item-proto variant USE_ABILITY_UP (ordinal 7).
 * Nine standard timed stat bonuses. Reuse refreshes the matching affect; death removes it.
 * TODO(OWN-02S): dungeon/war potion accounting belongs to the corresponding systems.
 */
UCLASS(Blueprintable)
class METIN2_API UMT2ItemUseAbilityUpTemplate : public UMT2ItemUseTemplate
{
	GENERATED_BODY()

public:
	UMT2ItemUseAbilityUpTemplate();
	virtual void Serialize(FStructuredArchive::FRecord Record) override;
	virtual void PostLoad() override;
	virtual void NormalizeRestoredEffect(FMT2StatusEffect& Effect) const override;
	virtual EMT2ItemUseExecution ExecuteUse(AMT2PlayerCharacter& Character,
		UMT2InventoryComponent& Inventory, int32 InventorySlot) const override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Use|Ability")
	EMT2ItemBonusType BonusType = EMT2ItemBonusType::None;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Use|Ability")
	int32 BonusAmount = 0;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Use|Ability", meta = (Units = "s", ClampMin = "1"))
	int32 DurationSeconds = 0;

protected:
	virtual bool BuildEffect(struct FMT2StatusEffect& Effect) const;
	virtual bool RefreshesExistingEffect() const { return true; }

private:
	bool bMigrateAbilityFields = false;
};

