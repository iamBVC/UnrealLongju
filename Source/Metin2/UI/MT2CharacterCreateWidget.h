/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "UI/MT2UserWidget.h"
#include "CoreMinimal.h"
#include "Player/MT2PlayerTypes.h"
#include "MT2CharacterCreateWidget.generated.h"

class UButton;
class UEditableTextBox;
class UImage;
class UMaterialInstanceDynamic;
class USizeBox;
class UTextBlock;
class AMT2CharacterPreviewActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMT2CharacterCreateFlowSignature);

// Character-creation step of the gateway flow (old client: introEmpire + createcharacterwindow.py).
// Binds to the /Game/UI/MT2CharacterCreate Widget Blueprint, which must contain: NameBox
// (EditableTextBox), WarriorButton/AssassinButton/SuraButton/ShamanButton, SexButton,
// EmpireRedButton/EmpireYellowButton/EmpireBlueButton, CreateButton, BackButton (Buttons),
// SelectionText + StatusText (TextBlocks).
UCLASS()
class METIN2_API UMT2CharacterCreateWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	// Fired when creation succeeded or the player backs out - the gateway HUD returns to select.
	UPROPERTY(BlueprintAssignable, Category = "Character Create")
	FMT2CharacterCreateFlowSignature OnFinished;

	UFUNCTION(BlueprintCallable, Category = "Character Create")
	void PrepareForDisplay();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	UFUNCTION() void HandleWarriorClicked();
	UFUNCTION() void HandleAssassinClicked();
	UFUNCTION() void HandleSuraClicked();
	UFUNCTION() void HandleShamanClicked();
	UFUNCTION() void HandleSexClicked();
	UFUNCTION() void HandleStyleClicked();
	UFUNCTION() void HandleEmpireRedClicked();
	UFUNCTION() void HandleEmpireYellowClicked();
	UFUNCTION() void HandleEmpireBlueClicked();
	UFUNCTION() void HandleCreateClicked();
	UFUNCTION() void HandleBackClicked();

	UFUNCTION()
	void HandleCreationCompleted(bool bSucceeded, const FString& Error);

	void RefreshSelectionText();
	void RefreshCharacterPreview();
	void EnsureCharacterPreview();
	void HandleVisibilityChanged(ESlateVisibility NewVisibility);
	void SetStatus(const FString& Message);
	class AMT2PlayerController* GetMT2Controller() const;

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UEditableTextBox> NameBox;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> WarriorButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> AssassinButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> SuraButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> ShamanButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> SexButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> StyleButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> EmpireRedButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> EmpireYellowButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> EmpireBlueButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> CreateButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> BackButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> SelectionText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> StatusText;
	// UE 5.7 marks UViewport experimental and removes it from the Widget Blueprint palette. Add a
	// normal SizeBox with this name; C++ creates the transparent preview image as its runtime content.
	UPROPERTY(meta = (BindWidget)) TObjectPtr<USizeBox> CharacterPreviewHost;
	UPROPERTY(Transient)
	TObjectPtr<UImage> CharacterPreviewImage;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CharacterPreviewMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "Character Create|Preview")
	FIntPoint PreviewRenderResolution = FIntPoint(1024, 1536);

	FMT2CharacterAppearance PendingAppearance;
	EMT2Empire PendingEmpire = EMT2Empire::Shinsoo;
	TWeakObjectPtr<AMT2CharacterPreviewActor> CharacterPreviewActor;
};
