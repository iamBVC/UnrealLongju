/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2DraggableWindowWidget.h"

#include "Input/Events.h"
#include "InputCoreTypes.h"

FReply UMT2DraggableWindowWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	const FVector2D LocalPosition = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && LocalPosition.Y <= DragBarHeight)
	{
		bDraggingWindow = true;
		DragStartMousePosition = InMouseEvent.GetScreenSpacePosition();
		DragStartTranslation = GetRenderTransform().Translation;
		return FReply::Handled().CaptureMouse(TakeWidget());
	}

	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UMT2DraggableWindowWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDraggingWindow && HasMouseCapture())
	{
		const float Scale = FMath::Max(InGeometry.Scale, UE_SMALL_NUMBER);
		SetRenderTranslation(DragStartTranslation + (InMouseEvent.GetScreenSpacePosition() - DragStartMousePosition) / Scale);
		return FReply::Handled();
	}

	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

FReply UMT2DraggableWindowWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDraggingWindow && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDraggingWindow = false;
		return FReply::Handled().ReleaseMouseCapture();
	}

	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}
