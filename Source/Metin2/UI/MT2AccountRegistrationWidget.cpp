/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2AccountRegistrationWidget.h"

#include "Authentication/MT2AuthenticationUtils.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Player/MT2PlayerController.h"

void UMT2AccountRegistrationWidget::NativeConstruct()
{
	Super::NativeConstruct();
	CreateAccountButton->OnClicked.AddUniqueDynamic(
		this, &UMT2AccountRegistrationWidget::HandleCreateAccountClicked);
	BackButton->OnClicked.AddUniqueDynamic(this, &UMT2AccountRegistrationWidget::HandleBackClicked);
	ConfirmPasswordBox->OnTextCommitted.AddUniqueDynamic(
		this, &UMT2AccountRegistrationWidget::HandlePasswordCommitted);
	PasswordBox->SetIsPassword(true);
	ConfirmPasswordBox->SetIsPassword(true);
	if (AMT2PlayerController* Controller = GetMT2Controller())
	{
		Controller->OnRegistrationCompleted.AddUniqueDynamic(
			this, &UMT2AccountRegistrationWidget::HandleRegistrationCompleted);
	}
}

void UMT2AccountRegistrationWidget::NativeDestruct()
{
	if (AMT2PlayerController* Controller = GetMT2Controller())
	{
		Controller->OnRegistrationCompleted.RemoveDynamic(
			this, &UMT2AccountRegistrationWidget::HandleRegistrationCompleted);
	}
	ConfirmPasswordBox->OnTextCommitted.RemoveDynamic(
		this, &UMT2AccountRegistrationWidget::HandlePasswordCommitted);
	Super::NativeDestruct();
}

void UMT2AccountRegistrationWidget::PrepareForDisplay()
{
	PendingUsername.Reset();
	PasswordBox->SetText(FText::GetEmpty());
	ConfirmPasswordBox->SetText(FText::GetEmpty());
	SetControlsEnabled(true);
	SetStatus(FString());
	UsernameBox->SetKeyboardFocus();
}

AMT2PlayerController* UMT2AccountRegistrationWidget::GetMT2Controller() const
{
	return Cast<AMT2PlayerController>(GetOwningPlayer());
}

void UMT2AccountRegistrationWidget::HandleCreateAccountClicked()
{
	AMT2PlayerController* Controller = GetMT2Controller();
	const FString Username = UsernameBox->GetText().ToString().TrimStartAndEnd();
	const FString Password = PasswordBox->GetText().ToString();
	const FString Confirmation = ConfirmPasswordBox->GetText().ToString();
	FString ValidationError;
	if (!Controller)
	{
		SetStatus(TEXT("Gateway is unavailable."));
		return;
	}
	if (!MT2Authentication::ValidateUsername(Username, ValidationError) ||
		!MT2Authentication::ValidatePassword(Password, ValidationError))
	{
		SetStatus(ValidationError);
		return;
	}
	if (Password != Confirmation)
	{
		SetStatus(TEXT("Passwords do not match."));
		return;
	}

	PendingUsername = Username;
	SetStatus(TEXT("Creating account..."));
	SetControlsEnabled(false);
	Controller->RegisterAccount(Username, Password);
}

void UMT2AccountRegistrationWidget::HandleBackClicked()
{
	OnBackRequested.Broadcast();
}

void UMT2AccountRegistrationWidget::HandlePasswordCommitted(
	const FText&, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		HandleCreateAccountClicked();
	}
}

void UMT2AccountRegistrationWidget::HandleRegistrationCompleted(
	bool bSucceeded, const FString& Error)
{
	SetControlsEnabled(true);
	if (!bSucceeded)
	{
		SetStatus(Error.IsEmpty() ? TEXT("Registration failed.") : Error);
		return;
	}
	SetStatus(TEXT("Account created."));
	OnRegistrationSucceeded.Broadcast(PendingUsername);
}

void UMT2AccountRegistrationWidget::SetStatus(const FString& Message)
{
	StatusText->SetText(FText::FromString(Message));
}

void UMT2AccountRegistrationWidget::SetControlsEnabled(bool bEnabled)
{
	UsernameBox->SetIsEnabled(bEnabled);
	PasswordBox->SetIsEnabled(bEnabled);
	ConfirmPasswordBox->SetIsEnabled(bEnabled);
	CreateAccountButton->SetIsEnabled(bEnabled);
	BackButton->SetIsEnabled(bEnabled);
}
