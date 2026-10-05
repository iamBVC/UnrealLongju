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
#include "MT2ChatWidget.generated.h"

class UEditableTextBox;
class UButton;
class UCanvasPanel;
class UScrollBox;
class UTextBlock;
class UVerticalBox;
class UBorder;
class UWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMT2ChatCommandSignature, const FString&, Command);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMT2ChatMessageSignature, const FString&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMT2ChatClosedSignature);

// WidgetTree is authored onto the /Game/UI/MT2Chat Widget Blueprint by MT2GenerateUIBlueprintsCommandlet
// (same pattern as the taskbar/inventory/respawn widgets). This class only binds behavior; the input box
// and history scroll box come from that Blueprint via BindWidget.
UCLASS()
class METIN2_API UMT2ChatWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	void OpenForInput();
	void FocusInput();
	bool IsInputOpen() const { return bIsOpen; }
	void AddLine(const FString& Line);
	void AddRewardLine(const FString& Line);
	void AddGlobalLine(
		EMT2Empire Empire, const FString& SenderName, const FString& Message, bool bWorldBroadcast);

	// Slate widget the player controller should focus when opening chat, so keystrokes land in the text
	// box instead of driving character movement. Null until the widget has been constructed.
	UWidget* GetInputBoxWidget() const;

	UPROPERTY(BlueprintAssignable, Category = "Chat")
	FMT2ChatCommandSignature OnCommandSubmitted;

	UPROPERTY(BlueprintAssignable, Category = "Chat")
	FMT2ChatMessageSignature OnMessageSubmitted;

	// Fires whenever the input box closes, whether or not a message was actually sent (Enter with text,
	// Enter with no text, or focus lost) - the controller uses this to restore normal gameplay input.
	UPROPERTY(BlueprintAssignable, Category = "Chat")
	FMT2ChatClosedSignature OnChatClosed;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	void CloseInput();
	void KeepHistoryVisible();
	void SubmitCurrentText();
	void RecordSubmittedText(const FString& Text);
	void NavigateSubmitHistory(int32 Direction);

	// Command autocomplete: while the first token starts with '/', a popup above the input lists
	// matching commands with usage hints. Up/Down move the selection, Tab completes it.
	void EnsureSuggestionsPanel();
	void RefreshSuggestions();
	void NavigateSuggestions(int32 Direction);
	void ApplySelectedSuggestion();
	void HideSuggestions();
	bool AreSuggestionsVisible() const;

	UFUNCTION()
	void HandleTextChanged(const FText& Text);

	UFUNCTION()
	void HandleTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void HandleSendClicked();

	UFUNCTION()
	void HandleFocusInputClicked();

	UFUNCTION()
	void HandleHistoryClicked();

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCanvasPanel> HistoryPanel;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCanvasPanel> InputControls;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UEditableTextBox> InputBox;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UScrollBox> HistoryBox;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> ModeButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> SendButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> WhisperButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> HistoryButton;

	bool bIsOpen = false;
	bool bHistoryPinned = false;

	// Previously submitted inputs, browsable with Up/Down while the text box is focused (oldest
	// first). Consecutive duplicates are stored once so spamming a command doesn't flood the list.
	TArray<FString> SubmitHistory;
	// INDEX_NONE = not browsing (typing a fresh line); otherwise the SubmitHistory entry shown.
	int32 SubmitHistoryCursor = INDEX_NONE;
	// What the player had typed before browsing began; restored when stepping past the newest entry.
	FString PendingDraft;
	static constexpr int32 MaxSubmitHistory = 50;

	UPROPERTY(Transient) TObjectPtr<UBorder> SuggestionsPanel;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> SuggestionsBox;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SuggestionTexts;
	// Command names currently listed (parallel to SuggestionTexts' visible rows).
	TArray<FString> CurrentSuggestions;
	int32 SuggestionCursor = 0;

	// The history stays fully visible until this world time, then fades to transparent over
	// HistoryFadeDuration - matching the old client where chat lines linger a few seconds then fade.
	double HistoryVisibleUntil = 0.0;
	static constexpr float HistoryHoldTime = 5.0f;
	static constexpr float HistoryFadeDuration = 1.0f;

	// Authored in MT2Chat so its position, size and styling remain fully editable in UMG.
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UScrollBox> RewardHistoryBox;
	double RewardVisibleUntil = 0.0;
};
