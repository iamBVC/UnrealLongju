/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2QuestDialogWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "InputCoreTypes.h"
#include "UI/MT2UIStyle.h"

namespace
{
	const FLinearColor DialogTitleColor(1.0f, 0.89f, 0.42f);
	const FLinearColor DialogBodyColor(0.9f, 0.9f, 0.85f);
	const FLinearColor DialogOptionColor(0.6f, 0.85f, 1.0f);
}

TSharedRef<SWidget> UMT2QuestDialogWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("DialogRoot"));
		WidgetTree->RootWidget = Root;

		UBorder* Board = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DialogBoard"));
		Board->SetBrushColor(FLinearColor(0.03f, 0.03f, 0.04f, 0.92f));
		Board->SetPadding(FMargin(14.0f, 10.0f));
		// Centered-right like the old quest window (it sits beside the NPC, not fullscreen).
		if (UCanvasPanelSlot* BoardSlot = Root->AddChildToCanvas(Board))
		{
			BoardSlot->SetAnchors(FAnchors(0.5f, 0.45f));
			BoardSlot->SetAlignment(FVector2D(0.5, 0.5));
			BoardSlot->SetAutoSize(true);
		}

		// The board auto-sizes to its content, so a size box between the two provides the floor without
		// capping how large a long dialog may grow.
		BoardSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("DialogSizeBox"));
		Board->SetContent(BoardSizeBox);

		UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DialogContent"));
		BoardSizeBox->AddChild(Content);
		ApplyMinimumSize();

		TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DialogTitle"));
		FSlateFontInfo TitleFont = TitleText->GetFont();
		TitleFont.Size = 12;
		TitleText->SetFont(TitleFont);
		TitleText->SetColorAndOpacity(FSlateColor(DialogTitleColor));
		Content->AddChildToVerticalBox(TitleText);

		BodyText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DialogBody"));
		FSlateFontInfo BodyFont = BodyText->GetFont();
		BodyFont.Size = 10;
		BodyText->SetFont(BodyFont);
		BodyText->SetColorAndOpacity(FSlateColor(DialogBodyColor));
		BodyText->SetAutoWrapText(true);
		BodyText->SetWrapTextAt(340.0f);
		if (UVerticalBoxSlot* BodySlot = Content->AddChildToVerticalBox(BodyText))
		{
			BodySlot->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 10.0f));
		}

		OptionsBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DialogOptions"));
		Content->AddChildToVerticalBox(OptionsBox);

		InputBox = WidgetTree->ConstructWidget<UEditableTextBox>(
			UEditableTextBox::StaticClass(), TEXT("DialogInput"));
		InputBox->OnTextCommitted.AddUniqueDynamic(this, &UMT2QuestDialogWidget::HandleInputCommitted);
		InputBox->SetVisibility(ESlateVisibility::Collapsed);
		Content->AddChildToVerticalBox(InputBox);

		InputConfirmButton = WidgetTree->ConstructWidget<UButton>(
			UButton::StaticClass(), TEXT("DialogInputConfirm"));
		InputConfirmButton->OnClicked.AddUniqueDynamic(this, &UMT2QuestDialogWidget::HandleInputConfirmed);
		InputConfirmButton->AddChild(FMT2UIStyle::Label(
			*WidgetTree, FText::FromString(TEXT("OK")), 10, ETextJustify::Center));
		InputConfirmButton->SetVisibility(ESlateVisibility::Collapsed);
		ApplyWidgetSound(InputConfirmButton);
		Content->AddChildToVerticalBox(InputConfirmButton);

		SetVisibility(ESlateVisibility::Collapsed);
		SetIsFocusable(true);
	}
	return Super::RebuildWidget();
}

void UMT2QuestDialogWidget::ApplyMinimumSize()
{
	if (!BoardSizeBox)
	{
		return;
	}
	// A minimum of 0 means "no floor", which the size box expresses by clearing the override.
	if (MinimumWidth > 0.0f) { BoardSizeBox->SetMinDesiredWidth(MinimumWidth); }
	else { BoardSizeBox->ClearMinDesiredWidth(); }

	if (MinimumHeight > 0.0f) { BoardSizeBox->SetMinDesiredHeight(MinimumHeight); }
	else { BoardSizeBox->ClearMinDesiredHeight(); }
}

void UMT2QuestDialogWidget::ShowDialog(const FMT2DialogPayload& Payload)
{
	TakeWidget();
	if (!OptionsBox)
	{
		return;
	}
	// Picks up a minimum changed after the tree was built (Blueprint default or runtime).
	ApplyMinimumSize();

	TitleText->SetText(FText::FromString(Payload.Title));
	TitleText->SetVisibility(Payload.Title.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	BodyText->SetText(FText::FromString(FString::Join(Payload.TextLines, TEXT("\n"))));

	OptionsBox->ClearChildren();
	OptionButtons.Reset();
	bNumericInputActive = Payload.bNumericInput;
	if (InputBox)
	{
		InputBox->SetText(FText::GetEmpty());
		InputBox->SetVisibility(Payload.bRequestsTextInput
			? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (InputConfirmButton)
	{
		InputConfirmButton->SetVisibility(Payload.bRequestsTextInput
			? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	TArray<FString> Options = Payload.Options;
	if (Options.IsEmpty() && !Payload.bRequestsTextInput)
	{
		Options.Add(TEXT("Close"));
	}
	for (int32 Index = 0; Index < Options.Num(); ++Index)
	{
		UButton* OptionButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		FButtonStyle Style;
		Style.Normal.DrawAs = ESlateBrushDrawType::NoDrawType;
		Style.Hovered.DrawAs = ESlateBrushDrawType::Box;
		Style.Hovered.TintColor = FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.08f));
		Style.Pressed = Style.Hovered;
		OptionButton->SetStyle(Style);
		OptionButton->OnClicked.AddUniqueDynamic(this, &UMT2QuestDialogWidget::HandleOptionClicked);

		UTextBlock* Label = FMT2UIStyle::Label(*WidgetTree,
			FText::FromString(FString::Printf(TEXT("%s"), *Options[Index])), 10, ETextJustify::Left);
		Label->SetColorAndOpacity(FSlateColor(DialogOptionColor));
		OptionButton->AddChild(Label);
		if (UButtonSlot* LabelSlot = Cast<UButtonSlot>(Label->Slot))
		{
			LabelSlot->SetPadding(FMargin(4.0f, 2.0f));
			LabelSlot->SetHorizontalAlignment(HAlign_Left);
		}

		// Option buttons are built per dialog, long after NativeConstruct walked the tree, and their
		// custom style carries no sound of its own.
		ApplyWidgetSound(OptionButton);
		OptionsBox->AddChildToVerticalBox(OptionButton);
		OptionButtons.Add(OptionButton);
	}

	SetVisibility(ESlateVisibility::Visible);
	if (Payload.bRequestsTextInput && InputBox)
	{
		InputBox->SetKeyboardFocus();
	}
}

void UMT2QuestDialogWidget::CloseDialog()
{
	SetVisibility(ESlateVisibility::Collapsed);
}

int32 UMT2QuestDialogWidget::FindClickedOption() const
{
	for (int32 Index = 0; Index < OptionButtons.Num(); ++Index)
	{
		if (OptionButtons[Index] && OptionButtons[Index]->IsHovered())
		{
			return Index + 1;
		}
	}
	return 0;
}

void UMT2QuestDialogWidget::HandleOptionClicked()
{
	const int32 Choice = FindClickedOption();
	CloseDialog();
	OnOptionSelected.Broadcast(Choice);
}

void UMT2QuestDialogWidget::HandleCloseClicked()
{
	CloseDialog();
	// Dismissal must not masquerade as submitting a legitimate empty input string.
	OnOptionSelected.Broadcast(0);
}

void UMT2QuestDialogWidget::HandleInputCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		HandleInputConfirmed();
	}
}

void UMT2QuestDialogWidget::HandleInputConfirmed()
{
	const FString Value = InputBox ? InputBox->GetText().ToString().TrimStartAndEnd() : FString();
	if (bNumericInputActive && !Value.IsNumeric())
	{
		return;
	}
	CloseDialog();
	OnTextSubmitted.Broadcast(Value);
}

FReply UMT2QuestDialogWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape && GetVisibility() == ESlateVisibility::Visible)
	{
		HandleCloseClicked();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}
