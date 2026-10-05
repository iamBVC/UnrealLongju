/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MT2InputConfig.generated.h"

class UInputAction;
class UInputMappingContext;

UCLASS(BlueprintType)
class METIN2_API UMT2InputConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	static UMT2InputConfig* CreateNativeDefaults(UObject* Outer);

	UInputMappingContext* GetDefaultMappingContext() const { return DefaultMappingContext; }
	UInputAction* GetMoveAction() const { return MoveAction; }
	UInputAction* GetLookAction() const { return LookAction; }
	UInputAction* GetAttackAction() const { return AttackAction; }
	UInputAction* GetWalkAction() const { return WalkAction; }
	UInputAction* GetCameraRotateAction() const { return CameraRotateAction; }
	UInputAction* GetClickMoveAction() const { return ClickMoveAction; }
	UInputAction* GetKeyboardCameraYawAction() const { return KeyboardCameraYawAction; }
	UInputAction* GetKeyboardCameraZoomAction() const { return KeyboardCameraZoomAction; }
	UInputAction* GetKeyboardCameraPitchAction() const { return KeyboardCameraPitchAction; }

private:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> AttackAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> WalkAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> CameraRotateAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> ClickMoveAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> KeyboardCameraYawAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> KeyboardCameraZoomAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> KeyboardCameraPitchAction;
};
