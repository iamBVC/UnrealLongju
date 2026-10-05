/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2CursorCarryWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"

TSharedRef<SWidget> UMT2CursorCarryWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		IconImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("CarryIcon"));
		WidgetTree->RootWidget = IconImage;
		SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	return Super::RebuildWidget();
}

void UMT2CursorCarryWidget::SetIconBrush(const FSlateBrush& Brush)
{
	if (IconImage)
	{
		IconImage->SetBrush(Brush);
	}
}

void UMT2CursorCarryWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	// Glue to the cursor, slightly offset so the click point stays visible like the old client.
	const FVector2D MousePosition = UWidgetLayoutLibrary::GetMousePositionOnViewport(GetWorld());
	SetPositionInViewport(MousePosition + FVector2D(4.0, 4.0), false);
}
