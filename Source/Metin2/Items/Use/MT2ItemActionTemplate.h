/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Items/MT2ItemTemplate.h"
#include "MT2ItemActionTemplate.generated.h"

class AMT2PlayerCharacter;
class UMT2InventoryComponent;

enum class EMT2ItemUseExecution : uint8
{
	Rejected,
	Accepted,
	Consume
};

// Server-only behavior hook for special right-click items. Accepted starts an asynchronous action
// (usually a dialog); Consume completes immediately and lets the inventory remove one item.
UCLASS(Abstract, Blueprintable)
class METIN2_API UMT2ItemActionTemplate : public UMT2ItemUseTemplate
{
	GENERATED_BODY()

public:
	// Native behavior may precede legacy quest dispatch when that quest only contains an incomplete
	// imported compatibility script. Most action items keep quest-first behavior.
	virtual bool ExecuteBeforeQuestItemUse() const { return false; }

	virtual EMT2ItemUseExecution ExecuteUse(
		AMT2PlayerCharacter& Character, UMT2InventoryComponent& Inventory,
		int32 InventorySlot) const PURE_VIRTUAL(
			UMT2ItemActionTemplate::ExecuteUse, return EMT2ItemUseExecution::Rejected;);
};
