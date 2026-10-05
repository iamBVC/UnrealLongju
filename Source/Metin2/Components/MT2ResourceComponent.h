/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "AttributeSet.h"
#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "MT2ResourceComponent.generated.h"

class UAbilitySystemComponent;
struct FOnAttributeChangeData;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FMT2ResourceChangedSignature, float, OldValue, float, NewValue);

UCLASS(Abstract, BlueprintType)
class METIN2_API UMT2ResourceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMT2ResourceComponent();
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void InitializeWithAbilitySystem(UAbilitySystemComponent* InAbilitySystem);
	void UninitializeFromAbilitySystem();

	UFUNCTION(BlueprintPure, Category = "Resource")
	bool IsInitialized() const { return AbilitySystem.IsValid(); }

	UFUNCTION(BlueprintPure, Category = "Resource")
	float GetCurrentValue() const;

	UFUNCTION(BlueprintPure, Category = "Resource")
	float GetMaxValue() const;

	UFUNCTION(BlueprintPure, Category = "Resource")
	float GetNormalizedValue() const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Resource")
	bool SetCurrentValue(float NewValue);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Resource")
	bool SetMaxValue(float NewValue);

	UPROPERTY(BlueprintAssignable, Category = "Resource")
	FMT2ResourceChangedSignature OnValueChanged;

	UPROPERTY(BlueprintAssignable, Category = "Resource")
	FMT2ResourceChangedSignature OnMaxValueChanged;

protected:
	void ConfigureAttributes(FGameplayAttribute InCurrentAttribute, FGameplayAttribute InMaxAttribute);
	virtual void HandleCurrentValueChanged(float OldValue, float NewValue);
	virtual void HandleMaxValueChanged(float OldValue, float NewValue);

	TWeakObjectPtr<UAbilitySystemComponent> AbilitySystem;

private:
	void OnCurrentAttributeChanged(const FOnAttributeChangeData& ChangeData);
	void OnMaxAttributeChanged(const FOnAttributeChangeData& ChangeData);

	FGameplayAttribute CurrentAttribute;
	FGameplayAttribute MaxAttribute;
	FDelegateHandle CurrentChangedHandle;
	FDelegateHandle MaxChangedHandle;
};
