/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2SlotEffectWidget.h"

#include "Blueprint/WidgetTree.h"
#include "UI/MT2AtlasImage.h"
#include "UI/MT2UIStyle.h"

namespace
{
	constexpr int32 SlotEffectFrameCount = 13;
	constexpr float SlotEffectFrameSeconds = 1.0f / 15.0f;

	FMT2AtlasRect EffectFrame(int32 Frame)
	{
		const int32 Index = FMath::Clamp(Frame, 0, SlotEffectFrameCount - 1);
		return FMT2AtlasRect(
			static_cast<float>((Index % 8) * 32),
			static_cast<float>(252 + (Index / 8) * 32), 32.0f, 32.0f);
	}
}

TSharedRef<SWidget> UMT2SlotEffectWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		EffectAtlas = FMT2UIStyle::LoadTexture(TEXT("/Game/ymir_work/ui/T_public.T_public"));
		EffectImage = WidgetTree->ConstructWidget<UMT2AtlasImage>(
			UMT2AtlasImage::StaticClass(), TEXT("SlotEffectImage"));
		EffectImage->SetAtlas(EffectAtlas, EffectFrame(0));
		EffectImage->SetVisibility(ESlateVisibility::HitTestInvisible);
		WidgetTree->RootWidget = EffectImage;
	}
	return Super::RebuildWidget();
}

void UMT2SlotEffectWidget::SetEffectVisible(bool bVisible, const FLinearColor& Tint)
{
	TakeWidget();
	SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (EffectImage)
	{
		EffectImage->SetColorAndOpacity(Tint);
	}
	if (!bVisible)
	{
		ElapsedSeconds = 0.0f;
	}
}

void UMT2SlotEffectWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!EffectImage || GetVisibility() == ESlateVisibility::Collapsed)
	{
		return;
	}
	ElapsedSeconds += InDeltaTime;
	EffectImage->SetAtlas(EffectAtlas,
		EffectFrame(FMath::FloorToInt(ElapsedSeconds / SlotEffectFrameSeconds) % SlotEffectFrameCount));
}
