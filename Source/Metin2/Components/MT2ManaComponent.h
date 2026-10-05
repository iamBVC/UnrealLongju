/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Components/MT2ResourceComponent.h"
#include "MT2ManaComponent.generated.h"

UCLASS(BlueprintType, ClassGroup = "MT2", meta = (BlueprintSpawnableComponent))
class METIN2_API UMT2ManaComponent : public UMT2ResourceComponent
{
	GENERATED_BODY()

public:
	UMT2ManaComponent();

	UFUNCTION(BlueprintPure, Category = "Mana")
	float GetMana() const { return GetCurrentValue(); }

	UFUNCTION(BlueprintPure, Category = "Mana")
	float GetMaxMana() const { return GetMaxValue(); }

	UFUNCTION(BlueprintPure, Category = "Mana")
	float GetManaNormalized() const { return GetNormalizedValue(); }

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Mana")
	bool SetMana(float NewMana) { return SetCurrentValue(NewMana); }
};
