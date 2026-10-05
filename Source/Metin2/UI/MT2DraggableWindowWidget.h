/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "UI/MT2UserWidget.h"
#include "CoreMinimal.h"
#include "MT2DraggableWindowWidget.generated.h"

UCLASS(Abstract)
class METIN2_API UMT2DraggableWindowWidget : public UMT2UserWidget
{
	GENERATED_BODY()

protected:
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Window")
	float DragBarHeight = 30.0f;

private:
	bool bDraggingWindow = false;
	FVector2D DragStartMousePosition = FVector2D::ZeroVector;
	FVector2D DragStartTranslation = FVector2D::ZeroVector;
};
