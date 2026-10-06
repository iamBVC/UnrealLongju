/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2TitleBarWidget.h"
#include "Config/MT2PathSettings.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "UI/MT2UIStyle.h"

void UMT2TitleBarWidget::InitializeTitleBar(float InWidth, const FText& InTitle)
{
	Title = InTitle;
	SetDesiredSizeInViewport(FVector2D(InWidth, 23.0f));
	RefreshVisuals();
}

void UMT2TitleBarWidget::NativeConstruct()
{
	Super::NativeConstruct();
	CloseButton->OnClicked.AddUniqueDynamic(this, &UMT2TitleBarWidget::HandleCloseClicked);
	RefreshVisuals();
}

void UMT2TitleBarWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	RefreshVisuals();
}

void UMT2TitleBarWidget::RefreshVisuals()
{
	TitleText->SetText(Title);
	TitleLeft->SetBrush(FMT2UIStyle::TextureBrush(FMT2UIStyle::LoadTexture(UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_titlebar_left")))));
	TitleCenter->SetBrush(FMT2UIStyle::TextureBrush(FMT2UIStyle::LoadTexture(UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_titlebar_center")))));
	TitleRight->SetBrush(FMT2UIStyle::TextureBrush(FMT2UIStyle::LoadTexture(UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_titlebar_right")))));
}

void UMT2TitleBarWidget::HandleCloseClicked()
{
	OnCloseClicked.Broadcast();
}
