/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "MT2ItemBlueprintThumbnailRenderer.h"

#include "CanvasItem.h"
#include "Engine/Blueprint.h"
#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "Items/MT2ItemTemplate.h"

UTexture2D* UMT2ItemBlueprintThumbnailRenderer::ResolveItemIcon(UObject* Object)
{
	const UBlueprint* Blueprint = Cast<UBlueprint>(Object);
	if (!Blueprint || !Blueprint->GeneratedClass ||
		!Blueprint->GeneratedClass->IsChildOf(UMT2ItemTemplate::StaticClass()))
	{
		return nullptr;
	}

	const UMT2ItemTemplate* Template =
		Blueprint->GeneratedClass->GetDefaultObject<UMT2ItemTemplate>();
	return Template ? Template->Icon.LoadSynchronous() : nullptr;
}

bool UMT2ItemBlueprintThumbnailRenderer::CanVisualizeAsset(UObject* Object)
{
	const UBlueprint* Blueprint = Cast<UBlueprint>(Object);
	if (Blueprint && Blueprint->GeneratedClass &&
		Blueprint->GeneratedClass->IsChildOf(UMT2ItemTemplate::StaticClass()))
	{
		return true;
	}
	return Super::CanVisualizeAsset(Object);
}

void UMT2ItemBlueprintThumbnailRenderer::Draw(
	UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height,
	FRenderTarget* RenderTarget, FCanvas* Canvas, bool bAdditionalViewFamily)
{
	UTexture2D* Icon = ResolveItemIcon(Object);
	if (!Icon || !Icon->GetResource() || !Canvas)
	{
		Super::Draw(
			Object, X, Y, Width, Height, RenderTarget, Canvas, bAdditionalViewFamily);
		return;
	}

	FCanvasTileItem Background(
		FVector2D(X, Y), FVector2D(Width, Height), FLinearColor(0.015f, 0.015f, 0.015f, 1.0f));
	Background.BlendMode = SE_BLEND_Opaque;
	Canvas->DrawItem(Background);

	const float TextureWidth = FMath::Max(static_cast<float>(Icon->GetSurfaceWidth()), 1.0f);
	const float TextureHeight = FMath::Max(static_cast<float>(Icon->GetSurfaceHeight()), 1.0f);
	const float Padding = FMath::Max(FMath::Min(Width, Height) * 0.04f, 2.0f);
	const FVector2D Available(
		FMath::Max(static_cast<float>(Width) - Padding * 2.0f, 1.0f),
		FMath::Max(static_cast<float>(Height) - Padding * 2.0f, 1.0f));
	const float Scale = FMath::Min(Available.X / TextureWidth, Available.Y / TextureHeight);
	const FVector2D DrawSize(TextureWidth * Scale, TextureHeight * Scale);
	const FVector2D DrawPosition(
		X + (static_cast<float>(Width) - DrawSize.X) * 0.5f,
		Y + (static_cast<float>(Height) - DrawSize.Y) * 0.5f);

	FCanvasTileItem IconTile(
		DrawPosition, Icon->GetResource(), DrawSize, FVector2D::ZeroVector,
		FVector2D::UnitVector, FLinearColor::White);
	IconTile.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(IconTile);
}
