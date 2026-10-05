/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "MT2UserWidget.generated.h"

class USoundBase;
class UWidget;

UCLASS(Abstract)
class METIN2_API UMT2UserWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UMT2UserWidget(const FObjectInitializer& ObjectInitializer);

	// Gives every button a click sound and every text field a typing sound. Runs automatically on
	// construction; call it again after building widgets at runtime, since those are not in the tree
	// yet when the widget is first constructed (quest dialog options, quest log rows, shop lists).
	UFUNCTION(BlueprintCallable, Category = "UI|Sound")
	void RefreshWidgetSounds();

	// Same for a single widget, for callers that know exactly what they just created.
	void ApplyWidgetSound(UWidget* Widget);

protected:
	virtual void NativeConstruct() override;

	// Used only when a button has no custom Pressed Sound configured in its Widget Blueprint.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Sound")
	TSoftObjectPtr<USoundBase> DefaultButtonClickSound;

	// The old client played this on every IME update, i.e. once per keystroke (ui.py OnIMEUpdate).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Sound")
	TSoftObjectPtr<USoundBase> TypingSound;

private:
	// One handler for all three editable text widget types; UMG's delegates differ only in whether
	// they carry a commit reason.
	UFUNCTION()
	void HandleTypingSound(const FText& Text);
};
