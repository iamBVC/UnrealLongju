/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Audio/MT2AudioUserSettings.h"

#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "Engine/Engine.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
	const TCHAR* AudioConfigSection = TEXT("MT2Audio");
}

FSimpleMulticastDelegate FMT2AudioUserSettings::OnChanged;

float FMT2AudioUserSettings::LoadFloat(const TCHAR* Key, float Default)
{
	float Value = Default;
	if (GConfig)
	{
		GConfig->GetFloat(AudioConfigSection, Key, Value, GGameUserSettingsIni);
	}
	return FMath::Clamp(Value, 0.0f, 1.0f);
}

void FMT2AudioUserSettings::SaveFloat(const TCHAR* Key, float Value)
{
	if (GConfig)
	{
		GConfig->SetFloat(AudioConfigSection, Key, FMath::Clamp(Value, 0.0f, 1.0f), GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

FString FMT2AudioUserSettings::LoadString(const TCHAR* Key)
{
	FString Value;
	if (GConfig)
	{
		GConfig->GetString(AudioConfigSection, Key, Value, GGameUserSettingsIni);
	}
	return Value;
}

void FMT2AudioUserSettings::SaveString(const TCHAR* Key, const FString& Value)
{
	if (GConfig)
	{
		GConfig->SetString(AudioConfigSection, Key, *Value, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

float FMT2AudioUserSettings::GetGeneralVolume() { return LoadFloat(TEXT("MasterVolume"), 1.0f); }
float FMT2AudioUserSettings::GetMusicVolume() { return LoadFloat(TEXT("MusicVolume"), 1.0f); }
float FMT2AudioUserSettings::GetVoiceVolume() { return LoadFloat(TEXT("VoiceVolume"), 1.0f); }
bool FMT2AudioUserSettings::GetVoiceToggleMode()
{
	bool bEnabled = false;
	if (GConfig)
	{
		GConfig->GetBool(AudioConfigSection, TEXT("VoiceToggleMode"), bEnabled, GGameUserSettingsIni);
	}
	return bEnabled;
}
FString FMT2AudioUserSettings::GetVoiceInputDevice() { return LoadString(TEXT("VoiceInputDevice")); }
FString FMT2AudioUserSettings::GetOutputDeviceId() { return LoadString(TEXT("OutputDeviceId")); }

void FMT2AudioUserSettings::SetGeneralVolume(float Value)
{
	SaveFloat(TEXT("MasterVolume"), Value);
	ApplyGeneralVolume();
	OnChanged.Broadcast();
}

void FMT2AudioUserSettings::SetMusicVolume(float Value)
{
	SaveFloat(TEXT("MusicVolume"), Value);
	OnChanged.Broadcast();
}

void FMT2AudioUserSettings::SetVoiceVolume(float Value)
{
	SaveFloat(TEXT("VoiceVolume"), Value);
	OnChanged.Broadcast();
}

void FMT2AudioUserSettings::SetVoiceToggleMode(bool bEnabled)
{
	if (GConfig)
	{
		GConfig->SetBool(AudioConfigSection, TEXT("VoiceToggleMode"), bEnabled, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	OnChanged.Broadcast();
}

void FMT2AudioUserSettings::SetVoiceInputDevice(const FString& DeviceName)
{
	SaveString(TEXT("VoiceInputDevice"), DeviceName);
	OnChanged.Broadcast();
}

void FMT2AudioUserSettings::SetOutputDeviceId(const FString& DeviceId)
{
	SaveString(TEXT("OutputDeviceId"), DeviceId);
	OnChanged.Broadcast();
}

void FMT2AudioUserSettings::ApplyGeneralVolume()
{
	// FApp's multiplier is owned by application focus handling and is reset to 1.0 whenever the
	// window regains focus. The audio-device primary volume is the runtime master bus and therefore
	// affects UI, music, effects, and voice consistently in PIE and packaged clients.
	if (FAudioDeviceManager* DeviceManager = GEngine ? GEngine->GetAudioDeviceManager() : nullptr)
	{
		const float Volume = GetGeneralVolume();
		// PIE can own a separate audio device from GEngine's main device (and multiple PIE worlds can
		// each own one). Apply the user setting to every live device so the slider affects the actual
		// world being heard as well as standalone/packaged clients.
		DeviceManager->IterateOverAllDevices(
			[Volume](Audio::FDeviceId, FAudioDevice* AudioDevice)
			{
				if (AudioDevice)
				{
					AudioDevice->SetTransientPrimaryVolume(Volume);
				}
			});
	}
}
