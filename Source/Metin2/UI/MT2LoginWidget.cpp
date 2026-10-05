/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2LoginWidget.h"

#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Player/MT2PlayerController.h"

void UMT2LoginWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (LoginButton)
	{
		LoginButton->OnClicked.AddUniqueDynamic(this, &UMT2LoginWidget::HandleLoginClicked);
	}
	if (RegisterButton)
	{
		RegisterButton->OnClicked.AddUniqueDynamic(this, &UMT2LoginWidget::HandleRegisterClicked);
	}
	if (ExitButton)
	{
		ExitButton->OnClicked.AddUniqueDynamic(this, &UMT2LoginWidget::HandleExitClicked);
	}
	if (PasswordBox)
	{
		PasswordBox->SetIsPassword(true);
		PasswordBox->OnTextCommitted.AddUniqueDynamic(this, &UMT2LoginWidget::HandlePasswordCommitted);
	}
	if (AMT2PlayerController* Controller = GetMT2Controller())
	{
		Controller->OnLoginCompleted.AddUniqueDynamic(this, &UMT2LoginWidget::HandleLoginCompleted);
	}
	SetStatus(FString());
}

void UMT2LoginWidget::NativeDestruct()
{
	if (AMT2PlayerController* Controller = GetMT2Controller())
	{
		Controller->OnLoginCompleted.RemoveDynamic(this, &UMT2LoginWidget::HandleLoginCompleted);
	}
	if (PasswordBox)
	{
		PasswordBox->OnTextCommitted.RemoveDynamic(this, &UMT2LoginWidget::HandlePasswordCommitted);
	}
	Super::NativeDestruct();
}

void UMT2LoginWidget::PrepareForDisplay(const FString& Username)
{
	if (UsernameBox && !Username.IsEmpty())
	{
		UsernameBox->SetText(FText::FromString(Username));
	}
	if (PasswordBox)
	{
		PasswordBox->SetText(FText::GetEmpty());
	}
	SetControlsEnabled(true);
	SetStatus(FString());
	if (UsernameBox)
	{
		UsernameBox->SetKeyboardFocus();
	}
}

void UMT2LoginWidget::ShowError(const FString& Error)
{
	SetControlsEnabled(true);
	SetStatus(Error);
}

AMT2PlayerController* UMT2LoginWidget::GetMT2Controller() const
{
	return Cast<AMT2PlayerController>(GetOwningPlayer());
}

void UMT2LoginWidget::HandleLoginClicked()
{
	AMT2PlayerController* Controller = GetMT2Controller();
	const FString Username = UsernameBox ? UsernameBox->GetText().ToString().TrimStartAndEnd() : FString();
	const FString Password = PasswordBox ? PasswordBox->GetText().ToString() : FString();
	if (!Controller || Username.IsEmpty() || Password.IsEmpty())
	{
		SetStatus(TEXT("Enter account name and password."));
		return;
	}
	SetStatus(TEXT("Logging in..."));
	SetControlsEnabled(false);
	Controller->Login(Username, Password);
}

void UMT2LoginWidget::HandleRegisterClicked()
{
	OnRegistrationRequested.Broadcast();
}

void UMT2LoginWidget::HandleExitClicked()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

void UMT2LoginWidget::HandleLoginCompleted(bool bSucceeded, const FString& Error)
{
	if (bSucceeded)
	{
		SetStatus(Error);
		OnLoginSucceeded.Broadcast();
	}
	else
	{
		SetStatus(Error.IsEmpty() ? TEXT("Login failed.") : Error);
	}
	SetControlsEnabled(true);
}

void UMT2LoginWidget::HandlePasswordCommitted(const FText&, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		HandleLoginClicked();
	}
}

void UMT2LoginWidget::SetStatus(const FString& Message)
{
	if (StatusText)
	{
		StatusText->SetText(FText::FromString(Message));
	}
}

void UMT2LoginWidget::SetControlsEnabled(bool bEnabled)
{
	if (UsernameBox) UsernameBox->SetIsEnabled(bEnabled);
	if (PasswordBox) PasswordBox->SetIsEnabled(bEnabled);
	if (LoginButton) LoginButton->SetIsEnabled(bEnabled);
	if (RegisterButton) RegisterButton->SetIsEnabled(bEnabled);
}
