/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "ThumbnailRendering/BlueprintThumbnailRenderer.h"
#include "MT2ItemBlueprintThumbnailRenderer.generated.h"

// UMT2ItemTemplate assets are Blueprint classes, so the Content Browser normally gives them the
// generic class pie icon. This renderer draws the template's imported item icon instead and delegates
// every non-item Blueprint back to Unreal's standard renderer.
UCLASS()
class UMT2ItemBlueprintThumbnailRenderer : public UBlueprintThumbnailRenderer
{
	GENERATED_BODY()

public:
	virtual bool CanVisualizeAsset(UObject* Object) override;
	virtual void Draw(
		UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height,
		FRenderTarget* RenderTarget, FCanvas* Canvas, bool bAdditionalViewFamily) override;

private:
	static class UTexture2D* ResolveItemIcon(UObject* Object);
};
