/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once
#include "Items/Templates/MT2ItemConsumableTemplate.h"
#include "MT2ItemUseTemplate.generated.h"

UCLASS(Blueprintable)
class METIN2_API UMT2ItemUseTemplate : public UMT2ItemConsumableTemplate
{
	GENERATED_BODY()

public:
	virtual TSubclassOf<UMT2ItemTooltipWidget> GetDefaultTooltipClass() const override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Use")
	EMT2UseSubType UseSubType = EMT2UseSubType::Potion;

	// Load-only compatibility defaults for old Blueprint child delta archives. Authoring and
	// gameplay use the dedicated subtype fields; keep these defaults when a parent migrates.
	UPROPERTY()
	EMT2ItemBonusType GrantedBonus = EMT2ItemBonusType::None;

	UPROPERTY()
	int32 GrantedBonusValue = 0;

	UPROPERTY()
	int32 BuffDurationSeconds = 0;

	UPROPERTY()
	FMT2AffectIdentifier AffectType;
};
