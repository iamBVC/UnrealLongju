/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once
#include "Items/Templates/MT2ItemUseSpecialTemplate.h"
#include "Items/Types/MT2AutoRecoveryTypes.h"
#include "Items/State/MT2AutoRecoveryItemState.h"
#include "MT2ItemAutoRecoveryTemplate.generated.h"

UCLASS(Blueprintable)
class METIN2_API UMT2ItemAutoRecoveryTemplate : public UMT2ItemUseSpecialTemplate
{
	GENERATED_BODY()

public:
	virtual EMT2ItemUseExecution ExecuteUse(AMT2PlayerCharacter& Character,
		UMT2InventoryComponent& Inventory, int32 InventorySlot) const override;
	// Produces a shared recovery-pool update, never an instant heal. Returns reservoir points spent.
	int32 PrepareRecoveryTransfer(AMT2PlayerCharacter& Character, int32 Available,
		FMT2StatusEffect& OutPool) const;
	virtual void InitializeDefaultFamilyState(FMT2ItemInstanceData& Data) const override;
	virtual void CaptureDefaultFamilyState(const FMT2ItemInstanceData& Data) override;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Initial State")
	FMT2AutoRecoveryItemState DefaultRecoveryState;

	virtual void InitializeGeneratedInstance(FMT2ItemInstanceData& InOutData) const override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Automatic Recovery")
	EMT2AutoRecoveryResource Resource = EMT2AutoRecoveryResource::Health;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Automatic Recovery", meta = (ClampMin = "1"))
	int32 RecoveryCapacity = 0;
};
