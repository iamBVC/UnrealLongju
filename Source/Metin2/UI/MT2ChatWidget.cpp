/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2ChatWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "UI/MT2EmpireFlagImage.h"

namespace
{
	struct FMT2ChatCommandInfo
	{
		const TCHAR* Command;
		const TCHAR* Usage;
		const TCHAR* Description;
	};

	// Admin chat commands understood by ServerExecuteChatCommand. Keep in sync when adding one.
	const FMT2ChatCommandInfo ChatCommands[] = {
		{TEXT("/a"), TEXT("/a <applyType> <value> [seconds]"), TEXT("Apply a status effect to yourself")},
		{TEXT("/i"), TEXT("/i <vnum> [count]"), TEXT("Add an item to your inventory")},
		{TEXT("/itemlist"), TEXT("/itemlist [page]"), TEXT("List registered item VNUMs and names")},
		{TEXT("/m"), TEXT("/m <vnum> [count]"), TEXT("Spawn mobs at your position")},
		{TEXT("/moblist"), TEXT("/moblist [page]"), TEXT("List registered mob VNUMs and names")},
		{TEXT("/money"), TEXT("/money <amount>"), TEXT("Grant yang")},
		{TEXT("/netprofiler"), TEXT("/netprofiler <client|server> <0|1>"), TEXT("Toggle network capture")},
		{TEXT("/race"), TEXT("/race <warrior|assassin|sura|shaman>"), TEXT("Change race and reset race equipment and skills")},
		{TEXT("/gender"), TEXT("/gender <male|female>"), TEXT("Change character gender")},
		{TEXT("/style"), TEXT("/style <red|blue>"), TEXT("Change character style")},
		{TEXT("/setskill"), TEXT("/setskill <vnum> <level>"), TEXT("Set a skill's level (0-40)")},
		{TEXT("/setskillgroup"), TEXT("/setskillgroup <0|1|2>"), TEXT("Set your skill group (0 clears)")},
		{TEXT("/stats"), TEXT("/stats"), TEXT("Print all calculated stats and active bonuses")},
		{TEXT("/xp"), TEXT("/xp <amount>"), TEXT("Grant experience")},
	};
}

void UMT2ChatWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
	if (InputBox)
	{
		InputBox->OnTextCommitted.AddUniqueDynamic(this, &UMT2ChatWidget::HandleTextCommitted);
		InputBox->OnTextChanged.AddUniqueDynamic(this, &UMT2ChatWidget::HandleTextChanged);
	}
	if (SendButton) SendButton->OnClicked.AddUniqueDynamic(this, &UMT2ChatWidget::HandleSendClicked);
	if (ModeButton) ModeButton->OnClicked.AddUniqueDynamic(this, &UMT2ChatWidget::HandleFocusInputClicked);
	if (WhisperButton) WhisperButton->OnClicked.AddUniqueDynamic(this, &UMT2ChatWidget::HandleFocusInputClicked);
	if (HistoryButton) HistoryButton->OnClicked.AddUniqueDynamic(this, &UMT2ChatWidget::HandleHistoryClicked);
	if (InputControls) InputControls->SetVisibility(ESlateVisibility::Collapsed);
}

void UMT2ChatWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!HistoryPanel)
	{
		return;
	}

	// While typing, the history stays fully visible; otherwise it holds for HistoryHoldTime after the
	// last message then fades out, so old chatter doesn't clutter the screen.
	float TargetOpacity = 1.0f;
	if (!bIsOpen && !bHistoryPinned)
	{
		const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		const double Remaining = HistoryVisibleUntil - Now;
		if (Remaining <= 0.0)
		{
			TargetOpacity = 0.0f;
		}
		else if (Remaining < HistoryFadeDuration)
		{
			TargetOpacity = static_cast<float>(Remaining / HistoryFadeDuration);
		}
	}
	HistoryPanel->SetRenderOpacity(TargetOpacity);

	if (RewardHistoryBox)
	{
		const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		const double Remaining = RewardVisibleUntil - Now;
		const float RewardOpacity = Remaining <= 0.0
			? 0.0f
			: (Remaining < HistoryFadeDuration
				? static_cast<float>(Remaining / HistoryFadeDuration) : 1.0f);
		RewardHistoryBox->SetRenderOpacity(RewardOpacity);
	}
}

UWidget* UMT2ChatWidget::GetInputBoxWidget() const
{
	return InputBox;
}

void UMT2ChatWidget::OpenForInput()
{
	bIsOpen = true;
	SubmitHistoryCursor = INDEX_NONE;
	PendingDraft.Reset();
	KeepHistoryVisible();
	if (InputControls)
	{
		InputControls->SetVisibility(ESlateVisibility::Visible);
	}
	if (InputBox)
	{
		InputBox->SetText(FText::GetEmpty());
	}
	FocusInput();
}

void UMT2ChatWidget::FocusInput()
{
	if (bIsOpen && InputBox)
	{
		InputBox->SetFocus();
		if (APlayerController* OwningPlayer = GetOwningPlayer())
		{
			InputBox->SetUserFocus(OwningPlayer);
		}
		InputBox->SetKeyboardFocus();
	}
}

void UMT2ChatWidget::CloseInput()
{
	bIsOpen = false;
	HideSuggestions();
	KeepHistoryVisible();
	if (InputControls)
	{
		InputControls->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (InputBox)
	{
		InputBox->SetText(FText::GetEmpty());
	}
}

void UMT2ChatWidget::KeepHistoryVisible()
{
	HistoryVisibleUntil = (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0) + HistoryHoldTime;
}

void UMT2ChatWidget::AddLine(const FString& Line)
{
	if (!HistoryBox || !WidgetTree)
	{
		return;
	}
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Text->SetText(FText::FromString(Line));
	Text->SetAutoWrapText(true);
	FSlateFontInfo Font = Text->GetFont();
	Font.Size = 10;
	Text->SetFont(Font);
	Text->SetColorAndOpacity(FSlateColor(FLinearColor(0.94f, 0.93f, 0.82f, 1.0f)));
	Text->SetShadowOffset(FVector2D(1.0f));
	Text->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.8f));
	HistoryBox->AddChild(Text);
	HistoryBox->ScrollToEnd();
	KeepHistoryVisible();
}

void UMT2ChatWidget::AddRewardLine(const FString& Line)
{
	if (!RewardHistoryBox || !WidgetTree)
	{
		return;
	}
	while (RewardHistoryBox->GetChildrenCount() >= 8)
	{
		RewardHistoryBox->RemoveChildAt(0);
	}
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Text->SetText(FText::FromString(Line));
	Text->SetAutoWrapText(true);
	FSlateFontInfo Font = Text->GetFont();
	Font.Size = 10;
	Text->SetFont(Font);
	Text->SetColorAndOpacity(FSlateColor(FLinearColor(0.92f, 0.78f, 0.25f, 1.0f)));
	Text->SetShadowOffset(FVector2D(1.0f));
	Text->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.8f));
	RewardHistoryBox->AddChild(Text);
	RewardHistoryBox->ScrollToEnd();
	RewardHistoryBox->SetRenderOpacity(1.0f);
	RewardVisibleUntil = (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0) + HistoryHoldTime;
}

void UMT2ChatWidget::AddGlobalLine(
	EMT2Empire Empire, const FString& SenderName, const FString& Message, bool bWorldBroadcast)
{
	if (!HistoryBox || !WidgetTree)
	{
		return;
	}

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass());
	UMT2EmpireFlagImage* Flag = WidgetTree->ConstructWidget<UMT2EmpireFlagImage>(
		UMT2EmpireFlagImage::StaticClass());
	Flag->SetEmpire(Empire);
	if (UHorizontalBoxSlot* FlagSlot = Row->AddChildToHorizontalBox(Flag))
	{
		FlagSlot->SetPadding(FMargin(0.0f, 1.0f, 3.0f, 0.0f));
		FlagSlot->SetVerticalAlignment(VAlign_Center);
	}

	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Text->SetText(FText::FromString(FString::Printf(TEXT("[%s] : %s"), *SenderName, *Message)));
	Text->SetAutoWrapText(true);
	FSlateFontInfo Font = Text->GetFont();
	Font.Size = 10;
	Text->SetFont(Font);
	const FLinearColor TextColor = bWorldBroadcast
		? FLinearColor::FromSRGBColor(FColor(0x60, 0xC0, 0x60))
		: FLinearColor(0.94f, 0.93f, 0.82f, 1.0f);
	Text->SetColorAndOpacity(FSlateColor(TextColor));
	Text->SetShadowOffset(FVector2D(1.0f));
	Text->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.8f));
	if (UHorizontalBoxSlot* TextSlot = Row->AddChildToHorizontalBox(Text))
	{
		TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TextSlot->SetVerticalAlignment(VAlign_Center);
	}

	HistoryBox->AddChild(Row);
	HistoryBox->ScrollToEnd();
	KeepHistoryVisible();
}

void UMT2ChatWidget::SubmitCurrentText()
{
	const FString Message = InputBox ? InputBox->GetText().ToString().TrimStartAndEnd() : FString();
	CloseInput();
	OnChatClosed.Broadcast();
	if (Message.IsEmpty())
	{
		return;
	}
	if (Message.StartsWith(TEXT("/")))
	{
		RecordSubmittedText(Message);
		OnCommandSubmitted.Broadcast(Message);
	}
	else
	{
		OnMessageSubmitted.Broadcast(Message);
	}
}

void UMT2ChatWidget::RecordSubmittedText(const FString& Text)
{
	// Skip consecutive duplicates so spamming the same command keeps a single history entry.
	if (SubmitHistory.Num() > 0 && SubmitHistory.Last() == Text)
	{
		return;
	}
	SubmitHistory.Add(Text);
	if (SubmitHistory.Num() > MaxSubmitHistory)
	{
		SubmitHistory.RemoveAt(0);
	}
}

void UMT2ChatWidget::NavigateSubmitHistory(int32 Direction)
{
	if (!InputBox || SubmitHistory.IsEmpty())
	{
		return;
	}

	if (SubmitHistoryCursor == INDEX_NONE)
	{
		if (Direction > 0)
		{
			return;
		}
		// Entering browse mode: stash whatever was being typed so Down can bring it back.
		PendingDraft = InputBox->GetText().ToString();
		SubmitHistoryCursor = SubmitHistory.Num() - 1;
	}
	else if (Direction < 0)
	{
		SubmitHistoryCursor = FMath::Max(SubmitHistoryCursor - 1, 0);
	}
	else if (++SubmitHistoryCursor >= SubmitHistory.Num())
	{
		// Stepped past the newest entry: leave browse mode and restore the unfinished draft.
		SubmitHistoryCursor = INDEX_NONE;
		InputBox->SetText(FText::FromString(PendingDraft));
		return;
	}
	InputBox->SetText(FText::FromString(SubmitHistory[SubmitHistoryCursor]));
}

void UMT2ChatWidget::EnsureSuggestionsPanel()
{
	if (SuggestionsPanel || !InputBox || !WidgetTree)
	{
		return;
	}
	// The popup lives in the same canvas as the input box, anchored just above it.
	UCanvasPanelSlot* InputSlot = Cast<UCanvasPanelSlot>(InputBox->Slot);
	UCanvasPanel* ParentCanvas = InputSlot ? Cast<UCanvasPanel>(InputBox->GetParent()) : nullptr;
	if (!ParentCanvas)
	{
		return;
	}

	SuggestionsPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CommandSuggestions"));
	SuggestionsPanel->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f));
	SuggestionsPanel->SetPadding(FMargin(6.0f, 4.0f));
	SuggestionsPanel->SetVisibility(ESlateVisibility::Collapsed);
	SuggestionsBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CommandSuggestionRows"));
	SuggestionsPanel->SetContent(SuggestionsBox);

	for (const FMT2ChatCommandInfo& Info : ChatCommands)
	{
		UTextBlock* Row = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		FSlateFontInfo Font = Row->GetFont();
		Font.Size = 9;
		Row->SetFont(Font);
		Row->SetShadowOffset(FVector2D(1.0));
		Row->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.8f));
		SuggestionsBox->AddChildToVerticalBox(Row);
		SuggestionTexts.Add(Row);
	}

	UCanvasPanelSlot* PanelSlot = ParentCanvas->AddChildToCanvas(SuggestionsPanel);
	PanelSlot->SetAutoSize(true);
	const FVector2D InputPosition = InputSlot->GetPosition();
	// Grow upwards from the input box's top edge.
	PanelSlot->SetAlignment(FVector2D(0.0, 1.0));
	PanelSlot->SetPosition(FVector2D(InputPosition.X, InputPosition.Y - 2.0));
	PanelSlot->SetZOrder(10);
}

bool UMT2ChatWidget::AreSuggestionsVisible() const
{
	return SuggestionsPanel && SuggestionsPanel->GetVisibility() != ESlateVisibility::Collapsed &&
		!CurrentSuggestions.IsEmpty();
}

void UMT2ChatWidget::HideSuggestions()
{
	CurrentSuggestions.Reset();
	SuggestionCursor = 0;
	if (SuggestionsPanel)
	{
		SuggestionsPanel->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UMT2ChatWidget::RefreshSuggestions()
{
	EnsureSuggestionsPanel();
	if (!SuggestionsPanel || !InputBox)
	{
		return;
	}

	// Suggest only while typing the command token itself: leading '/', no space yet.
	const FString Typed = InputBox->GetText().ToString();
	if (!bIsOpen || !Typed.StartsWith(TEXT("/")) || Typed.Contains(TEXT(" ")))
	{
		HideSuggestions();
		return;
	}

	const FString PreviousSelection = CurrentSuggestions.IsValidIndex(SuggestionCursor)
		? CurrentSuggestions[SuggestionCursor] : FString();
	CurrentSuggestions.Reset();
	int32 RowIndex = 0;
	for (const FMT2ChatCommandInfo& Info : ChatCommands)
	{
		UTextBlock* Row = SuggestionTexts[RowIndex];
		if (!FString(Info.Command).StartsWith(Typed, ESearchCase::IgnoreCase))
		{
			++RowIndex;
			Row->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}
		CurrentSuggestions.Add(Info.Command);
		Row->SetText(FText::FromString(FString::Printf(TEXT("%s  -  %s"), Info.Usage, Info.Description)));
		Row->SetVisibility(ESlateVisibility::HitTestInvisible);
		++RowIndex;
	}

	if (CurrentSuggestions.IsEmpty() ||
		(CurrentSuggestions.Num() == 1 && CurrentSuggestions[0].Equals(Typed, ESearchCase::IgnoreCase)))
	{
		HideSuggestions();
		return;
	}

	SuggestionCursor = FMath::Max(0, CurrentSuggestions.IndexOfByKey(PreviousSelection));
	SuggestionsPanel->SetVisibility(ESlateVisibility::HitTestInvisible);

	// Highlight the selected row.
	int32 VisibleIndex = 0;
	for (int32 Index = 0; Index < SuggestionTexts.Num(); ++Index)
	{
		UTextBlock* Row = SuggestionTexts[Index];
		if (Row->GetVisibility() == ESlateVisibility::Collapsed)
		{
			continue;
		}
		const bool bSelected = VisibleIndex == SuggestionCursor;
		Row->SetColorAndOpacity(FSlateColor(bSelected
			? FLinearColor(1.0f, 0.89f, 0.42f) : FLinearColor(0.8f, 0.8f, 0.8f)));
		++VisibleIndex;
	}
}

void UMT2ChatWidget::NavigateSuggestions(int32 Direction)
{
	if (CurrentSuggestions.IsEmpty())
	{
		return;
	}
	SuggestionCursor = (SuggestionCursor + Direction + CurrentSuggestions.Num()) % CurrentSuggestions.Num();
	// Re-run the highlight pass without changing the list.
	int32 VisibleIndex = 0;
	for (UTextBlock* Row : SuggestionTexts)
	{
		if (Row->GetVisibility() == ESlateVisibility::Collapsed)
		{
			continue;
		}
		const bool bSelected = VisibleIndex == SuggestionCursor;
		Row->SetColorAndOpacity(FSlateColor(bSelected
			? FLinearColor(1.0f, 0.89f, 0.42f) : FLinearColor(0.8f, 0.8f, 0.8f)));
		++VisibleIndex;
	}
}

void UMT2ChatWidget::ApplySelectedSuggestion()
{
	if (!InputBox || !CurrentSuggestions.IsValidIndex(SuggestionCursor))
	{
		return;
	}
	InputBox->SetText(FText::FromString(CurrentSuggestions[SuggestionCursor] + TEXT(" ")));
	HideSuggestions();
}

void UMT2ChatWidget::HandleTextChanged(const FText& Text)
{
	if (bIsOpen)
	{
		RefreshSuggestions();
	}
}

void UMT2ChatWidget::HandleTextCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (!bIsOpen)
	{
		return;
	}
	if (CommitMethod == ETextCommit::OnEnter)
	{
		SubmitCurrentText();
	}
	else if (CommitMethod == ETextCommit::OnUserMovedFocus || CommitMethod == ETextCommit::OnCleared)
	{
		CloseInput();
		OnChatClosed.Broadcast();
	}
}

void UMT2ChatWidget::HandleSendClicked()
{
	SubmitCurrentText();
}

void UMT2ChatWidget::HandleFocusInputClicked()
{
	FocusInput();
}

void UMT2ChatWidget::HandleHistoryClicked()
{
	bHistoryPinned = !bHistoryPinned;
	KeepHistoryVisible();
	FocusInput();
}

FReply UMT2ChatWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (bIsOpen && InKeyEvent.GetKey() == EKeys::Escape)
	{
		CloseInput();
		OnChatClosed.Broadcast();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FReply UMT2ChatWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	// The editable text box consumes arrow keys (and Tab drives focus navigation), so grab them in
	// the preview (tunnel) pass, which visits this widget before the focused text box gets them.
	if (bIsOpen && InKeyEvent.GetKey() == EKeys::Tab && AreSuggestionsVisible())
	{
		ApplySelectedSuggestion();
		return FReply::Handled();
	}
	if (bIsOpen && InKeyEvent.GetKey() == EKeys::Up)
	{
		AreSuggestionsVisible() ? NavigateSuggestions(-1) : NavigateSubmitHistory(-1);
		return FReply::Handled();
	}
	if (bIsOpen && InKeyEvent.GetKey() == EKeys::Down)
	{
		AreSuggestionsVisible() ? NavigateSuggestions(1) : NavigateSubmitHistory(1);
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}
