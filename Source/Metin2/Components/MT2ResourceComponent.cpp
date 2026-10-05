/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Components/MT2ResourceComponent.h"

#include "AbilitySystemComponent.h"

UMT2ResourceComponent::UMT2ResourceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UMT2ResourceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UninitializeFromAbilitySystem();
	Super::EndPlay(EndPlayReason);
}

void UMT2ResourceComponent::InitializeWithAbilitySystem(UAbilitySystemComponent* InAbilitySystem)
{
	if (AbilitySystem.Get() == InAbilitySystem || !InAbilitySystem || !CurrentAttribute.IsValid() || !MaxAttribute.IsValid())
	{
		return;
	}

	UninitializeFromAbilitySystem();
	AbilitySystem = InAbilitySystem;
	CurrentChangedHandle = InAbilitySystem->GetGameplayAttributeValueChangeDelegate(CurrentAttribute).AddUObject(
		this, &UMT2ResourceComponent::OnCurrentAttributeChanged);
	MaxChangedHandle = InAbilitySystem->GetGameplayAttributeValueChangeDelegate(MaxAttribute).AddUObject(
		this, &UMT2ResourceComponent::OnMaxAttributeChanged);
}

void UMT2ResourceComponent::UninitializeFromAbilitySystem()
{
	if (UAbilitySystemComponent* ExistingAbilitySystem = AbilitySystem.Get())
	{
		ExistingAbilitySystem->GetGameplayAttributeValueChangeDelegate(CurrentAttribute).Remove(CurrentChangedHandle);
		ExistingAbilitySystem->GetGameplayAttributeValueChangeDelegate(MaxAttribute).Remove(MaxChangedHandle);
	}

	AbilitySystem.Reset();
	CurrentChangedHandle.Reset();
	MaxChangedHandle.Reset();
}

float UMT2ResourceComponent::GetCurrentValue() const
{
	return AbilitySystem.IsValid() ? AbilitySystem->GetNumericAttribute(CurrentAttribute) : 0.0f;
}

float UMT2ResourceComponent::GetMaxValue() const
{
	return AbilitySystem.IsValid() ? AbilitySystem->GetNumericAttribute(MaxAttribute) : 0.0f;
}

float UMT2ResourceComponent::GetNormalizedValue() const
{
	const float Maximum = GetMaxValue();
	return Maximum > 0.0f ? FMath::Clamp(GetCurrentValue() / Maximum, 0.0f, 1.0f) : 0.0f;
}

bool UMT2ResourceComponent::SetCurrentValue(float NewValue)
{
	UAbilitySystemComponent* ExistingAbilitySystem = AbilitySystem.Get();
	if (!ExistingAbilitySystem || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	ExistingAbilitySystem->SetNumericAttributeBase(CurrentAttribute, FMath::Clamp(NewValue, 0.0f, GetMaxValue()));
	return true;
}

bool UMT2ResourceComponent::SetMaxValue(float NewValue)
{
	UAbilitySystemComponent* ExistingAbilitySystem = AbilitySystem.Get();
	if (!ExistingAbilitySystem || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	ExistingAbilitySystem->SetNumericAttributeBase(MaxAttribute, FMath::Max(NewValue, 0.0f));
	return true;
}

void UMT2ResourceComponent::ConfigureAttributes(
	FGameplayAttribute InCurrentAttribute, FGameplayAttribute InMaxAttribute)
{
	CurrentAttribute = InCurrentAttribute;
	MaxAttribute = InMaxAttribute;
}

void UMT2ResourceComponent::HandleCurrentValueChanged(float OldValue, float NewValue)
{
	OnValueChanged.Broadcast(OldValue, NewValue);
}

void UMT2ResourceComponent::HandleMaxValueChanged(float OldValue, float NewValue)
{
	OnMaxValueChanged.Broadcast(OldValue, NewValue);
}

void UMT2ResourceComponent::OnCurrentAttributeChanged(const FOnAttributeChangeData& ChangeData)
{
	HandleCurrentValueChanged(ChangeData.OldValue, ChangeData.NewValue);
}

void UMT2ResourceComponent::OnMaxAttributeChanged(const FOnAttributeChangeData& ChangeData)
{
	HandleMaxValueChanged(ChangeData.OldValue, ChangeData.NewValue);
}
