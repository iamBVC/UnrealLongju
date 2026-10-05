/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Items/Use/MT2ItemActionTemplate.h"
#include "MT2MountItemTemplate.generated.h"

class UMT2MountDefinition;

// A mount seal/license. Importer data comes from the server ride quest tables because item_proto
// does not contain the mount race vnum or its complete usage policy.
UCLASS(Blueprintable)
class METIN2_API UMT2MountItemTemplate : public UMT2ItemActionTemplate
{
	GENERATED_BODY()

public:
	virtual EMT2ItemUseExecution ExecuteUse(
		AMT2PlayerCharacter& Character, UMT2InventoryComponent& Inventory,
		int32 InventorySlot) const override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mount")
	TSoftObjectPtr<UMT2MountDefinition> MountDefinition;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mount", meta = (ClampMin = "0", Units = "s"))
	int32 RideDurationSeconds = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mount", meta = (ClampMin = "0"))
	int32 MinimumPlayerLevel = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mount")
	bool bConsumeOnMount = false;
};
