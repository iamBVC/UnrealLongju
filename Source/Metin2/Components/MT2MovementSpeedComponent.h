/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "MT2MovementSpeedComponent.generated.h"

class UAbilitySystemComponent;
struct FOnAttributeChangeData;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FMT2MovementSpeedChangedSignature, float, OldSpeed, float, NewSpeed);

UCLASS(BlueprintType, ClassGroup = "MT2", meta = (BlueprintSpawnableComponent))
class METIN2_API UMT2MovementSpeedComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMT2MovementSpeedComponent();
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void InitializeWithAbilitySystem(UAbilitySystemComponent* InAbilitySystem);

	UFUNCTION(BlueprintPure, Category = "Movement")
	float GetMovementSpeed() const;

	UFUNCTION(BlueprintPure, Category = "Movement")
	float GetMovementSpeedMultiplier() const { return GetMovementSpeed() / 100.0f; }

	UFUNCTION(BlueprintPure, Category = "Movement")
	bool IsOutOfStamina() const;

	UFUNCTION(BlueprintPure, Category = "Movement")
	bool ShouldUseWalkMovement() const;

	void RefreshMovementSpeed();

	// Movement-mode multiplier (mounts now, future transformations later). Keeping it here means a
	// stat/equipment refresh cannot silently remove the active mode's speed adjustment.
	UFUNCTION(BlueprintCallable, Category = "Movement")
	void SetExternalSpeedMultiplier(float NewMultiplier);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Movement")
	bool SetMovementSpeed(float NewSpeed);

	UPROPERTY(BlueprintAssignable, Category = "Movement")
	FMT2MovementSpeedChangedSignature OnMovementSpeedChanged;

private:
	void Uninitialize();
	void HandleMovementSpeedChanged(const FOnAttributeChangeData& ChangeData);
	void HandleStaminaChanged(const FOnAttributeChangeData& ChangeData);
	void ApplyMovementSpeed(float NewSpeed);

	UPROPERTY(EditDefaultsOnly, Category = "Movement", meta = (ClampMin = "0.0", Units = "cm/s"))
	float BaseMovementSpeed = 450.0f;

	float ExternalSpeedMultiplier = 1.0f;

	TWeakObjectPtr<UAbilitySystemComponent> AbilitySystem;
	FDelegateHandle MovementSpeedChangedHandle;
	FDelegateHandle StaminaChangedHandle;
};
