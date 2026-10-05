/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Items/Use/MT2MountItemTemplate.h"
#include "MT2HorseBookItemTemplate.generated.h"

// The original horse licences (50051-50053) call the player's owned horse. The remake keeps the
// called horse as server-authoritative mount data, then mounts it directly because it does not need
// a second gameplay actor standing beside the player.
UCLASS(Blueprintable)
class METIN2_API UMT2HorseBookItemTemplate : public UMT2MountItemTemplate
{
	GENERATED_BODY()

public:
	UMT2HorseBookItemTemplate();
	virtual bool ExecuteBeforeQuestItemUse() const override { return true; }

	virtual EMT2ItemUseExecution ExecuteUse(
		AMT2PlayerCharacter& Character, UMT2InventoryComponent& Inventory,
		int32 InventorySlot) const override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Horse|Summoning", meta = (ClampMin = "0"))
	int32 ManaCost = 300;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Horse|Summoning", meta = (ClampMin = "1"))
	int32 SummonSkillVnum = 131;

	// Array index is the summon skill level. This mirrors horse_summon.quest and remains editable.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Horse|Summoning")
	TArray<int32> SuccessPercentBySkillLevel;
};
