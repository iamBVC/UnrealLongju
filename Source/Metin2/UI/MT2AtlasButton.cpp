/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2AtlasButton.h"

#include "Engine/Texture2D.h"

void UMT2AtlasButton::SetAtlasRegions(UTexture2D* InTexture, const FMT2AtlasRect& InNormal, const FMT2AtlasRect& InHovered, const FMT2AtlasRect& InPressed)
{
	AtlasTexture = InTexture;
	NormalRegion = InNormal;
	HoveredRegion = InHovered;
	PressedRegion = InPressed;
	RefreshAtlasStyle();
}

void UMT2AtlasButton::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	RefreshAtlasStyle();
}

void UMT2AtlasButton::RefreshAtlasStyle()
{
	if (!AtlasTexture || !NormalRegion.IsValid())
	{
		return;
	}

	FButtonStyle AtlasStyle = GetStyle();
	AtlasStyle.SetNormal(MakeStateBrush(AtlasStyle.Normal, NormalRegion));
	AtlasStyle.SetHovered(MakeStateBrush(AtlasStyle.Hovered, HoveredRegion.IsValid() ? HoveredRegion : NormalRegion));
	AtlasStyle.SetPressed(MakeStateBrush(AtlasStyle.Pressed, PressedRegion.IsValid() ? PressedRegion : NormalRegion));
	if (bUseDisabledRegion && DisabledRegion.IsValid())
	{
		AtlasStyle.SetDisabled(MakeStateBrush(AtlasStyle.Disabled, DisabledRegion));
	}
	SetStyle(AtlasStyle);
}

FSlateBrush UMT2AtlasButton::MakeStateBrush(const FSlateBrush& SourceBrush, const FMT2AtlasRect& Region) const
{
	FSlateBrush Result = SourceBrush;
	Result.SetResourceObject(AtlasTexture);
	const float TextureWidth = FMath::Max(1, AtlasTexture->GetSizeX());
	const float TextureHeight = FMath::Max(1, AtlasTexture->GetSizeY());
	Result.SetUVRegion(FBox2f(
		FVector2f(Region.X / TextureWidth, Region.Y / TextureHeight),
		FVector2f((Region.X + Region.Width) / TextureWidth, (Region.Y + Region.Height) / TextureHeight)));
	if (bUseRegionAsImageSize)
	{
		Result.ImageSize = FVector2D(Region.Width, Region.Height);
	}
	return Result;
}
