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

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Use|Buff")
	EMT2ItemBonusType GrantedBonus = EMT2ItemBonusType::None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Use|Buff")
	int32 GrantedBonusValue = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Use|Buff")
	int32 BuffDurationSeconds = 0;

	// USE_AFFECT Value0. Multiple data-driven consumables may grant the same apply type under
	// different affect IDs, so keep the identity separate from the bonus enum.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Use|Buff")
	FMT2AffectIdentifier AffectType;
};
