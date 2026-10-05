/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2AtlasImage.h"

#include "Engine/Texture2D.h"
#if WITH_EDITOR
#include "TextureCompiler.h"
#endif

namespace
{
	FBox2f MakeUVRegion(const UTexture2D* Texture, const FMT2AtlasRect& Region)
	{
		const float TextureWidth = FMath::Max(1, Texture->GetSizeX());
		const float TextureHeight = FMath::Max(1, Texture->GetSizeY());
		return FBox2f(
			FVector2f(Region.X / TextureWidth, Region.Y / TextureHeight),
			FVector2f((Region.X + Region.Width) / TextureWidth, (Region.Y + Region.Height) / TextureHeight));
	}
}

void UMT2AtlasImage::SetAtlas(UTexture2D* InTexture, const FMT2AtlasRect& InRegion)
{
	AtlasTexture = InTexture;
	AtlasRegion = InRegion;
	RefreshAtlasBrush();
}

void UMT2AtlasImage::SetAtlasRegion(const FMT2AtlasRect& InRegion)
{
	AtlasRegion = InRegion;
	RefreshAtlasBrush();
}

void UMT2AtlasImage::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	RefreshAtlasBrush();
}

void UMT2AtlasImage::RefreshAtlasBrush()
{
	if (!AtlasTexture || !AtlasRegion.IsValid())
	{
		return;
	}

	// On the first PIE the atlas texture can still be async-compiling; its render resource isn't
	// ready and GetSizeX() reports a placeholder size, so any UVs computed now would be garbage.
	// Finish it synchronously instead of skipping - skipping left never-refreshed brushes behind
	// on widgets whose SetAtlas ran during compilation.
#if WITH_EDITOR
	FTextureCompilingManager::Get().FinishCompilation({AtlasTexture.Get()});
#endif

	// Keep the atlas fully resident so it never shows streamed-in placeholder mips.
	AtlasTexture->bIgnoreStreamingMipBias = true;
	AtlasTexture->SetForceMipLevelsToBeResident(-1.0f);
	AtlasTexture->WaitForStreaming();

	FSlateBrush AtlasBrush = GetBrush();
	AtlasBrush.SetResourceObject(AtlasTexture);
	AtlasBrush.SetUVRegion(MakeUVRegion(AtlasTexture, AtlasRegion));
	if (bUseRegionAsImageSize)
	{
		AtlasBrush.ImageSize = FVector2D(AtlasRegion.Width, AtlasRegion.Height);
	}
	SetBrush(AtlasBrush);
}
