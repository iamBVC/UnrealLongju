/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2QuestLogEntryWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace
{
	// Deliberately small: the quest page is a narrow panel, and the old client's quest list is compact.
	constexpr int32 TitleFontSize = 9;
	constexpr int32 DetailFontSize = 8;

	UTextBlock* MakeLine(UWidgetTree& Tree, UVerticalBox& Parent, int32 FontSize,
		const FLinearColor& Colour, float TopPadding)
	{
		UTextBlock* Text = Tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = FontSize;
		Text->SetFont(Font);
		Text->SetColorAndOpacity(FSlateColor(Colour));
		Text->SetAutoWrapText(true);
		if (UVerticalBoxSlot* BoxSlot = Parent.AddChildToVerticalBox(Text))
		{
			BoxSlot->SetPadding(FMargin(0.0f, TopPadding, 0.0f, 0.0f));
			FSlateChildSize Size;
			Size.SizeRule = ESlateSizeRule::Automatic;
			BoxSlot->SetSize(Size);
			BoxSlot->SetHorizontalAlignment(HAlign_Fill);
		}
		return Text;
	}
}

TSharedRef<SWidget> UMT2QuestLogEntryWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		// The whole row is the button, so clicking anywhere on the entry opens the quest.
		ClickButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("ClickButton"));
		// Transparent styling: the row should read as text, not as a chunky button.
		FButtonStyle Style = ClickButton->GetStyle();
		Style.Normal.TintColor = FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.0f));
		Style.Hovered.TintColor = FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.10f));
		Style.Pressed.TintColor = FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.18f));
		ClickButton->SetStyle(Style);
		ClickButton->OnClicked.AddUniqueDynamic(this, &UMT2QuestLogEntryWidget::HandleClicked);

		Lines = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Lines"));
		if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(ClickButton->AddChild(Lines)))
		{
			ButtonSlot->SetPadding(FMargin(6.0f, 3.0f));
			ButtonSlot->SetHorizontalAlignment(HAlign_Fill);
		}

		TitleText = MakeLine(*WidgetTree, *Lines, TitleFontSize, FLinearColor(1.0f, 0.85f, 0.45f), 0.0f);
		SummaryText = MakeLine(*WidgetTree, *Lines, DetailFontSize, FLinearColor(0.85f, 0.85f, 0.85f), 1.0f);
		CounterText = MakeLine(*WidgetTree, *Lines, DetailFontSize, FLinearColor(0.75f, 0.8f, 1.0f), 1.0f);

		WidgetTree->RootWidget = ClickButton;
		ApplyEntry();
	}
	return Super::RebuildWidget();
}

void UMT2QuestLogEntryWidget::SetEntry(
	FName InQuestId, const FText& Title, const FText& Summary, int32 Counter)
{
	QuestId = InQuestId;
	PendingTitle = Title;
	PendingSummary = Summary;
	PendingCounter = Counter;
	ApplyEntry();
}

void UMT2QuestLogEntryWidget::ApplyEntry()
{
	const FText& Title = PendingTitle;
	const FText& Summary = PendingSummary;
	const int32 Counter = PendingCounter;
	if (TitleText)
	{
		TitleText->SetText(Title);
		TitleText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.85f, 0.45f)));
	}
	if (SummaryText)
	{
		SummaryText->SetText(Summary);
		SummaryText->SetVisibility(
			Summary.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (CounterText)
	{
		// Counter is -1 when the quest shows no progress figure.
		CounterText->SetText(FText::FromString(FString::Printf(TEXT("Progress: %d"), Counter)));
		CounterText->SetVisibility(
			Counter >= 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UMT2QuestLogEntryWidget::HandleClicked()
{
	OnQuestClicked.Broadcast(QuestId);
}
