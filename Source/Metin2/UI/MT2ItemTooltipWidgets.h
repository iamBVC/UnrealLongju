/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "UI/MT2ItemTooltipWidget.h"
#include "MT2ItemTooltipWidgets.generated.h"

// Weapon: physical + magic attack power ranges, then stat bonuses.
UCLASS(Blueprintable)
class METIN2_API UMT2WeaponTooltipWidget : public UMT2ItemTooltipWidget
{
	GENERATED_BODY()

protected:
	virtual void BuildTypeSpecific(const UMT2ItemTemplate* Template, const AMT2PlayerState* Viewer) override;
};

// Armor: defense + magic defense, then stat bonuses.
UCLASS(Blueprintable)
class METIN2_API UMT2ArmorTooltipWidget : public UMT2ItemTooltipWidget
{
	GENERATED_BODY()

protected:
	virtual void BuildTypeSpecific(const UMT2ItemTemplate* Template, const AMT2PlayerState* Viewer) override;
};

// Rings, belts, necklaces, earrings, bracelets: stat bonuses only.
UCLASS(Blueprintable)
class METIN2_API UMT2AccessoryTooltipWidget : public UMT2ItemTooltipWidget
{
	GENERATED_BODY()

protected:
	virtual void BuildTypeSpecific(const UMT2ItemTemplate* Template, const AMT2PlayerState* Viewer) override;
};

// Potions and other consumables: applied effects (potions restore, ability-ups grant bonuses).
UCLASS(Blueprintable)
class METIN2_API UMT2UseItemTooltipWidget : public UMT2ItemTooltipWidget
{
	GENERATED_BODY()

protected:
	virtual void BuildTypeSpecific(const UMT2ItemTemplate* Template, const AMT2PlayerState* Viewer) override;
};
