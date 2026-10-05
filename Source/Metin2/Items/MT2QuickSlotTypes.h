/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "MT2QuickSlotTypes.generated.h"

// Old client TQuickslot types (QUICKSLOT_TYPE_ITEM / _SKILL).
UENUM(BlueprintType)
enum class EMT2QuickSlotType : uint8
{
	None,
	Item,
	Skill
};

// One taskbar cell: an item vnum or a skill vnum. The old game stored inventory positions for
// items; we key by vnum so the binding survives inventory reshuffles.
USTRUCT(BlueprintType)
struct METIN2_API FMT2QuickSlotAssignment
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Quick Slot")
	EMT2QuickSlotType Type = EMT2QuickSlotType::None;

	UPROPERTY(BlueprintReadOnly, Category = "Quick Slot")
	int32 Vnum = 0;

	bool IsEmpty() const { return Type == EMT2QuickSlotType::None || Vnum <= 0; }
};

namespace MT2QuickSlots
{
	// 4 pages x 8 keys (1-4, F1-F4), like the old game's paged bottom bar.
	inline constexpr int32 SlotsPerPage = 8;
	inline constexpr int32 PageCount = 4;
	inline constexpr int32 TotalSlots = SlotsPerPage * PageCount;
}
