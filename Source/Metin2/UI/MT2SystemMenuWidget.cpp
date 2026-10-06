/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2SystemMenuWidget.h"
#include "Config/MT2PathSettings.h"

#include "Audio/MT2AudioUserSettings.h"
#include "AudioCaptureCore.h"
#include "Blueprint/WidgetTree.h"
#include "Authentication/MT2ClientSessionSubsystem.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/IConsoleManager.h"
#include "Input/MT2KeyBindingSubsystem.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Localization/MT2LocalizationSubsystem.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"
#include "Player/MT2PlayerController.h"
#include "Player/MT2PlayerState.h"
#include "UI/MT2UIStyle.h"
#include "Voice/MT2VoiceChatClientSubsystem.h"

namespace
{
	const TCHAR* VideoConfigSection = TEXT("MT2Video");
	const TCHAR* MotionBlurKey = TEXT("MotionBlurEnabled");
	// Gateway the client first connected to; used when the session subsystem never captured one
	// (e.g. direct-to-map PIE testing).
	const TCHAR* FallbackGatewayAddress = TEXT("127.0.0.1:11000");
	const TCHAR* DefaultDeviceLabel = TEXT("Default");
	const TCHAR* UnboundKeyLabel = TEXT("---");
}

void UMT2SystemMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);

	if (AudioButton) AudioButton->OnClicked.AddUniqueDynamic(this, &UMT2SystemMenuWidget::HandleAudioClicked);
	if (ControlsButton) ControlsButton->OnClicked.AddUniqueDynamic(this, &UMT2SystemMenuWidget::HandleControlsClicked);
	if (SettingsButton) SettingsButton->OnClicked.AddUniqueDynamic(this, &UMT2SystemMenuWidget::HandleSettingsClicked);
	if (DisconnectButton) DisconnectButton->OnClicked.AddUniqueDynamic(this, &UMT2SystemMenuWidget::HandleDisconnectClicked);
	if (ExitButton) ExitButton->OnClicked.AddUniqueDynamic(this, &UMT2SystemMenuWidget::HandleExitClicked);
	if (CancelButton) CancelButton->OnClicked.AddUniqueDynamic(this, &UMT2SystemMenuWidget::HandleCancelClicked);
	if (AudioBackButton) AudioBackButton->OnClicked.AddUniqueDynamic(this, &UMT2SystemMenuWidget::HandleBackClicked);
	if (ControlsBackButton) ControlsBackButton->OnClicked.AddUniqueDynamic(this, &UMT2SystemMenuWidget::HandleBackClicked);
	if (SettingsBackButton) SettingsBackButton->OnClicked.AddUniqueDynamic(this, &UMT2SystemMenuWidget::HandleBackClicked);
	if (ResetAllKeysButton) ResetAllKeysButton->OnClicked.AddUniqueDynamic(this, &UMT2SystemMenuWidget::HandleResetAllKeysClicked);

	if (GeneralVolumeSlider)
	{
		GeneralVolumeSlider->OnValueChanged.AddUniqueDynamic(this, &UMT2SystemMenuWidget::HandleGeneralVolumeChanged);
		RefreshVolumeRow(GeneralVolumeSlider, GeneralVolumeValueText, FMT2AudioUserSettings::GetGeneralVolume());
	}
	if (MusicVolumeSlider)
	{
		MusicVolumeSlider->OnValueChanged.AddUniqueDynamic(this, &UMT2SystemMenuWidget::HandleMusicVolumeChanged);
		RefreshVolumeRow(MusicVolumeSlider, MusicVolumeValueText, FMT2AudioUserSettings::GetMusicVolume());
	}
	if (VoiceVolumeSlider)
	{
		VoiceVolumeSlider->OnValueChanged.AddUniqueDynamic(this, &UMT2SystemMenuWidget::HandleVoiceVolumeChanged);
		RefreshVolumeRow(VoiceVolumeSlider, VoiceVolumeValueText, FMT2AudioUserSettings::GetVoiceVolume());
	}
	if (VoiceToggleModeCheckBox)
	{
		VoiceToggleModeCheckBox->SetIsChecked(FMT2AudioUserSettings::GetVoiceToggleMode());
		VoiceToggleModeCheckBox->OnCheckStateChanged.AddUniqueDynamic(
			this, &UMT2SystemMenuWidget::HandleVoiceToggleModeChanged);
	}
	if (InputDeviceCombo)
	{
		InputDeviceCombo->OnSelectionChanged.AddUniqueDynamic(this, &UMT2SystemMenuWidget::HandleInputDeviceSelected);
	}
	if (OutputDeviceCombo)
	{
		OutputDeviceCombo->OnSelectionChanged.AddUniqueDynamic(this, &UMT2SystemMenuWidget::HandleOutputDeviceSelected);
	}

	if (WindowedButton) WindowedButton->OnClicked.AddUniqueDynamic(this, &UMT2SystemMenuWidget::HandleWindowedClicked);
	if (FullscreenButton) FullscreenButton->OnClicked.AddUniqueDynamic(this, &UMT2SystemMenuWidget::HandleFullscreenClicked);
	if (ShadowQualityCombo) ShadowQualityCombo->OnSelectionChanged.AddUniqueDynamic(this, &UMT2SystemMenuWidget::HandleShadowQualitySelected);
	if (TextureQualityCombo) TextureQualityCombo->OnSelectionChanged.AddUniqueDynamic(this, &UMT2SystemMenuWidget::HandleTextureQualitySelected);
	if (EffectsQualityCombo) EffectsQualityCombo->OnSelectionChanged.AddUniqueDynamic(this, &UMT2SystemMenuWidget::HandleEffectsQualitySelected);
	if (AntiAliasingQualityCombo) AntiAliasingQualityCombo->OnSelectionChanged.AddUniqueDynamic(this, &UMT2SystemMenuWidget::HandleAntiAliasingQualitySelected);
	if (FoliageQualityCombo) FoliageQualityCombo->OnSelectionChanged.AddUniqueDynamic(this, &UMT2SystemMenuWidget::HandleFoliageQualitySelected);
	if (MotionBlurCombo) MotionBlurCombo->OnSelectionChanged.AddUniqueDynamic(this, &UMT2SystemMenuWidget::HandleMotionBlurSelected);
	if (FrameRateLimitCombo) FrameRateLimitCombo->OnSelectionChanged.AddUniqueDynamic(this, &UMT2SystemMenuWidget::HandleFrameRateLimitSelected);
	if (LanguageCombo) LanguageCombo->OnSelectionChanged.AddUniqueDynamic(this, &UMT2SystemMenuWidget::HandleLanguageSelected);
	if (VSyncCheckBox) VSyncCheckBox->OnCheckStateChanged.AddUniqueDynamic(this, &UMT2SystemMenuWidget::HandleVSyncChanged);
	if (AggressiveModeButton) AggressiveModeButton->OnClicked.AddUniqueDynamic(
		this, &UMT2SystemMenuWidget::HandleAggressiveModeClicked);

	if (UMT2KeyBindingSubsystem* Bindings = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMT2KeyBindingSubsystem>() : nullptr)
	{
		Bindings->OnBindingsChanged.AddWeakLambda(this, [this] { RebuildControlsRows(); });
	}

	PopulateAudioDeviceLists();
	PopulateSettingsPage();
	PopulateLanguageList();
	BindAggressiveModeState();
	RebuildControlsRows();
	ShowPage(MainPanel);
}

void UMT2SystemMenuWidget::NativeDestruct()
{
	if (AMT2PlayerState* State = BoundPlayerState.Get())
	{
		State->OnAggressiveModeChanged.RemoveDynamic(
			this, &UMT2SystemMenuWidget::HandleAggressiveModeChanged);
	}
	BoundPlayerState.Reset();
	Super::NativeDestruct();
}

void UMT2SystemMenuWidget::ToggleMenu()
{
	if (IsMenuOpen()) CloseMenu(); else OpenMenu();
}

void UMT2SystemMenuWidget::OpenMenu()
{
	// Like the old dialog, opening always lands on the main button list, not a stale submenu.
	PendingRebindActionId = NAME_None;
	ShowPage(MainPanel);
	BindAggressiveModeState();
	SetVisibility(ESlateVisibility::Visible);
}

void UMT2SystemMenuWidget::CloseMenu()
{
	PendingRebindActionId = NAME_None;
	SetVisibility(ESlateVisibility::Collapsed);
}

void UMT2SystemMenuWidget::ShowPage(UCanvasPanel* Page)
{
	UCanvasPanel* AllPages[] = {MainPanel.Get(), AudioPanel.Get(), ControlsPanel.Get(), SettingsPanel.Get()};
	for (UCanvasPanel* Each : AllPages)
	{
		if (Each)
		{
			Each->SetVisibility(Each == Page ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		}
	}
}

void UMT2SystemMenuWidget::HandleAudioClicked()
{
	PopulateAudioDeviceLists();
	ShowPage(AudioPanel);
}

void UMT2SystemMenuWidget::HandleControlsClicked()
{
	RebuildControlsRows();
	ShowPage(ControlsPanel);
}

void UMT2SystemMenuWidget::HandleSettingsClicked()
{
	BindAggressiveModeState();
	PopulateSettingsPage();
	PopulateLanguageList();
	ShowPage(SettingsPanel);
}

void UMT2SystemMenuWidget::BindAggressiveModeState()
{
	AMT2PlayerState* State = GetOwningPlayer()
		? GetOwningPlayer()->GetPlayerState<AMT2PlayerState>() : nullptr;
	if (BoundPlayerState.Get() != State)
	{
		if (AMT2PlayerState* Previous = BoundPlayerState.Get())
		{
			Previous->OnAggressiveModeChanged.RemoveDynamic(
				this, &UMT2SystemMenuWidget::HandleAggressiveModeChanged);
		}
		BoundPlayerState = State;
		if (State)
		{
			State->OnAggressiveModeChanged.AddUniqueDynamic(
				this, &UMT2SystemMenuWidget::HandleAggressiveModeChanged);
		}
	}
	RefreshAggressiveModeButton();
}

void UMT2SystemMenuWidget::RefreshAggressiveModeButton()
{
	const AMT2PlayerState* State = BoundPlayerState.Get();
	const bool bAggressive = State && State->IsAggressiveMode();
	if (AggressiveModeButton)
	{
		AggressiveModeButton->SetIsEnabled(State != nullptr);
	}
	if (AggressiveModeText)
	{
		AggressiveModeText->SetText(bAggressive
			? NSLOCTEXT("MT2SystemMenu", "PvPAggressive", "PvP Mode: Aggressive")
			: NSLOCTEXT("MT2SystemMenu", "PvPNeutral", "PvP Mode: Neutral"));
		AggressiveModeText->SetColorAndOpacity(FSlateColor(bAggressive
			? FLinearColor(1.0f, 0.24f, 0.12f) : FLinearColor(0.9f, 0.86f, 0.72f)));
	}
}

void UMT2SystemMenuWidget::HandleAggressiveModeClicked()
{
	if (AMT2PlayerState* State = BoundPlayerState.Get())
	{
		State->ServerSetAggressiveMode(!State->IsAggressiveMode());
	}
}

void UMT2SystemMenuWidget::HandleAggressiveModeChanged(bool bEnabled)
{
	RefreshAggressiveModeButton();
}

void UMT2SystemMenuWidget::HandleBackClicked()
{
	PendingRebindActionId = NAME_None;
	ShowPage(MainPanel);
}

void UMT2SystemMenuWidget::HandleCancelClicked() { CloseMenu(); }

void UMT2SystemMenuWidget::HandleDisconnectClicked()
{
	// Old logout_button: net.LogOutGame() -> back to the login screen. Traveling back to the
	// gateway drops the map-server connection and lands on the gateway map's login flow.
	CloseMenu();
	AMT2PlayerController* Controller = Cast<AMT2PlayerController>(GetOwningPlayer());
	if (!Controller)
	{
		return;
	}
	FString GatewayAddress = FallbackGatewayAddress;
	if (const UMT2ClientSessionSubsystem* Session = Controller->GetGameInstance()
		? Controller->GetGameInstance()->GetSubsystem<UMT2ClientSessionSubsystem>() : nullptr)
	{
		if (!Session->GetGatewayAddress().IsEmpty())
		{
			GatewayAddress = Session->GetGatewayAddress();
		}
	}
	Controller->ConnectToServer(GatewayAddress);
}

void UMT2SystemMenuWidget::HandleExitClicked()
{
	// Old exit_button: net.ExitApplication().
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

// ---------------------------------------------------------------------------- Audio page

void UMT2SystemMenuWidget::RefreshVolumeRow(USlider* Slider, UTextBlock* ValueText, float Value)
{
	if (Slider)
	{
		Slider->SetValue(Value);
	}
	if (ValueText)
	{
		ValueText->SetText(FText::FromString(
			FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Value * 100.0f))));
	}
}

void UMT2SystemMenuWidget::HandleGeneralVolumeChanged(float Value)
{
	FMT2AudioUserSettings::SetGeneralVolume(Value);
	RefreshVolumeRow(nullptr, GeneralVolumeValueText, Value);
}

void UMT2SystemMenuWidget::HandleMusicVolumeChanged(float Value)
{
	FMT2AudioUserSettings::SetMusicVolume(Value);
	RefreshVolumeRow(nullptr, MusicVolumeValueText, Value);
}

void UMT2SystemMenuWidget::HandleVoiceVolumeChanged(float Value)
{
	FMT2AudioUserSettings::SetVoiceVolume(Value);
	RefreshVolumeRow(nullptr, VoiceVolumeValueText, Value);
}

void UMT2SystemMenuWidget::HandleVoiceToggleModeChanged(bool bIsChecked)
{
	FMT2AudioUserSettings::SetVoiceToggleMode(bIsChecked);
	// A mode switch must never leave a previously latched/captured stream transmitting.
	if (UMT2VoiceChatClientSubsystem* Voice =
		GetWorld() ? GetWorld()->GetSubsystem<UMT2VoiceChatClientSubsystem>() : nullptr)
	{
		Voice->StopTalking();
	}
}

void UMT2SystemMenuWidget::PopulateAudioDeviceLists()
{
	// Microphones for the proximity voice chat. Enumerated through AudioCaptureCore ("AudioCapture"
	// loads the platform backend); the Voice module matches its capture device by name.
	if (InputDeviceCombo)
	{
		InputDeviceCombo->ClearOptions();
		InputDeviceCombo->AddOption(DefaultDeviceLabel);
		FModuleManager::Get().LoadModulePtr<IModuleInterface>(TEXT("AudioCapture"));
		Audio::FAudioCapture CaptureEnumerator;
		TArray<Audio::FCaptureDeviceInfo> CaptureDevices;
		CaptureEnumerator.GetCaptureDevicesAvailable(CaptureDevices);
		for (const Audio::FCaptureDeviceInfo& Device : CaptureDevices)
		{
			if (!Device.DeviceName.IsEmpty())
			{
				InputDeviceCombo->AddOption(Device.DeviceName);
			}
		}
		const FString SavedInput = FMT2AudioUserSettings::GetVoiceInputDevice();
		InputDeviceCombo->SetSelectedOption(SavedInput.IsEmpty() ? DefaultDeviceLabel : SavedInput);
	}

	// Output devices arrive asynchronously from the audio mixer.
	if (OutputDeviceCombo)
	{
		FOnAudioOutputDevicesObtained DevicesObtained;
		DevicesObtained.BindDynamic(this, &UMT2SystemMenuWidget::HandleOutputDevicesObtained);
		UAudioMixerBlueprintLibrary::GetAvailableAudioOutputDevices(this, DevicesObtained);
	}
}

void UMT2SystemMenuWidget::HandleOutputDevicesObtained(const TArray<FAudioOutputDeviceInfo>& AvailableDevices)
{
	if (!OutputDeviceCombo)
	{
		return;
	}
	OutputDeviceNameToId.Reset();
	OutputDeviceCombo->ClearOptions();
	OutputDeviceCombo->AddOption(DefaultDeviceLabel);
	const FString SavedOutput = FMT2AudioUserSettings::GetOutputDeviceId();
	FString SelectedName = DefaultDeviceLabel;
	for (const FAudioOutputDeviceInfo& Device : AvailableDevices)
	{
		if (Device.Name.IsEmpty() || Device.DeviceId.IsEmpty())
		{
			continue;
		}
		OutputDeviceNameToId.Add(Device.Name, Device.DeviceId);
		OutputDeviceCombo->AddOption(Device.Name);
		if (!SavedOutput.IsEmpty() && Device.DeviceId == SavedOutput)
		{
			SelectedName = Device.Name;
		}
	}
	OutputDeviceCombo->SetSelectedOption(SelectedName);
}

void UMT2SystemMenuWidget::HandleInputDeviceSelected(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (SelectionType == ESelectInfo::Direct)
	{
		return;
	}
	FMT2AudioUserSettings::SetVoiceInputDevice(
		SelectedItem == DefaultDeviceLabel ? FString() : SelectedItem);
	if (UMT2VoiceChatClientSubsystem* Voice =
		GetWorld() ? GetWorld()->GetSubsystem<UMT2VoiceChatClientSubsystem>() : nullptr)
	{
		Voice->ResetCaptureDevice();
	}
}

void UMT2SystemMenuWidget::HandleOutputDeviceSelected(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (SelectionType == ESelectInfo::Direct)
	{
		return;
	}
	FString DeviceId;
	if (const FString* Found = OutputDeviceNameToId.Find(SelectedItem))
	{
		DeviceId = *Found;
	}
	FMT2AudioUserSettings::SetOutputDeviceId(DeviceId);
	if (!DeviceId.IsEmpty())
	{
		UAudioMixerBlueprintLibrary::SwapAudioOutputDevice(this, DeviceId, FOnCompletedDeviceSwap());
	}
}

// ---------------------------------------------------------------------------- Settings page

int32 UMT2SystemMenuWidget::QualityLevelFromName(const FString& Name)
{
	if (Name == TEXT("Low")) return 0;
	if (Name == TEXT("Medium")) return 1;
	if (Name == TEXT("High")) return 2;
	return 3;
}

FString UMT2SystemMenuWidget::QualityNameFromLevel(int32 Level)
{
	switch (FMath::Clamp(Level, 0, 3))
	{
	case 0: return TEXT("Low");
	case 1: return TEXT("Medium");
	case 2: return TEXT("High");
	default: return TEXT("Epic");
	}
}

void UMT2SystemMenuWidget::PopulateSettingsPage()
{
	const UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	auto Populate = [Settings](UComboBoxString* Combo, int32 CurrentLevel)
	{
		if (!Combo)
		{
			return;
		}
		Combo->ClearOptions();
		Combo->AddOption(TEXT("Low"));
		Combo->AddOption(TEXT("Medium"));
		Combo->AddOption(TEXT("High"));
		Combo->AddOption(TEXT("Epic"));
		Combo->SetSelectedOption(QualityNameFromLevel(CurrentLevel));
	};
	Populate(ShadowQualityCombo, Settings ? Settings->GetShadowQuality() : 3);
	Populate(TextureQualityCombo, Settings ? Settings->GetTextureQuality() : 3);
	Populate(EffectsQualityCombo, Settings ? Settings->GetVisualEffectQuality() : 3);
	Populate(AntiAliasingQualityCombo, Settings ? Settings->GetAntiAliasingQuality() : 3);
	Populate(FoliageQualityCombo, Settings ? Settings->GetFoliageQuality() : 3);

	if (MotionBlurCombo)
	{
		bool bMotionBlur = true;
		if (GConfig)
		{
			GConfig->GetBool(VideoConfigSection, MotionBlurKey, bMotionBlur, GGameUserSettingsIni);
		}
		MotionBlurCombo->ClearOptions();
		MotionBlurCombo->AddOption(TEXT("Off"));
		MotionBlurCombo->AddOption(TEXT("On"));
		MotionBlurCombo->SetSelectedOption(bMotionBlur ? TEXT("On") : TEXT("Off"));
	}

	if (FrameRateLimitCombo)
	{
		FrameRateLimitCombo->ClearOptions();
		FrameRateLimitCombo->AddOption(TEXT("30"));
		FrameRateLimitCombo->AddOption(TEXT("60"));
		FrameRateLimitCombo->AddOption(TEXT("90"));
		FrameRateLimitCombo->AddOption(TEXT("120"));
		FrameRateLimitCombo->AddOption(TEXT("OFF"));

		const float CurrentLimit = Settings ? Settings->GetFrameRateLimit() : 120.0f;
		if (CurrentLimit <= 0.0f)
		{
			FrameRateLimitCombo->SetSelectedOption(TEXT("OFF"));
		}
		else
		{
			const int32 AvailableLimits[] = {30, 60, 90, 120};
			int32 ClosestLimit = AvailableLimits[0];
			for (const int32 Limit : AvailableLimits)
			{
				if (FMath::Abs(CurrentLimit - Limit) < FMath::Abs(CurrentLimit - ClosestLimit))
				{
					ClosestLimit = Limit;
				}
			}
			FrameRateLimitCombo->SetSelectedOption(FString::FromInt(ClosestLimit));
		}
	}

	if (VSyncCheckBox)
	{
		VSyncCheckBox->SetIsChecked(Settings ? Settings->IsVSyncEnabled() : true);
	}
}

void UMT2SystemMenuWidget::PopulateLanguageList()
{
	if (!LanguageCombo)
	{
		return;
	}
	UMT2LocalizationSubsystem* Localization = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMT2LocalizationSubsystem>() : nullptr;
	if (!Localization)
	{
		return;
	}

	LanguageCombo->ClearOptions();
	LanguageDisplayNameToCulture.Reset();
	FString CurrentDisplayName;
	const FString CurrentCulture = Localization->GetCurrentCulture();
	for (const FMT2LanguageOption& Language : Localization->GetSupportedLanguages())
	{
		FString DisplayName = Language.DisplayName.ToString();
		if (LanguageDisplayNameToCulture.Contains(DisplayName))
		{
			DisplayName += FString::Printf(TEXT(" (%s)"), *Language.CultureCode);
		}
		LanguageCombo->AddOption(DisplayName);
		LanguageDisplayNameToCulture.Add(DisplayName, Language.CultureCode);
		if (Language.CultureCode.Equals(CurrentCulture, ESearchCase::IgnoreCase))
		{
			CurrentDisplayName = DisplayName;
		}
	}
	if (!CurrentDisplayName.IsEmpty())
	{
		LanguageCombo->SetSelectedOption(CurrentDisplayName);
	}
}

void UMT2SystemMenuWidget::HandleLanguageSelected(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (SelectionType == ESelectInfo::Direct)
	{
		return;
	}
	const FString* CultureCode = LanguageDisplayNameToCulture.Find(SelectedItem);
	UMT2LocalizationSubsystem* Localization = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMT2LocalizationSubsystem>() : nullptr;
	if (CultureCode && Localization && Localization->SetCurrentCulture(*CultureCode))
	{
		PopulateSettingsPage();
		PopulateLanguageList();
	}
}

namespace
{
	void ApplyQuality(void (UGameUserSettings::*Setter)(int32), int32 Level)
	{
		if (UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr)
		{
			(Settings->*Setter)(Level);
			Settings->ApplySettings(false);
			Settings->SaveSettings();
		}
	}
}

void UMT2SystemMenuWidget::HandleShadowQualitySelected(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (SelectionType != ESelectInfo::Direct)
		ApplyQuality(&UGameUserSettings::SetShadowQuality, QualityLevelFromName(SelectedItem));
}

void UMT2SystemMenuWidget::HandleTextureQualitySelected(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (SelectionType != ESelectInfo::Direct)
		ApplyQuality(&UGameUserSettings::SetTextureQuality, QualityLevelFromName(SelectedItem));
}

void UMT2SystemMenuWidget::HandleEffectsQualitySelected(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (SelectionType != ESelectInfo::Direct)
		ApplyQuality(&UGameUserSettings::SetVisualEffectQuality, QualityLevelFromName(SelectedItem));
}

void UMT2SystemMenuWidget::HandleAntiAliasingQualitySelected(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (SelectionType != ESelectInfo::Direct)
		ApplyQuality(&UGameUserSettings::SetAntiAliasingQuality, QualityLevelFromName(SelectedItem));
}

void UMT2SystemMenuWidget::HandleFoliageQualitySelected(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (SelectionType != ESelectInfo::Direct)
		ApplyQuality(&UGameUserSettings::SetFoliageQuality, QualityLevelFromName(SelectedItem));
}

void UMT2SystemMenuWidget::ApplyMotionBlur(bool bEnabled)
{
	if (IConsoleVariable* MotionBlurQuality =
		IConsoleManager::Get().FindConsoleVariable(TEXT("r.MotionBlurQuality")))
	{
		MotionBlurQuality->Set(bEnabled ? 4 : 0, ECVF_SetByGameSetting);
	}
}

void UMT2SystemMenuWidget::HandleMotionBlurSelected(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (SelectionType == ESelectInfo::Direct)
	{
		return;
	}
	const bool bEnabled = SelectedItem == TEXT("On");
	if (GConfig)
	{
		GConfig->SetBool(VideoConfigSection, MotionBlurKey, bEnabled, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	ApplyMotionBlur(bEnabled);
}

void UMT2SystemMenuWidget::HandleFrameRateLimitSelected(
	FString SelectedItem,
	ESelectInfo::Type SelectionType)
{
	if (SelectionType == ESelectInfo::Direct)
	{
		return;
	}
	if (UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		const float FrameRateLimit = SelectedItem == TEXT("OFF")
			? 0.0f : static_cast<float>(FCString::Atoi(*SelectedItem));
		Settings->SetFrameRateLimit(FrameRateLimit);
		Settings->ApplySettings(false);
		Settings->SaveSettings();
	}
}

void UMT2SystemMenuWidget::HandleVSyncChanged(bool bIsChecked)
{
	if (UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		Settings->SetVSyncEnabled(bIsChecked);
		Settings->ApplySettings(false);
		Settings->SaveSettings();
	}
}

void UMT2SystemMenuWidget::SetWindowMode(bool bFullscreen)
{
	if (UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		Settings->SetFullscreenMode(
			bFullscreen ? EWindowMode::WindowedFullscreen : EWindowMode::Windowed);
		Settings->ApplySettings(false);
		Settings->SaveSettings();
	}
}

void UMT2SystemMenuWidget::HandleWindowedClicked() { SetWindowMode(false); }
void UMT2SystemMenuWidget::HandleFullscreenClicked() { SetWindowMode(true); }

// ---------------------------------------------------------------------------- Controls page

void UMT2SystemMenuWidget::RebuildControlsRows()
{
	UMT2KeyBindingSubsystem* Bindings = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMT2KeyBindingSubsystem>() : nullptr;
	if (!ControlsList || !Bindings || !WidgetTree)
	{
		return;
	}
	ControlsList->ClearChildren();
	KeyButtonLabels.Reset();

	UTexture2D* Public = FMT2UIStyle::LoadTexture(UMT2PathSettings::Path(TEXT("UI_PublicAtlas")));
	const FMT2AtlasRegion MiddleNormal(194, 142, 255, 163);
	const FMT2AtlasRegion MiddleHovered(88, 181, 149, 202);
	const FMT2AtlasRegion MiddlePressed(149, 181, 210, 202);

	for (const FMT2KeyBindingAction& Action : Bindings->GetActions())
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();

		UTextBlock* NameText = FMT2UIStyle::Label(*WidgetTree,
			FText::FromString(Action.DisplayName), 8, ETextJustify::Left);
		UHorizontalBoxSlot* NameSlot = Row->AddChildToHorizontalBox(NameText);
		NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		NameSlot->SetVerticalAlignment(VAlign_Center);
		NameSlot->SetPadding(FMargin(2.0f, 3.0f, 4.0f, 3.0f));

		// Current key: click, then press the new key ("Press key..." while listening).
		const FName ActionId = Action.Id;
		UMT2KeyBindingRowButton* KeyButton = WidgetTree->ConstructWidget<UMT2KeyBindingRowButton>();
		FButtonStyle KeyStyle = KeyButton->GetStyle();
		KeyStyle.SetNormal(FMT2UIStyle::AtlasBrush(Public, MiddleNormal));
		KeyStyle.SetHovered(FMT2UIStyle::AtlasBrush(Public, MiddleHovered));
		KeyStyle.SetPressed(FMT2UIStyle::AtlasBrush(Public, MiddlePressed));
		KeyButton->SetStyle(KeyStyle);
		KeyButton->ActionId = ActionId;
		KeyButton->BindActionClick();
		KeyButton->OnActionClicked.BindUObject(this, &UMT2SystemMenuWidget::BeginKeyCapture);
		UTextBlock* KeyLabel = FMT2UIStyle::Label(*WidgetTree, FText::FromString(
			Action.CurrentKey.IsValid() ? Action.CurrentKey.GetDisplayName().ToString() : UnboundKeyLabel), 8);
		KeyButton->AddChild(KeyLabel);
		if (UButtonSlot* LabelSlot = Cast<UButtonSlot>(KeyLabel->Slot))
		{
			LabelSlot->SetPadding(FMargin(2.0f));
		}
		UHorizontalBoxSlot* KeySlot = Row->AddChildToHorizontalBox(KeyButton);
		KeySlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		KeySlot->SetVerticalAlignment(VAlign_Center);
		KeyButtonLabels.Add(ActionId, KeyLabel);

		// Per-row reset to the action's default key.
		UMT2KeyBindingRowButton* ResetButton = WidgetTree->ConstructWidget<UMT2KeyBindingRowButton>();
		FButtonStyle ResetStyle = ResetButton->GetStyle();
		ResetStyle.SetNormal(FMT2UIStyle::AtlasBrush(Public, MiddleNormal));
		ResetStyle.SetHovered(FMT2UIStyle::AtlasBrush(Public, MiddleHovered));
		ResetStyle.SetPressed(FMT2UIStyle::AtlasBrush(Public, MiddlePressed));
		ResetButton->SetStyle(ResetStyle);
		ResetButton->ActionId = ActionId;
		ResetButton->BindActionClick();
		ResetButton->OnActionClicked.BindUObject(this, &UMT2SystemMenuWidget::ResetActionKey);
		UTextBlock* ResetLabel = FMT2UIStyle::Label(*WidgetTree, FText::FromString(TEXT("R")), 8);
		ResetButton->AddChild(ResetLabel);
		if (UButtonSlot* ResetLabelSlot = Cast<UButtonSlot>(ResetLabel->Slot))
		{
			ResetLabelSlot->SetPadding(FMargin(2.0f));
		}
		UHorizontalBoxSlot* ResetSlot = Row->AddChildToHorizontalBox(ResetButton);
		ResetSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		ResetSlot->SetVerticalAlignment(VAlign_Center);
		ResetSlot->SetPadding(FMargin(3.0f, 0.0f, 0.0f, 0.0f));

		ControlsList->AddChild(Row);
	}
}

void UMT2SystemMenuWidget::ResetActionKey(FName ActionId)
{
	PendingRebindActionId = NAME_None;
	if (UMT2KeyBindingSubsystem* Bindings = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMT2KeyBindingSubsystem>() : nullptr)
	{
		Bindings->ResetKey(ActionId);
	}
}

FReply UMT2SystemMenuWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape && IsMenuOpen())
	{
		PendingRebindActionId = NAME_None;
		CloseMenu();
		return FReply::Handled();
	}

	if (!PendingRebindActionId.IsNone())
	{
		const FKey Key = InKeyEvent.GetKey();
		UMT2KeyBindingSubsystem* Bindings = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UMT2KeyBindingSubsystem>() : nullptr;
		if (Bindings)
		{
			Bindings->SetKey(PendingRebindActionId, Key);
		}
		PendingRebindActionId = NAME_None;
		RebuildControlsRows();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

void UMT2SystemMenuWidget::BeginKeyCapture(FName ActionId)
{
	PendingRebindActionId = ActionId;
	if (const TObjectPtr<UTextBlock>* Label = KeyButtonLabels.Find(ActionId))
	{
		if (*Label)
		{
			(*Label)->SetText(FText::FromString(TEXT("Press key...")));
		}
	}
	SetKeyboardFocus();
}

void UMT2SystemMenuWidget::HandleResetAllKeysClicked()
{
	PendingRebindActionId = NAME_None;
	if (UMT2KeyBindingSubsystem* Bindings = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMT2KeyBindingSubsystem>() : nullptr)
	{
		Bindings->ResetAllKeys();
	}
}

// ---------------------------------------------------------------------------- Startup apply

void UMT2SystemMenuWidget::ApplySavedAudioVideoSettings(const UObject* WorldContext)
{
	FMT2AudioUserSettings::ApplyGeneralVolume();

	bool bMotionBlur = true;
	if (GConfig)
	{
		GConfig->GetBool(VideoConfigSection, MotionBlurKey, bMotionBlur, GGameUserSettingsIni);
	}
	ApplyMotionBlur(bMotionBlur);

	const FString OutputDeviceId = FMT2AudioUserSettings::GetOutputDeviceId();
	if (!OutputDeviceId.IsEmpty() && WorldContext)
	{
		UAudioMixerBlueprintLibrary::SwapAudioOutputDevice(
			WorldContext, OutputDeviceId, FOnCompletedDeviceSwap());
	}
}
