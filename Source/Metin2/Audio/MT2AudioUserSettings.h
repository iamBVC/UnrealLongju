/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"

// Client-side audio preferences persisted in GameUserSettings.ini ([MT2Audio]). Volumes are
// 0..1: General drives the app-wide volume multiplier, Music is a multiplier the BGM audio
// components apply on top, Voice scales the proximity-voice playback components. Device strings
// select the microphone used for voice capture and the OS output device the audio mixer renders
// to. Setters save immediately and broadcast OnChanged so live components can re-apply.
class METIN2_API FMT2AudioUserSettings
{
public:
	static float GetGeneralVolume();
	static float GetMusicVolume();
	static float GetVoiceVolume();
	static bool GetVoiceToggleMode();
	static FString GetVoiceInputDevice();  // empty = system default microphone
	static FString GetOutputDeviceId();    // empty = system default output

	static void SetGeneralVolume(float Value);
	static void SetMusicVolume(float Value);
	static void SetVoiceVolume(float Value);
	static void SetVoiceToggleMode(bool bEnabled);
	static void SetVoiceInputDevice(const FString& DeviceName);
	static void SetOutputDeviceId(const FString& DeviceId);

	// Applies the saved general volume to the app multiplier (call once per map start).
	static void ApplyGeneralVolume();

	static FSimpleMulticastDelegate OnChanged;

private:
	static float LoadFloat(const TCHAR* Key, float Default);
	static void SaveFloat(const TCHAR* Key, float Value);
	static FString LoadString(const TCHAR* Key);
	static void SaveString(const TCHAR* Key, const FString& Value);
};
