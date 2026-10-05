/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2ExperienceGaugeWidget.h"

#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "UI/MT2UIStyle.h"

void UMT2ExperienceGaugeWidget::NativeConstruct()
{
	Super::NativeConstruct();
	Segments = {ExperienceSegment1, ExperienceSegment2, ExperienceSegment3, ExperienceSegment4};
	ApplyGaugeStyle();
	SetExperiencePercent(0.0f);
}

void UMT2ExperienceGaugeWidget::ApplyGaugeStyle()
{
	UTexture2D* Taskbar = FMT2UIStyle::LoadTexture(TEXT("/Game/ymir_work/ui/T_taskbar.T_taskbar"));
	if (!Taskbar)
	{
		return;
	}

	for (UProgressBar* Segment : Segments)
	{
		if (!Segment)
		{
			continue;
		}
		FProgressBarStyle Style = Segment->GetWidgetStyle();
		Style.BackgroundImage.DrawAs = ESlateBrushDrawType::NoDrawType;
		Style.FillImage = FMT2UIStyle::AtlasBrush(Taskbar, FMT2AtlasRegion(487, 0, 506, 19));
		Style.MarqueeImage.DrawAs = ESlateBrushDrawType::NoDrawType;
		Segment->SetWidgetStyle(Style);
		Segment->SetFillColorAndOpacity(FLinearColor::White);
	}
}

void UMT2ExperienceGaugeWidget::SetExperiencePercent(float Percent)
{
	const float Scaled = FMath::Clamp(Percent, 0.0f, 1.0f) * 4.0f;
	for (int32 Index = 0; Index < Segments.Num(); ++Index)
	{
		if (Segments[Index])
		{
			Segments[Index]->SetPercent(FMath::Clamp(Scaled - Index, 0.0f, 1.0f));
		}
	}
}

void UMT2ExperienceGaugeWidget::SetExperience(int64 CurrentExperience, int64 RequiredExperience)
{
	const bool bMaximumLevel = RequiredExperience <= 0;
	const float Percent = bMaximumLevel
		? 1.0f
		: FMath::Clamp(static_cast<double>(CurrentExperience) / static_cast<double>(RequiredExperience), 0.0, 1.0);
	SetExperiencePercent(Percent);

	FNumberFormattingOptions PercentFormat;
	PercentFormat.MinimumFractionalDigits = 1;
	PercentFormat.MaximumFractionalDigits = 1;
	const FText ToolTip = bMaximumLevel
		? NSLOCTEXT("MT2UI", "MaximumLevelExperienceToolTip", "EXP: 100% (maximum level)")
		: FText::Format(
			NSLOCTEXT("MT2UI", "ExperienceToolTip", "EXP: {0}%\n{1} / {2}"),
			FText::AsNumber(Percent * 100.0, &PercentFormat),
			FText::AsNumber(CurrentExperience),
			FText::AsNumber(RequiredExperience));
	SetGaugeToolTip(ToolTip);
}

void UMT2ExperienceGaugeWidget::SetGaugeToolTip(const FText& ToolTip)
{
	SetToolTipText(ToolTip);
	if (ExperienceBackground)
	{
		ExperienceBackground->SetToolTipText(ToolTip);
	}
	for (UProgressBar* Segment : Segments)
	{
		if (Segment)
		{
			Segment->SetToolTipText(ToolTip);
		}
	}
}
