/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Components/Image.h"
#include "CoreMinimal.h"
#include "UI/MT2AtlasTypes.h"
#include "MT2AtlasImage.generated.h"

class UTexture2D;

UCLASS(meta = (DisplayName = "MT2 Atlas Image"))
class METIN2_API UMT2AtlasImage : public UImage
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atlas")
	TObjectPtr<UTexture2D> AtlasTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atlas")
	FMT2AtlasRect AtlasRegion;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atlas")
	bool bUseRegionAsImageSize = true;

	UFUNCTION(BlueprintCallable, Category = "Atlas")
	void SetAtlas(UTexture2D* InTexture, const FMT2AtlasRect& InRegion);

	UFUNCTION(BlueprintCallable, Category = "Atlas")
	void SetAtlasRegion(const FMT2AtlasRect& InRegion);

protected:
	virtual void SynchronizeProperties() override;

private:
	void RefreshAtlasBrush();
};
