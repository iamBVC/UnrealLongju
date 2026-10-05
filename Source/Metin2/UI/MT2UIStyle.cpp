/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2UIStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/GridPanel.h"
#include "Components/GridSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#if WITH_EDITOR
#include "TextureCompiler.h"
#endif

UTexture2D* FMT2UIStyle::LoadTexture(const TCHAR* ObjectPath)
{
	UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, ObjectPath);
	if (Texture)
	{
#if WITH_EDITOR
		// First PIE after editor start: the texture may still be async-compiling, so GetSizeX()
		// reports a placeholder and every UV/size computed from it bakes garbage into brushes.
		FTextureCompilingManager::Get().FinishCompilation({Texture});
#endif
		// UI atlases must never show streamed-in low mips; keep them fully resident.
		Texture->bIgnoreStreamingMipBias = true;
		Texture->SetForceMipLevelsToBeResident(-1.0f);
		Texture->WaitForStreaming();
	}
	return Texture;
}

FSlateBrush FMT2UIStyle::TextureBrush(UTexture2D* Texture, bool bTileHorizontal, bool bTileVertical)
{
	FSlateBrush Brush;
	if (Texture)
	{
		Brush.SetResourceObject(Texture);
		Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
		Brush.Tiling = bTileHorizontal && bTileVertical ? ESlateBrushTileType::Both
			: bTileHorizontal ? ESlateBrushTileType::Horizontal
			: bTileVertical ? ESlateBrushTileType::Vertical
			: ESlateBrushTileType::NoTile;
	}
	return Brush;
}

FSlateBrush FMT2UIStyle::AtlasBrush(UTexture2D* Texture, const FMT2AtlasRegion& Region)
{
	FSlateBrush Brush;
	if (Texture)
	{
		Brush.SetResourceObject(Texture);
		Brush.ImageSize = Region.Size();
		Brush.SetUVRegion(FBox2f(
			FVector2f(Region.Left / Texture->GetSizeX(), Region.Top / Texture->GetSizeY()),
			FVector2f(Region.Right / Texture->GetSizeX(), Region.Bottom / Texture->GetSizeY())));
	}
	return Brush;
}

UImage* FMT2UIStyle::Image(UWidgetTree& Tree, UTexture2D* Texture, bool bTileHorizontal, bool bTileVertical)
{
	UImage* Widget = Tree.ConstructWidget<UImage>();
	Widget->SetBrush(TextureBrush(Texture, bTileHorizontal, bTileVertical));
	return Widget;
}

UImage* FMT2UIStyle::AtlasImage(UWidgetTree& Tree, UTexture2D* Texture, const FMT2AtlasRegion& Region)
{
	UImage* Widget = Tree.ConstructWidget<UImage>();
	Widget->SetBrush(AtlasBrush(Texture, Region));
	return Widget;
}

UButton* FMT2UIStyle::AtlasButton(UWidgetTree& Tree, UTexture2D* Texture, const FMT2AtlasRegion& Normal,
	const FMT2AtlasRegion& Hovered, const FMT2AtlasRegion& Pressed, const FText& Tooltip)
{
	UButton* Button = Tree.ConstructWidget<UButton>();
	Button->IsFocusable = false;
	Button->SetToolTipText(Tooltip);
	FButtonStyle Style = Button->GetStyle();
	Style.SetNormal(AtlasBrush(Texture, Normal));
	Style.SetHovered(AtlasBrush(Texture, Hovered));
	Style.SetPressed(AtlasBrush(Texture, Pressed));
	Button->SetStyle(Style);
	return Button;
}

UButton* FMT2UIStyle::TextureButton(UWidgetTree& Tree, UTexture2D* Normal, UTexture2D* Hovered, UTexture2D* Pressed, const FText& Tooltip)
{
	UButton* Button = Tree.ConstructWidget<UButton>();
	Button->IsFocusable = false;
	Button->SetToolTipText(Tooltip);
	FButtonStyle Style = Button->GetStyle();
	Style.SetNormal(TextureBrush(Normal));
	Style.SetHovered(TextureBrush(Hovered ? Hovered : Normal));
	Style.SetPressed(TextureBrush(Pressed ? Pressed : Normal));
	Button->SetStyle(Style);
	return Button;
}

UTextBlock* FMT2UIStyle::Label(UWidgetTree& Tree, const FText& Text, int32 FontSize, ETextJustify::Type Justification)
{
	UTextBlock* LabelWidget = Tree.ConstructWidget<UTextBlock>();
	LabelWidget->SetText(Text);
	LabelWidget->SetJustification(Justification);
	LabelWidget->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.93f, 0.86f, 1.0f)));
	LabelWidget->SetShadowOffset(FVector2D(1.0f));
	LabelWidget->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.8f));
	FSlateFontInfo Font = LabelWidget->GetFont();
	Font.Size = FontSize;
	LabelWidget->SetFont(Font);
	return LabelWidget;
}

UCanvasPanelSlot* FMT2UIStyle::Place(UCanvasPanel* Canvas, UWidget* Widget, const FVector2D& Position, const FVector2D& Size,
	const FAnchors& Anchors, const FVector2D& Alignment)
{
	UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Widget);
	Slot->SetAnchors(Anchors);
	Slot->SetAlignment(Alignment);
	Slot->SetPosition(Position);
	Slot->SetSize(Size);
	return Slot;
}

UWidget* FMT2UIStyle::ThinBoard(UWidgetTree& Tree, UWidget* Content, const FMargin& ContentPadding)
{
	// ui.py ThinBoard: 16x16 corners, edge lines tiled along each side, and a black 51%-alpha base
	// behind the content. Laid out as a 3x3 grid whose middle cell holds the content, so the whole
	// board auto-sizes around it exactly like the old SetSize() math did.
	constexpr float Corner = 16.0f;
	const FLinearColor BoardColor(0.0f, 0.0f, 0.0f, 0.51f);
	auto Pattern = [](const TCHAR* Name)
	{
		return LoadTexture(*FString::Printf(
			TEXT("/Game/ymir_work/ui/pattern/T_thinboard_%s.T_thinboard_%s"), Name, Name));
	};

	UGridPanel* Board = Tree.ConstructWidget<UGridPanel>();
	Board->SetColumnFill(1, 1.0f);
	Board->SetRowFill(1, 1.0f);

	auto AddPiece = [&](UTexture2D* Texture, int32 Row, int32 Column, bool bTileH, bool bTileV)
	{
		UImage* Piece = Tree.ConstructWidget<UImage>();
		FSlateBrush Brush = TextureBrush(Texture, bTileH, bTileV);
		Brush.ImageSize = FVector2D(Corner, Corner);
		Piece->SetBrush(Brush);
		Piece->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (UGridSlot* Slot = Board->AddChildToGrid(Piece, Row, Column))
		{
			Slot->SetHorizontalAlignment(bTileH ? HAlign_Fill : HAlign_Left);
			Slot->SetVerticalAlignment(bTileV ? VAlign_Fill : VAlign_Top);
		}
	};

	AddPiece(Pattern(TEXT("corner_lefttop")), 0, 0, false, false);
	AddPiece(Pattern(TEXT("line_top")), 0, 1, true, false);
	AddPiece(Pattern(TEXT("corner_righttop")), 0, 2, false, false);
	AddPiece(Pattern(TEXT("line_left")), 1, 0, false, true);
	AddPiece(Pattern(TEXT("line_right")), 1, 2, false, true);
	AddPiece(Pattern(TEXT("corner_leftbottom")), 2, 0, false, false);
	AddPiece(Pattern(TEXT("line_bottom")), 2, 1, true, false);
	AddPiece(Pattern(TEXT("corner_rightbottom")), 2, 2, false, false);

	// Middle cell: translucent base with the content on top.
	UOverlay* Center = Tree.ConstructWidget<UOverlay>();
	UImage* Base = Tree.ConstructWidget<UImage>();
	FSlateBrush BaseBrush;
	BaseBrush.DrawAs = ESlateBrushDrawType::Box;
	BaseBrush.TintColor = FSlateColor(BoardColor);
	Base->SetBrush(BaseBrush);
	Base->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UOverlaySlot* BaseSlot = Center->AddChildToOverlay(Base))
	{
		BaseSlot->SetHorizontalAlignment(HAlign_Fill);
		BaseSlot->SetVerticalAlignment(VAlign_Fill);
	}
	if (UOverlaySlot* ContentSlot = Center->AddChildToOverlay(Content))
	{
		ContentSlot->SetPadding(ContentPadding);
		ContentSlot->SetHorizontalAlignment(HAlign_Fill);
		ContentSlot->SetVerticalAlignment(VAlign_Fill);
	}
	if (UGridSlot* CenterSlot = Board->AddChildToGrid(Center, 1, 1))
	{
		CenterSlot->SetHorizontalAlignment(HAlign_Fill);
		CenterSlot->SetVerticalAlignment(VAlign_Fill);
	}
	return Board;
}
