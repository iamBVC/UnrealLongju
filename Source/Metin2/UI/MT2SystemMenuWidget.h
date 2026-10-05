/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "UI/MT2UserWidget.h"
#include "CoreMinimal.h"
#include "AudioMixerBlueprintLibrary.h"
#include "Components/Button.h"
#include "MT2SystemMenuWidget.generated.h"

class UCanvasPanel;
class UCheckBox;
class UComboBoxString;
class UScrollBox;
class USlider;
class UTextBlock;
class AMT2PlayerState;

// A button that knows which key-binding action its row represents; UButton's OnClicked carries
// no payload, so the runtime-built controls rows need this to route clicks back per action.
UCLASS()
class METIN2_API UMT2KeyBindingRowButton : public UButton
{
	GENERATED_BODY()

public:
	FName ActionId;
	DECLARE_DELEGATE_OneParam(FMT2ActionClickedSignature, FName);
	FMT2ActionClickedSignature OnActionClicked;

	void BindActionClick()
	{
		OnClicked.AddUniqueDynamic(this, &UMT2KeyBindingRowButton::HandleClicked);
	}

private:
	UFUNCTION()
	void HandleClicked() { OnActionClicked.ExecuteIfBound(ActionId); }
};

// The old client's ESC menu (uisystem.py SystemDialog / uiscript/systemdialog.py): a centered
// 200-wide thinboard with a vertical stack of XLarge buttons. Layout lives in the generated
// /Game/UI/MT2SystemMenu Widget Blueprint (MT2GenerateUIBlueprintsCommandlet -SystemMenuOnly);
// this class only binds behavior. Pages: Audio (general/music/voice sliders + capture/output
// device pickers), Controls (rebindable action keys), Settings (window mode + per-category
// scalability), replacing the old uiSystemOption/uiGameOption dialogs.
UCLASS()
class METIN2_API UMT2SystemMenuWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	void ToggleMenu();
	void OpenMenu();
	void CloseMenu();
	bool IsMenuOpen() const { return GetVisibility() != ESlateVisibility::Collapsed; }

	// Applies everything persisted by this menu that the engine does not restore by itself:
	// general volume, saved audio output device and the motion blur toggle. Call once per map.
	static void ApplySavedAudioVideoSettings(const UObject* WorldContext);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	UFUNCTION() void HandleAudioClicked();
	UFUNCTION() void HandleControlsClicked();
	UFUNCTION() void HandleSettingsClicked();
	UFUNCTION() void HandleDisconnectClicked();
	UFUNCTION() void HandleExitClicked();
	UFUNCTION() void HandleCancelClicked();
	UFUNCTION() void HandleBackClicked();

	UFUNCTION() void HandleGeneralVolumeChanged(float Value);
	UFUNCTION() void HandleMusicVolumeChanged(float Value);
	UFUNCTION() void HandleVoiceVolumeChanged(float Value);
	UFUNCTION() void HandleVoiceToggleModeChanged(bool bIsChecked);
	UFUNCTION() void HandleInputDeviceSelected(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleOutputDeviceSelected(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleOutputDevicesObtained(const TArray<FAudioOutputDeviceInfo>& AvailableDevices);

	UFUNCTION() void HandleWindowedClicked();
	UFUNCTION() void HandleFullscreenClicked();
	UFUNCTION() void HandleShadowQualitySelected(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleTextureQualitySelected(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleEffectsQualitySelected(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleAntiAliasingQualitySelected(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleFoliageQualitySelected(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleMotionBlurSelected(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleFrameRateLimitSelected(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleLanguageSelected(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION() void HandleVSyncChanged(bool bIsChecked);
	UFUNCTION() void HandleAggressiveModeClicked();
	UFUNCTION() void HandleAggressiveModeChanged(bool bEnabled);
	UFUNCTION() void HandleResetAllKeysClicked();

	void ShowPage(UCanvasPanel* Page);
	void SetWindowMode(bool bFullscreen);
	void RefreshVolumeRow(USlider* Slider, UTextBlock* ValueText, float Value);
	void PopulateAudioDeviceLists();
	void PopulateSettingsPage();
	void PopulateLanguageList();
	void BindAggressiveModeState();
	void RefreshAggressiveModeButton();
	void RebuildControlsRows();
	void BeginKeyCapture(FName ActionId);
	void ResetActionKey(FName ActionId);
	static int32 QualityLevelFromName(const FString& Name);
	static FString QualityNameFromLevel(int32 Level);
	static void ApplyMotionBlur(bool bEnabled);

	// Rebind state: while set, the next key press is captured for this action.
	FName PendingRebindActionId;

	// Output combo display name -> mixer device id.
	TMap<FString, FString> OutputDeviceNameToId;

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UCanvasPanel> MainPanel;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UCanvasPanel> AudioPanel;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UCanvasPanel> ControlsPanel;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UCanvasPanel> SettingsPanel;

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> AudioButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> ControlsButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> SettingsButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> DisconnectButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> ExitButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> CancelButton;

	UPROPERTY(meta = (BindWidget)) TObjectPtr<USlider> GeneralVolumeSlider;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> GeneralVolumeValueText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<USlider> MusicVolumeSlider;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> MusicVolumeValueText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<USlider> VoiceVolumeSlider;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> VoiceVolumeValueText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UCheckBox> VoiceToggleModeCheckBox;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UComboBoxString> InputDeviceCombo;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UComboBoxString> OutputDeviceCombo;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> AudioBackButton;

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UScrollBox> ControlsList;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> ResetAllKeysButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> ControlsBackButton;

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> WindowedButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> FullscreenButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UComboBoxString> ShadowQualityCombo;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UComboBoxString> TextureQualityCombo;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UComboBoxString> EffectsQualityCombo;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UComboBoxString> AntiAliasingQualityCombo;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UComboBoxString> FoliageQualityCombo;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UComboBoxString> MotionBlurCombo;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UComboBoxString> FrameRateLimitCombo;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UComboBoxString> LanguageCombo;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UCheckBox> VSyncCheckBox;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> AggressiveModeButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> AggressiveModeText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> SettingsBackButton;

	TWeakObjectPtr<AMT2PlayerState> BoundPlayerState;
	TMap<FString, FString> LanguageDisplayNameToCulture;

	// Runtime-built controls rows: per-action key button label, for refresh after rebinds.
	UPROPERTY(Transient) TMap<FName, TObjectPtr<UTextBlock>> KeyButtonLabels;
};
