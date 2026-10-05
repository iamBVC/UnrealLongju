/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Components/MT2ResourceComponent.h"
#include "MT2HealthComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMT2DeathStateSignature);

UCLASS(BlueprintType, ClassGroup = "MT2", meta = (BlueprintSpawnableComponent))
class METIN2_API UMT2HealthComponent : public UMT2ResourceComponent
{
	GENERATED_BODY()

public:
	UMT2HealthComponent();

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetHealth() const { return GetCurrentValue(); }

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetMaxHealth() const { return GetMaxValue(); }

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetHealthNormalized() const { return GetNormalizedValue(); }

	UFUNCTION(BlueprintPure, Category = "Health")
	bool IsDead() const { return GetHealth() <= 0.0f; }

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Health")
	bool SetHealth(float NewHealth) { return SetCurrentValue(NewHealth); }

	UPROPERTY(BlueprintAssignable, Category = "Health")
	FMT2DeathStateSignature OnDeath;

	UPROPERTY(BlueprintAssignable, Category = "Health")
	FMT2DeathStateSignature OnRevived;

protected:
	virtual void HandleCurrentValueChanged(float OldValue, float NewValue) override;
};
