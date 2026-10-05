/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Messenger/MT2MessengerTypes.h"
#include "UI/MT2UserWidget.h"
#include "MT2WhisperWidget.generated.h"

class UButton;
class UEditableTextBox;
class UMT2MessengerComponent;
class UMultiLineEditableTextBox;
class UScrollBox;
class USizeBox;
class UTextBlock;
class UWidget;

// The old WhisperDialog (uiscript/whisperdialog.py): a small 280x200 floating board with the
// companion's name in a slot at the top left, a GM mark, minimize and close buttons at the top right,
// the conversation in the middle, and an edit bar along the bottom with the send button.
//
// It is its own window, separate from the messenger list - clicking "Message" on a player opens this
// directly, without going near the friend list. Which conversation it shows comes from the messenger
// component, so the two windows never disagree about what is open.
UCLASS()
class METIN2_API UMT2WhisperWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	// Shows the window on the conversation the messenger component currently has open.
	UFUNCTION(BlueprintCallable, Category = "Messenger")
	void RefreshWhisper();

	UFUNCTION(BlueprintCallable, Category = "Messenger")
	void CloseWhisper();

	// Exact default size from uiscript/whisperdialog.py. This is deliberately compact: a whisper is
	// a focused conversation window, not a second global-chat panel.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Messenger|Layout")
	FVector2D WindowSize = FVector2D(280.0, 200.0);

	// The old dialog coloured its own lines and the companion's differently in the chat log.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Messenger|Style")
	FLinearColor CompanionLineColor = FLinearColor(0.9f, 0.9f, 0.85f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Messenger|Style")
	FLinearColor OwnLineColor = FLinearColor(0.65f, 0.82f, 1.0f);

	// A stored message that waited for the player to come back is worth marking, since the old game
	// could not do it at all.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Messenger|Style")
	FLinearColor OfflineNoticeColor = FLinearColor(1.0f, 0.7f, 0.5f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Messenger|Style")
	int32 LineFontSize = 8;

	// The band at the top of the window that drags it, matching the old dialog's movable title bar.
	// Anything below it belongs to the log and the buttons.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Messenger|Layout")
	float TitleBarHeight = 34.0f;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	// window["style"] included "movable": the old dialog could be dragged around by its title bar.
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	// ---- bound from the Widget Blueprint ----
	UPROPERTY(meta = (BindWidget)) TObjectPtr<USizeBox> RootSizeBox;
	// whisperdialog.py "name_slot"/"titlename": the companion this window is talking to.
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> TitleNameText;
	// "gamemastermark", shown only when the companion is a GM.
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UWidget> GameMasterMark;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UScrollBox> ChatLogBox;
	// "chatline": multi_line editline in the old script.
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMultiLineEditableTextBox> ChatLine;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> SendButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> CloseButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> MinimizeButton;

private:
	UFUNCTION() void HandleSendClicked();
	UFUNCTION() void HandleCloseClicked();
	UFUNCTION() void HandleMinimizeClicked();
	UFUNCTION() void HandleChatLineChanged(const FText& Text);
	UFUNCTION() void HandleMessengerChanged();

	UMT2MessengerComponent* ResolveMessenger() const;
	void BindMessenger();
	void BuildChatLog();

	// Minimize keeps the window but collapses everything except the title row, as the old dialog did.
	UPROPERTY(Transient) bool bMinimized = false;

	// Where the window sits in the viewport. Held here because a widget added to the viewport has no
	// canvas slot to read a position back from.
	UPROPERTY(Transient) FVector2D WindowPosition = FVector2D::ZeroVector;
	UPROPERTY(Transient) bool bPositionInitialized = false;
	UPROPERTY(Transient) bool bDragging = false;
	FVector2D DragStartMouse = FVector2D::ZeroVector;
	FVector2D DragStartWindow = FVector2D::ZeroVector;

	UPROPERTY(Transient) TWeakObjectPtr<UMT2MessengerComponent> BoundMessenger;
};
