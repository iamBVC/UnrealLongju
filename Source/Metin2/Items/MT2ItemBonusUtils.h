/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Items/MT2ItemBonusSettings.h"

class UMT2ItemTemplate;
struct FMT2ItemSlot;

namespace MT2ItemBonusUtils
{
	METIN2_API EMT2ItemBonusTarget ResolveTarget(const UMT2ItemTemplate* Template);
	METIN2_API int32 RollWeightedIndex(const TArray<int32>& Weights);
	METIN2_API bool HasBonusType(
		const TArray<FMT2ItemBonus>& Bonuses, int32 Type, EMT2ItemBonusKind Kind);
	METIN2_API bool AddRandomBonus(
		FMT2ItemSlot& Item, const TArray<FMT2ItemBonusDefinition>& Definitions,
		const TArray<int32>& LevelWeights, EMT2ItemBonusTarget Target, EMT2ItemBonusKind Kind);
	METIN2_API void AddDamageAddon(FMT2ItemSlot& Item, const FMT2DamageAddonFormula& Formula);
}
