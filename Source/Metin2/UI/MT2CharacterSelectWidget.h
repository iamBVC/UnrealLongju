/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "UI/MT2UserWidget.h"
#include "CoreMinimal.h"
#include "Server/MT2ServerRuntimeTypes.h"
#include "MT2CharacterSelectWidget.generated.h"

class UButton;
class UImage;
class UMaterialInstanceDynamic;
class USizeBox;
class UTextBlock;
class AMT2CharacterPreviewActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMT2CharacterSelectFlowSignature);

// Character-selection step of the gateway flow (old client: selectcharacterwindow.py, 4 slots).
// Binds to the /Game/UI/MT2CharacterSelect Widget Blueprint, which must contain: SlotButton1..4
// (Button), SlotName1..4 (TextBlock), StartButton + CreateButton (Button), StatusText (TextBlock).
UCLASS()
class METIN2_API UMT2CharacterSelectWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	// Fired when the player asks to create a new character - the gateway HUD swaps to the create widget.
	UPROPERTY(BlueprintAssignable, Category = "Character Select")
	FMT2CharacterSelectFlowSignature OnCreateRequested;

	// Refreshes the four slot rows from the session's cached character list.
	UFUNCTION(BlueprintCallable, Category = "Character Select")
	void RefreshCharacterList();

	UFUNCTION(BlueprintCallable, Category = "Character Select")
	void ShowError(const FString& Error);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	UFUNCTION() void HandleSlotOneClicked();
	UFUNCTION() void HandleSlotTwoClicked();
	UFUNCTION() void HandleSlotThreeClicked();
	UFUNCTION() void HandleSlotFourClicked();
	UFUNCTION() void HandleStartClicked();
	UFUNCTION() void HandleCreateClicked();

	UFUNCTION()
	void HandleCharacterListUpdated(const TArray<FMT2CharacterSummary>& Characters);

	UFUNCTION()
	void HandleTravelFailed(bool bSucceeded, const FString& Error);

	void SelectSlot(int32 SlotIndex);
	void EnsureCharacterPreview();
	void RefreshCharacterPreview();
	void HandleVisibilityChanged(ESlateVisibility NewVisibility);
	void SetStatus(const FString& Message);
	class AMT2PlayerController* GetMT2Controller() const;

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> SlotButton1;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> SlotButton2;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> SlotButton3;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> SlotButton4;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> SlotName1;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> SlotName2;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> SlotName3;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> SlotName4;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> StartButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> CreateButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> StatusText;
	// Designer-owned layout anchor. Add a SizeBox with this exact name to MT2CharacterSelect.
	UPROPERTY(meta = (BindWidget)) TObjectPtr<USizeBox> CharacterPreviewHost;

	UPROPERTY(Transient)
	TObjectPtr<UImage> CharacterPreviewImage;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CharacterPreviewMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "Character Select|Preview")
	FIntPoint PreviewRenderResolution = FIntPoint(1024, 1536);

	UPROPERTY(Transient)
	TArray<FMT2CharacterSummary> CachedCharacters;

	int32 SelectedSlot = INDEX_NONE;
	TWeakObjectPtr<AMT2CharacterPreviewActor> CharacterPreviewActor;
};
