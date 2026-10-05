/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Components/MT2ResourceComponent.h"
#include "MT2StaminaComponent.generated.h"

UCLASS(BlueprintType, ClassGroup = "MT2", meta = (BlueprintSpawnableComponent))
class METIN2_API UMT2StaminaComponent : public UMT2ResourceComponent
{
	GENERATED_BODY()

public:
	UMT2StaminaComponent();

	UFUNCTION(BlueprintPure, Category = "Stamina")
	float GetStamina() const { return GetCurrentValue(); }

	UFUNCTION(BlueprintPure, Category = "Stamina")
	float GetMaxStamina() const { return GetMaxValue(); }

	UFUNCTION(BlueprintPure, Category = "Stamina")
	float GetStaminaNormalized() const { return GetNormalizedValue(); }

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Stamina")
	bool SetStamina(float NewStamina) { return SetCurrentValue(NewStamina); }
};
