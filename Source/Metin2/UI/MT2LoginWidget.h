/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "UI/MT2UserWidget.h"
#include "CoreMinimal.h"
#include "Types/SlateEnums.h"
#include "MT2LoginWidget.generated.h"

class UButton;
class UEditableTextBox;
class UTextBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMT2LoginFlowSignature);

// Login step of the gateway flow (old client: loginwindow.py phase). Binds to the /Game/UI/MT2Login
// Widget Blueprint, which must contain: UsernameBox + PasswordBox (EditableTextBox), LoginButton +
// RegisterButton (Button), StatusText (TextBlock). Logic only - layout/art belongs to the Blueprint.
UCLASS()
class METIN2_API UMT2LoginWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	// Fired when authentication succeeded and the character list is ready - the gateway HUD swaps to
	// the character-select widget.
	UPROPERTY(BlueprintAssignable, Category = "Login")
	FMT2LoginFlowSignature OnLoginSucceeded;

	UPROPERTY(BlueprintAssignable, Category = "Login")
	FMT2LoginFlowSignature OnRegistrationRequested;

	UFUNCTION(BlueprintCallable, Category = "Login")
	void PrepareForDisplay(const FString& Username);

	void ShowError(const FString& Error);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	UFUNCTION()
	void HandleLoginClicked();

	UFUNCTION()
	void HandleRegisterClicked();

	UFUNCTION()
	void HandleExitClicked();

	UFUNCTION()
	void HandleLoginCompleted(bool bSucceeded, const FString& Error);

	UFUNCTION()
	void HandlePasswordCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	void SetStatus(const FString& Message);
	void SetControlsEnabled(bool bEnabled);
	class AMT2PlayerController* GetMT2Controller() const;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UEditableTextBox> UsernameBox;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UEditableTextBox> PasswordBox;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> LoginButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> RegisterButton;

	// Optional so existing user-authored login Blueprints keep compiling; add a Button named
	// "ExitButton" to the Blueprint and it closes the client (old login window's Exit).
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> ExitButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> StatusText;
};
