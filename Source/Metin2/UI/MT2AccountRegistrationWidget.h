/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Types/SlateEnums.h"
#include "UI/MT2UserWidget.h"
#include "MT2AccountRegistrationWidget.generated.h"

class UButton;
class UEditableTextBox;
class UTextBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FMT2RegistrationSucceededSignature, const FString&, Username);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMT2RegistrationBackSignature);

// Account-registration step. Layout lives in /Game/UI/MT2AccountRegistration and all controls are
// required BindWidgets so the screen remains fully editable in UMG.
UCLASS()
class METIN2_API UMT2AccountRegistrationWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Registration")
	FMT2RegistrationSucceededSignature OnRegistrationSucceeded;

	UPROPERTY(BlueprintAssignable, Category = "Registration")
	FMT2RegistrationBackSignature OnBackRequested;

	UFUNCTION(BlueprintCallable, Category = "Registration")
	void PrepareForDisplay();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	UFUNCTION() void HandleCreateAccountClicked();
	UFUNCTION() void HandleBackClicked();
	UFUNCTION() void HandlePasswordCommitted(const FText& Text, ETextCommit::Type CommitMethod);
	UFUNCTION() void HandleRegistrationCompleted(bool bSucceeded, const FString& Error);

	void SetStatus(const FString& Message);
	void SetControlsEnabled(bool bEnabled);
	class AMT2PlayerController* GetMT2Controller() const;

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UEditableTextBox> UsernameBox;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UEditableTextBox> PasswordBox;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UEditableTextBox> ConfirmPasswordBox;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> CreateAccountButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> BackButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> StatusText;

	FString PendingUsername;
};
