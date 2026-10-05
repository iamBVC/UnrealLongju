/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Items/Use/MT2ItemActionTemplate.h"
#include "MT2MobLureItemTemplate.generated.h"

// Bravery Cape variants attract every living monster in range to the player. Character types
// NPC/Stone/etc. are explicitly excluded even when their imported Blueprint derives from AMT2Mob.
UCLASS(Blueprintable)
class METIN2_API UMT2MobLureItemTemplate : public UMT2ItemActionTemplate
{
	GENERATED_BODY()

public:
	virtual EMT2ItemUseExecution ExecuteUse(
		AMT2PlayerCharacter& Character, UMT2InventoryComponent& Inventory,
		int32 InventorySlot) const override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mob Lure",
		meta = (ClampMin = "0.0", Units = "cm"))
	float LureRadius = 5000.0f;

	// Original FuncAggregateMonster rolls independently for every idle monster in range.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mob Lure",
		meta = (ClampMin = "0", ClampMax = "100", Units = "%"))
	int32 AggroChancePercent = 50;

};
