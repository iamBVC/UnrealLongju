/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Styling/SlateBrush.h"

class UCanvasPanel;
class UTexture2D;
class UWidget;
class UWidgetTree;

struct FMT2AtlasRegion
{
	float Left = 0.0f;
	float Top = 0.0f;
	float Right = 0.0f;
	float Bottom = 0.0f;

	FMT2AtlasRegion() = default;
	FMT2AtlasRegion(float InLeft, float InTop, float InRight, float InBottom)
		: Left(InLeft), Top(InTop), Right(InRight), Bottom(InBottom) {}

	FVector2D Size() const { return FVector2D(Right - Left, Bottom - Top); }
};

class METIN2_API FMT2UIStyle
{
public:
	static UTexture2D* LoadTexture(const TCHAR* ObjectPath);
	static FSlateBrush TextureBrush(UTexture2D* Texture, bool bTileHorizontal = false, bool bTileVertical = false);
	static FSlateBrush AtlasBrush(UTexture2D* Texture, const FMT2AtlasRegion& Region);
	static UImage* Image(UWidgetTree& Tree, UTexture2D* Texture, bool bTileHorizontal = false, bool bTileVertical = false);
	static UImage* AtlasImage(UWidgetTree& Tree, UTexture2D* Texture, const FMT2AtlasRegion& Region);
	static UButton* AtlasButton(UWidgetTree& Tree, UTexture2D* Texture, const FMT2AtlasRegion& Normal,
		const FMT2AtlasRegion& Hovered, const FMT2AtlasRegion& Pressed, const FText& Tooltip = FText::GetEmpty());
	static UButton* TextureButton(UWidgetTree& Tree, UTexture2D* Normal, UTexture2D* Hovered, UTexture2D* Pressed,
		const FText& Tooltip = FText::GetEmpty());
	static UTextBlock* Label(UWidgetTree& Tree, const FText& Text, int32 FontSize = 10, ETextJustify::Type Justification = ETextJustify::Center);
	static UCanvasPanelSlot* Place(UCanvasPanel* Canvas, UWidget* Widget, const FVector2D& Position, const FVector2D& Size,
		const FAnchors& Anchors = FAnchors(0.0f), const FVector2D& Alignment = FVector2D::ZeroVector);

	// Recreates ui.py's ThinBoard: 16px corner images + tiled edge lines around a translucent black
	// base (BOARD_COLOR = black at 51% alpha) - the old tooltip/board background. Returns a panel
	// that auto-sizes around Content.
	static UWidget* ThinBoard(UWidgetTree& Tree, UWidget* Content, const FMargin& ContentPadding = FMargin(8.0f, 6.0f));
};
