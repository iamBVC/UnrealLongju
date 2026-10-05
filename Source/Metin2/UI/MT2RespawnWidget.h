/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "UI/MT2UserWidget.h"
#include "CoreMinimal.h"
#include "MT2RespawnWidget.generated.h"

class UButton;

// WidgetTree is authored onto the /Game/UI/MT2Respawn Widget Blueprint by
// MT2GenerateUIBlueprintsCommandlet (same pattern as the taskbar/inventory/HUD widgets), replicating
// the original client's uiscript/restartdialog.py 1:1: a thinboard-framed panel with the XLarge_Button
// atlas art from Public.dds. This class only binds behavior to the widgets that Blueprint declares.
UCLASS()
class METIN2_API UMT2RespawnWidget : public UMT2UserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;

private:
	UFUNCTION()
	void HandleRespawnHereClicked();

	UFUNCTION()
	void HandleRespawnTownClicked();

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> RespawnHereButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> RespawnTownButton;
};
