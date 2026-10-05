/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Input/MT2InputConfig.h"

#include "EnhancedActionKeyMapping.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

namespace
{
	void AddNegateModifier(FEnhancedActionKeyMapping& Mapping, UObject* Outer)
	{
		Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(Outer));
	}

	void AddSwizzleModifier(FEnhancedActionKeyMapping& Mapping, UObject* Outer)
	{
		Mapping.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(Outer));
	}
}

UMT2InputConfig* UMT2InputConfig::CreateNativeDefaults(UObject* Outer)
{
	UMT2InputConfig* Config = NewObject<UMT2InputConfig>(Outer, TEXT("NativeInputConfig"), RF_Transient);
	Config->DefaultMappingContext = NewObject<UInputMappingContext>(Config, TEXT("IMC_NativeDefault"), RF_Transient);

	Config->MoveAction = NewObject<UInputAction>(Config, TEXT("IA_Move"), RF_Transient);
	Config->MoveAction->ValueType = EInputActionValueType::Axis2D;

	Config->LookAction = NewObject<UInputAction>(Config, TEXT("IA_Look"), RF_Transient);
	Config->LookAction->ValueType = EInputActionValueType::Axis2D;

	Config->AttackAction = NewObject<UInputAction>(Config, TEXT("IA_Attack"), RF_Transient);
	Config->AttackAction->ValueType = EInputActionValueType::Boolean;

	Config->WalkAction = NewObject<UInputAction>(Config, TEXT("IA_Walk"), RF_Transient);
	Config->WalkAction->ValueType = EInputActionValueType::Boolean;

	Config->CameraRotateAction = NewObject<UInputAction>(Config, TEXT("IA_CameraRotate"), RF_Transient);
	Config->CameraRotateAction->ValueType = EInputActionValueType::Boolean;

	Config->ClickMoveAction = NewObject<UInputAction>(Config, TEXT("IA_ClickMove"), RF_Transient);
	Config->ClickMoveAction->ValueType = EInputActionValueType::Boolean;

	Config->KeyboardCameraYawAction = NewObject<UInputAction>(Config, TEXT("IA_KeyboardCameraYaw"), RF_Transient);
	Config->KeyboardCameraYawAction->ValueType = EInputActionValueType::Axis1D;

	Config->KeyboardCameraZoomAction = NewObject<UInputAction>(Config, TEXT("IA_KeyboardCameraZoom"), RF_Transient);
	Config->KeyboardCameraZoomAction->ValueType = EInputActionValueType::Axis1D;

	Config->KeyboardCameraPitchAction = NewObject<UInputAction>(Config, TEXT("IA_KeyboardCameraPitch"), RF_Transient);
	Config->KeyboardCameraPitchAction->ValueType = EInputActionValueType::Axis1D;

	FEnhancedActionKeyMapping& Forward = Config->DefaultMappingContext->MapKey(Config->MoveAction, EKeys::W);
	AddSwizzleModifier(Forward, Config->DefaultMappingContext);

	FEnhancedActionKeyMapping& Backward = Config->DefaultMappingContext->MapKey(Config->MoveAction, EKeys::S);
	AddNegateModifier(Backward, Config->DefaultMappingContext);
	AddSwizzleModifier(Backward, Config->DefaultMappingContext);

	Config->DefaultMappingContext->MapKey(Config->MoveAction, EKeys::D);
	FEnhancedActionKeyMapping& Left = Config->DefaultMappingContext->MapKey(Config->MoveAction, EKeys::A);
	AddNegateModifier(Left, Config->DefaultMappingContext);

	Config->DefaultMappingContext->MapKey(Config->MoveAction, EKeys::Gamepad_Left2D);
	Config->DefaultMappingContext->MapKey(Config->LookAction, EKeys::Mouse2D);
	Config->DefaultMappingContext->MapKey(Config->LookAction, EKeys::Gamepad_Right2D);
	Config->DefaultMappingContext->MapKey(Config->AttackAction, EKeys::SpaceBar);
	Config->DefaultMappingContext->MapKey(Config->AttackAction, EKeys::Gamepad_FaceButton_Bottom);
	Config->DefaultMappingContext->MapKey(Config->WalkAction, EKeys::LeftControl);
	Config->DefaultMappingContext->MapKey(Config->CameraRotateAction, EKeys::RightMouseButton);
	Config->DefaultMappingContext->MapKey(Config->ClickMoveAction, EKeys::LeftMouseButton);

	FEnhancedActionKeyMapping& RotateLeft =
		Config->DefaultMappingContext->MapKey(Config->KeyboardCameraYawAction, EKeys::Q);
	AddNegateModifier(RotateLeft, Config->DefaultMappingContext);
	Config->DefaultMappingContext->MapKey(Config->KeyboardCameraYawAction, EKeys::E);

	FEnhancedActionKeyMapping& ZoomIn =
		Config->DefaultMappingContext->MapKey(Config->KeyboardCameraZoomAction, EKeys::R);
	AddNegateModifier(ZoomIn, Config->DefaultMappingContext);
	Config->DefaultMappingContext->MapKey(Config->KeyboardCameraZoomAction, EKeys::F);

	FEnhancedActionKeyMapping& PitchUp =
		Config->DefaultMappingContext->MapKey(Config->KeyboardCameraPitchAction, EKeys::T);
	AddNegateModifier(PitchUp, Config->DefaultMappingContext);
	Config->DefaultMappingContext->MapKey(Config->KeyboardCameraPitchAction, EKeys::G);

	return Config;
}
