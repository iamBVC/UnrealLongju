/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Items/MT2ItemBonusSettings.h"

const FMT2ItemBonusDefinition* UMT2ItemBonusSettings::FindDefinition(
	int32 ApplyType, EMT2ItemBonusKind Kind) const
{
	const TArray<FMT2ItemBonusDefinition>& Definitions =
		Kind == EMT2ItemBonusKind::Rare ? RareBonuses : NormalBonuses;
	return Definitions.FindByPredicate([ApplyType](const FMT2ItemBonusDefinition& Definition)
	{
		return Definition.ApplyType == ApplyType;
	});
}

FString UMT2ItemBonusSettings::FormatBonus(int32 ApplyType, int32 Value) const
{
	const FMT2ItemBonusDefinition* Definition = FindDefinition(ApplyType, EMT2ItemBonusKind::Normal);
	if (!Definition)
	{
		Definition = FindDefinition(ApplyType, EMT2ItemBonusKind::Rare);
	}
	if (!Definition)
	{
		return FString::Printf(TEXT("Bonus %d %+d"), ApplyType, Value);
	}
	if (Definition->ValueFormat == EMT2ItemBonusValueFormat::Boolean)
	{
		return Definition->DisplayName;
	}
	return FString::Printf(TEXT("%s %+d%s"), *Definition->DisplayName, Value,
		Definition->ValueFormat == EMT2ItemBonusValueFormat::Percent ? TEXT("%") : TEXT(""));
}
