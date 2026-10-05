/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Components/MT2MovementSpeedComponent.h"

#include "Abilities/MT2CoreAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Characters/MT2CharacterBase.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

UMT2MovementSpeedComponent::UMT2MovementSpeedComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UMT2MovementSpeedComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Uninitialize();
	Super::EndPlay(EndPlayReason);
}

void UMT2MovementSpeedComponent::InitializeWithAbilitySystem(UAbilitySystemComponent* InAbilitySystem)
{
	if (!InAbilitySystem || AbilitySystem.Get() == InAbilitySystem)
	{
		return;
	}

	Uninitialize();
	AbilitySystem = InAbilitySystem;
	MovementSpeedChangedHandle = InAbilitySystem->GetGameplayAttributeValueChangeDelegate(
		UMT2CoreAttributeSet::GetMovementSpeedAttribute()).AddUObject(
			this, &UMT2MovementSpeedComponent::HandleMovementSpeedChanged);
	StaminaChangedHandle = InAbilitySystem->GetGameplayAttributeValueChangeDelegate(
		UMT2CoreAttributeSet::GetStaminaAttribute()).AddUObject(
			this, &UMT2MovementSpeedComponent::HandleStaminaChanged);
	ApplyMovementSpeed(GetMovementSpeed());
}

float UMT2MovementSpeedComponent::GetMovementSpeed() const
{
	return AbilitySystem.IsValid()
		? AbilitySystem->GetNumericAttribute(UMT2CoreAttributeSet::GetMovementSpeedAttribute())
		: 100.0f;
}

bool UMT2MovementSpeedComponent::IsOutOfStamina() const
{
	return AbilitySystem.IsValid() &&
		AbilitySystem->GetNumericAttribute(UMT2CoreAttributeSet::GetStaminaAttribute()) <= 0.0f;
}

bool UMT2MovementSpeedComponent::ShouldUseWalkMovement() const
{
	const AMT2CharacterBase* Character = Cast<AMT2CharacterBase>(GetOwner());
	return IsOutOfStamina() || (Character && Character->IsWalkRequested());
}

void UMT2MovementSpeedComponent::RefreshMovementSpeed()
{
	ApplyMovementSpeed(GetMovementSpeed());
}

void UMT2MovementSpeedComponent::SetExternalSpeedMultiplier(float NewMultiplier)
{
	ExternalSpeedMultiplier = FMath::Max(NewMultiplier, 0.01f);
	RefreshMovementSpeed();
}

bool UMT2MovementSpeedComponent::SetMovementSpeed(float NewSpeed)
{
	UAbilitySystemComponent* ExistingAbilitySystem = AbilitySystem.Get();
	if (!ExistingAbilitySystem || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	ExistingAbilitySystem->SetNumericAttributeBase(
		UMT2CoreAttributeSet::GetMovementSpeedAttribute(), FMath::Max(NewSpeed, 0.0f));
	return true;
}

void UMT2MovementSpeedComponent::Uninitialize()
{
	if (UAbilitySystemComponent* ExistingAbilitySystem = AbilitySystem.Get())
	{
		ExistingAbilitySystem->GetGameplayAttributeValueChangeDelegate(
			UMT2CoreAttributeSet::GetMovementSpeedAttribute()).Remove(MovementSpeedChangedHandle);
		ExistingAbilitySystem->GetGameplayAttributeValueChangeDelegate(
			UMT2CoreAttributeSet::GetStaminaAttribute()).Remove(StaminaChangedHandle);
	}
	AbilitySystem.Reset();
	MovementSpeedChangedHandle.Reset();
	StaminaChangedHandle.Reset();
}

void UMT2MovementSpeedComponent::HandleMovementSpeedChanged(const FOnAttributeChangeData& ChangeData)
{
	ApplyMovementSpeed(ChangeData.NewValue);
	OnMovementSpeedChanged.Broadcast(ChangeData.OldValue, ChangeData.NewValue);
}

void UMT2MovementSpeedComponent::HandleStaminaChanged(const FOnAttributeChangeData& ChangeData)
{
	RefreshMovementSpeed();
}

void UMT2MovementSpeedComponent::ApplyMovementSpeed(float NewSpeed)
{
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		const float SpeedMultiplier = FMath::Max(NewSpeed, 0.0f) / 100.0f;
		const float WalkMultiplier = ShouldUseWalkMovement() ? 0.45f : 1.0f;
		Character->GetCharacterMovement()->MaxWalkSpeed =
			BaseMovementSpeed * SpeedMultiplier * WalkMultiplier * ExternalSpeedMultiplier;
	}
}
