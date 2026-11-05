#pragma once

#include "Items/Templates/MT2ItemUseTemplate.h"
#include "MT2ItemUseAffectTemplate.generated.h"

/**
 * Legacy item-proto variant USE_AFFECT (ordinal 8).
 * Data-authored affect identity with one active record per identity/apply pair.
 * Different applies of the same identity coexist. Status-effect component owns execution.
 */
UCLASS(Blueprintable)
class METIN2_API UMT2ItemUseAffectTemplate : public UMT2ItemUseTemplate
{
	GENERATED_BODY()

public:
	UMT2ItemUseAffectTemplate();
	virtual void Serialize(FStructuredArchive::FRecord Record) override;
	virtual void PostLoad() override;
	virtual EMT2ItemUseExecution ExecuteUse(AMT2PlayerCharacter& Character,
		UMT2InventoryComponent& Inventory, int32 InventorySlot) const override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Use|Affect")
	FMT2AffectIdentifier EffectIdentity;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Use|Affect")
	EMT2ItemBonusType BonusType = EMT2ItemBonusType::None;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Use|Affect")
	int32 BonusAmount = 0;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Use|Affect", meta = (Units = "s", ClampMin = "1"))
	int32 DurationSeconds = 0;

private:
	bool bMigrateAffectFields = false;
};

