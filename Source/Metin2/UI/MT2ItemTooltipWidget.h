/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "Items/MT2ItemTypes.h"
#include "MT2ItemTooltipWidget.generated.h"

class UBorder;
class UMT2ItemTemplate;
class UVerticalBox;
class AMT2PlayerState;

// Hover tooltip for an inventory/shop item, recreating uitooltip.py's per-type layout. The base
// builds the common sections (name, description, level requirement, stat bonuses); subclasses per
// item family (weapon/armor/accessory/use) override BuildTypeSpecific to add their lines. The
// template chooses which subclass to instantiate via GetDefaultTooltipClass(), so hovering
// different item types produces differently-styled tooltips (the old game's model).
UCLASS(Blueprintable)
class METIN2_API UMT2ItemTooltipWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Fills the tooltip for Template (Count for stacks). Viewer colors level requirements by whether
	// the player meets them. ExtraPriceLine, when >= 0, appends a shop price line colored by
	// affordability (green affordable / red not).
	void BuildTooltip(const UMT2ItemTemplate* Template, int32 Count, const AMT2PlayerState* Viewer,
		int64 ShopPrice = -1, const TArray<FMT2ItemBonus>* Bonuses = nullptr,
		const TArray<FMT2MetinSocket>* MetinSockets = nullptr, int32 SkillVnum = 0,
		int32 AutoRecoveryRemainingAmount = INDEX_NONE);

	// Instantiates the right tooltip subclass for the item (Template->ResolveTooltipClass) and fills
	// it. Returns null for an invalid template. ShopPrice >= 0 appends the affordability price line.
	static UMT2ItemTooltipWidget* Create(
		APlayerController* Owner, const UMT2ItemTemplate* Template, int32 Count,
		const AMT2PlayerState* Viewer, int64 ShopPrice = -1,
		const TArray<FMT2ItemBonus>* Bonuses = nullptr,
		const TArray<FMT2MetinSocket>* MetinSockets = nullptr, int32 SkillVnum = 0,
		int32 AutoRecoveryRemainingAmount = INDEX_NONE);

	// Old client colors.
	static const FLinearColor NormalColor;
	static const FLinearColor PositiveColor;
	static const FLinearColor NegativeColor;
	static const FLinearColor TitleColor;
	static const FLinearColor MaxBonusColor;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	// Per-type body between the description and the shared stat-bonus footer.
	virtual void BuildTypeSpecific(const UMT2ItemTemplate* Template, const AMT2PlayerState* Viewer) {}

	void AddLine(const FString& Text, const FLinearColor& Color, int32 FontSize = 10);
	void AddSpace(float Height = 5.0f);
	void AddLimits(const UMT2ItemTemplate* Template, const AMT2PlayerState* Viewer);
	void AddApplies(const UMT2ItemTemplate* Template);
	void AddWearableRaces(const UMT2ItemTemplate* Template);
	void AddMetinSockets(const TArray<FMT2MetinSocket>* MetinSockets);

	// Human-readable "STR +5" / "Movement Speed +10%" for an APPLY ordinal (shared with equip logic).
	static FString FormatApply(int32 ApplyType, int32 Value);

private:
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> LinesBox;
};
