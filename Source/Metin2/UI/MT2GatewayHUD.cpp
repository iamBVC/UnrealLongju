/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2GatewayHUD.h"

#include "Audio/MT2AudioUserSettings.h"
#include "Authentication/MT2ClientSessionSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Components/AudioComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/NetConnection.h"
#include "Sound/SoundBase.h"
#include "UI/MT2AccountRegistrationWidget.h"
#include "UI/MT2CharacterCreateWidget.h"
#include "UI/MT2CharacterSelectWidget.h"
#include "UI/MT2LoadingScreenWidget.h"
#include "UI/MT2LoginWidget.h"
#include "UI/MT2SystemMenuWidget.h"

void AMT2GatewayHUD::BeginPlay()
{
	Super::BeginPlay();

	APlayerController* OwningController = GetOwningPlayerController();
	if (!OwningController || !OwningController->IsLocalController())
	{
		return;
	}

	UMT2SystemMenuWidget::ApplySavedAudioVideoSettings(this);
	FMT2AudioUserSettings::OnChanged.AddWeakLambda(this, [this]
	{
		if (MusicComponent)
		{
			MusicComponent->SetVolumeMultiplier(FMT2AudioUserSettings::GetMusicVolume());
		}
	});
	LoginWidget = CreateGatewayWidget<UMT2LoginWidget>(TEXT("MT2Login"));
	RegistrationWidget = CreateGatewayWidget<UMT2AccountRegistrationWidget>(TEXT("MT2AccountRegistration"));
	CharacterSelectWidget = CreateGatewayWidget<UMT2CharacterSelectWidget>(TEXT("MT2CharacterSelect"));
	CharacterCreateWidget = CreateGatewayWidget<UMT2CharacterCreateWidget>(TEXT("MT2CharacterCreate"));
	LoadingScreenWidget = CreateGatewayWidget<UMT2LoadingScreenWidget>(TEXT("MT2LoadingScreen"));
	if (!LoginWidget || !RegistrationWidget || !CharacterSelectWidget || !CharacterCreateWidget ||
		!LoadingScreenWidget)
	{
		return;
	}

	LoginWidget->OnLoginSucceeded.AddUniqueDynamic(this, &AMT2GatewayHUD::ShowCharacterSelect);
	LoginWidget->OnRegistrationRequested.AddUniqueDynamic(this, &AMT2GatewayHUD::ShowRegistration);
	RegistrationWidget->OnRegistrationSucceeded.AddUniqueDynamic(
		this, &AMT2GatewayHUD::HandleRegistrationSucceeded);
	RegistrationWidget->OnBackRequested.AddUniqueDynamic(this, &AMT2GatewayHUD::ShowLogin);
	CharacterSelectWidget->OnCreateRequested.AddUniqueDynamic(this, &AMT2GatewayHUD::ShowCharacterCreate);
	CharacterCreateWidget->OnFinished.AddUniqueDynamic(this, &AMT2GatewayHUD::ShowCharacterSelect);
	if (UMT2ClientSessionSubsystem* Session = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMT2ClientSessionSubsystem>() : nullptr)
	{
		Session->OnStateChanged.AddUniqueDynamic(this, &AMT2GatewayHUD::HandleSessionStateChanged);
		// Remember which gateway this client is on so the in-game Disconnect button can travel
		// back to the login screen later, from whatever map server it ends up on.
		if (const UNetConnection* Connection = OwningController->GetNetConnection())
		{
			Session->SetGatewayAddress(FString::Printf(
				TEXT("%s:%d"), *Connection->URL.Host, Connection->URL.Port));
		}
	}

	SetOnlyVisible(LoginWidget);
	SetBackgroundMusic(LoginMusic);
	LoadingScreenWidget->SetVisibility(ESlateVisibility::Collapsed);
	LoginWidget->PrepareForDisplay(FString());
	if (const UMT2ClientSessionSubsystem* Session = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMT2ClientSessionSubsystem>() : nullptr;
		Session && Session->GetState() == EMT2ClientSessionState::Failed)
	{
		LoginWidget->ShowError(Session->GetLastError());
	}

	OwningController->bShowMouseCursor = true;
	FInputModeUIOnly InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	OwningController->SetInputMode(InputMode);
}

void AMT2GatewayHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	FMT2AudioUserSettings::OnChanged.RemoveAll(this);
	if (UMT2ClientSessionSubsystem* Session = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMT2ClientSessionSubsystem>() : nullptr)
	{
		Session->OnStateChanged.RemoveDynamic(this, &AMT2GatewayHUD::HandleSessionStateChanged);
	}
	if (MusicComponent)
	{
		MusicComponent->OnAudioFinished.RemoveDynamic(
			this, &AMT2GatewayHUD::RestartBackgroundMusic);
		MusicComponent->Stop();
	}
	Super::EndPlay(EndPlayReason);
}

template <typename WidgetType>
WidgetType* AMT2GatewayHUD::CreateGatewayWidget(const TCHAR* AssetName)
{
	const FString ClassPath = FString::Printf(TEXT("/Game/UI/%s.%s_C"), AssetName, AssetName);
	const TSubclassOf<WidgetType> WidgetClass = LoadClass<WidgetType>(nullptr, *ClassPath);
	if (!WidgetClass)
	{
		UE_LOG(LogTemp, Error,
			TEXT("Gateway UI asset /Game/UI/%s is missing or does not derive from the expected C++ widget class."),
			AssetName);
		return nullptr;
	}
	WidgetType* Widget = CreateWidget<WidgetType>(GetOwningPlayerController(), WidgetClass);
	if (Widget)
	{
		Widget->AddToViewport(Widget->IsA<UMT2LoadingScreenWidget>() ? 100 : 0);
		Widget->SetVisibility(ESlateVisibility::Collapsed);
	}
	return Widget;
}

void AMT2GatewayHUD::ShowCharacterSelect()
{
	if (CharacterSelectWidget)
	{
		CharacterSelectWidget->RefreshCharacterList();
	}
	SetOnlyVisible(CharacterSelectWidget);
	SetBackgroundMusic(CharacterSelectionMusic);
}

void AMT2GatewayHUD::ShowCharacterCreate()
{
	SetOnlyVisible(CharacterCreateWidget);
	SetBackgroundMusic(CharacterSelectionMusic);
	if (CharacterCreateWidget)
	{
		CharacterCreateWidget->PrepareForDisplay();
	}
}

void AMT2GatewayHUD::ShowRegistration()
{
	SetOnlyVisible(RegistrationWidget);
	SetBackgroundMusic(LoginMusic);
	if (RegistrationWidget)
	{
		RegistrationWidget->PrepareForDisplay();
	}
}

void AMT2GatewayHUD::ShowLogin()
{
	SetOnlyVisible(LoginWidget);
	SetBackgroundMusic(LoginMusic);
	if (LoginWidget)
	{
		LoginWidget->PrepareForDisplay(FString());
	}
}

void AMT2GatewayHUD::HandleRegistrationSucceeded(const FString& Username)
{
	SetOnlyVisible(LoginWidget);
	SetBackgroundMusic(LoginMusic);
	if (LoginWidget)
	{
		LoginWidget->PrepareForDisplay(Username);
	}
}

void AMT2GatewayHUD::HandleSessionStateChanged(
	EMT2ClientSessionState State, const FString& Error)
{
	if (!LoadingScreenWidget)
	{
		return;
	}
	if (State == EMT2ClientSessionState::Authenticating)
	{
		LoadingScreenWidget->SetLoadingText(TEXT("Authenticating..."));
		LoadingScreenWidget->SetVisibility(ESlateVisibility::Visible);
	}
	else if (State == EMT2ClientSessionState::TravelingToMap)
	{
		LoadingScreenWidget->SetLoadingText(TEXT("Entering the world..."));
		LoadingScreenWidget->SetVisibility(ESlateVisibility::Visible);
	}
	else
	{
		LoadingScreenWidget->SetVisibility(ESlateVisibility::Collapsed);
		if (State == EMT2ClientSessionState::Failed && !Error.IsEmpty())
		{
			SetOnlyVisible(LoginWidget);
			SetBackgroundMusic(LoginMusic);
			if (LoginWidget) LoginWidget->ShowError(Error);
		}
	}
}

void AMT2GatewayHUD::SetBackgroundMusic(const TSoftObjectPtr<USoundBase>& Music)
{
	USoundBase* RequestedMusic = Music.LoadSynchronous();
	if (ActiveMusic == RequestedMusic && MusicComponent && MusicComponent->IsPlaying()) return;

	if (!MusicComponent)
	{
		MusicComponent = NewObject<UAudioComponent>(this, TEXT("GatewayMusic"));
		MusicComponent->bAutoActivate = false;
		MusicComponent->bAllowSpatialization = false;
		MusicComponent->bIsUISound = true;
		MusicComponent->RegisterComponent();
	}

	MusicComponent->OnAudioFinished.RemoveDynamic(
		this, &AMT2GatewayHUD::RestartBackgroundMusic);
	MusicComponent->Stop();
	ActiveMusic = RequestedMusic;
	MusicComponent->SetSound(ActiveMusic);
	MusicComponent->SetVolumeMultiplier(FMT2AudioUserSettings::GetMusicVolume());
	if (ActiveMusic)
	{
		MusicComponent->OnAudioFinished.AddUniqueDynamic(
			this, &AMT2GatewayHUD::RestartBackgroundMusic);
		MusicComponent->Play();
	}
}

void AMT2GatewayHUD::RestartBackgroundMusic()
{
	if (ActiveMusic && MusicComponent)
	{
		MusicComponent->Play();
	}
}

void AMT2GatewayHUD::SetOnlyVisible(UUserWidget* Widget)
{
	UUserWidget* AllWidgets[] = {
		static_cast<UUserWidget*>(LoginWidget),
		static_cast<UUserWidget*>(RegistrationWidget),
		static_cast<UUserWidget*>(CharacterSelectWidget),
		static_cast<UUserWidget*>(CharacterCreateWidget)};
	for (UUserWidget* Each : AllWidgets)
	{
		if (Each)
		{
			Each->SetVisibility(Each == Widget ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		}
	}
}
