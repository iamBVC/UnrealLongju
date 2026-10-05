/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2UserWidget.h"
#include "Audio/MT2SoundPlaybackSubsystem.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/EditableText.h"
#include "Components/EditableTextBox.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

UMT2UserWidget::UMT2UserWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, DefaultButtonClickSound(FSoftObjectPath(TEXT("/Game/sound/ui/click.click")))
	, TypingSound(FSoftObjectPath(TEXT("/Game/sound/ui/type.type")))
{
}

void UMT2UserWidget::NativeConstruct()
{
	Super::NativeConstruct();
	RefreshWidgetSounds();
}

void UMT2UserWidget::RefreshWidgetSounds()
{
	if (!WidgetTree)
	{
		return;
	}
	WidgetTree->ForEachWidget([this](UWidget* Widget) { ApplyWidgetSound(Widget); });
}

void UMT2UserWidget::ApplyWidgetSound(UWidget* Widget)
{
	if (!Widget)
	{
		return;
	}

	if (UButton* Button = Cast<UButton>(Widget))
	{
		USoundBase* ClickSound = DefaultButtonClickSound.LoadSynchronous();
		FButtonStyle Style = Button->GetStyle();
		// A Pressed Sound set in the Widget Blueprint wins; this only fills the gap.
		if (ClickSound && !Style.PressedSlateSound.GetResourceObject())
		{
			FSlateSound SlateClickSound;
			SlateClickSound.SetResourceObject(ClickSound);
			Style.SetPressedSound(SlateClickSound);
			Button->SetStyle(Style);
		}
		return;
	}

	// AddUnique keeps a re-run from stacking handlers, so a rebuilt list does not end up playing the
	// typing sound several times per keystroke.
	if (UEditableText* EditableText = Cast<UEditableText>(Widget))
	{
		EditableText->OnTextChanged.AddUniqueDynamic(this, &UMT2UserWidget::HandleTypingSound);
	}
	else if (UEditableTextBox* TextBox = Cast<UEditableTextBox>(Widget))
	{
		TextBox->OnTextChanged.AddUniqueDynamic(this, &UMT2UserWidget::HandleTypingSound);
	}
	else if (UMultiLineEditableTextBox* MultiLineBox = Cast<UMultiLineEditableTextBox>(Widget))
	{
		MultiLineBox->OnTextChanged.AddUniqueDynamic(this, &UMT2UserWidget::HandleTypingSound);
	}
}

void UMT2UserWidget::HandleTypingSound(const FText& Text)
{
	if (USoundBase* Sound = TypingSound.LoadSynchronous())
	{
		UMT2SoundPlaybackSubsystem::PlayExclusive2D(this, Sound);
	}
}
