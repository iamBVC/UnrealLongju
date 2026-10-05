/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "MT2GatewayHUD.generated.h"

class UMT2CharacterCreateWidget;
class UMT2CharacterSelectWidget;
class UMT2AccountRegistrationWidget;
class UMT2LoadingScreenWidget;
class UMT2LoginWidget;
class UAudioComponent;
class USoundBase;
enum class EMT2ClientSessionState : uint8;

// HUD for the Gateway (login) map: shows Login -> Character Select -> Character Create and back,
// driving the existing gateway RPCs on AMT2PlayerController. Widget layouts are user-authored
// Blueprints at /Game/UI/MT2Login, /Game/UI/MT2CharacterSelect and /Game/UI/MT2CharacterCreate,
// each deriving from the matching C++ widget class (see those classes for the required
// BindWidget names). Selecting a character triggers the gateway travel into the world map.
UCLASS(Config = Game, DefaultConfig)
class METIN2_API AMT2GatewayHUD : public AHUD
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void ShowCharacterSelect();

	UFUNCTION()
	void ShowCharacterCreate();

	UFUNCTION()
	void ShowRegistration();

	UFUNCTION()
	void ShowLogin();

	UFUNCTION()
	void HandleRegistrationSucceeded(const FString& Username);

	UFUNCTION()
	void HandleSessionStateChanged(EMT2ClientSessionState State, const FString& Error);

	template <typename WidgetType>
	WidgetType* CreateGatewayWidget(const TCHAR* AssetName);

	void SetOnlyVisible(UUserWidget* Widget);
	void SetBackgroundMusic(const TSoftObjectPtr<USoundBase>& Music);

	UFUNCTION()
	void RestartBackgroundMusic();

	UPROPERTY(Config, EditDefaultsOnly, Category = "Gateway|Audio")
	TSoftObjectPtr<USoundBase> LoginMusic;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Gateway|Audio")
	TSoftObjectPtr<USoundBase> CharacterSelectionMusic;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> MusicComponent;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> ActiveMusic;

	UPROPERTY(Transient)
	TObjectPtr<UMT2LoginWidget> LoginWidget;

	UPROPERTY(Transient)
	TObjectPtr<UMT2AccountRegistrationWidget> RegistrationWidget;

	UPROPERTY(Transient)
	TObjectPtr<UMT2CharacterSelectWidget> CharacterSelectWidget;

	UPROPERTY(Transient)
	TObjectPtr<UMT2CharacterCreateWidget> CharacterCreateWidget;

	UPROPERTY(Transient)
	TObjectPtr<UMT2LoadingScreenWidget> LoadingScreenWidget;
};
