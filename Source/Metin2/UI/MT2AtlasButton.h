/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Components/Button.h"
#include "CoreMinimal.h"
#include "UI/MT2AtlasTypes.h"
#include "MT2AtlasButton.generated.h"

class UTexture2D;

UCLASS(meta = (DisplayName = "MT2 Atlas Button"))
class METIN2_API UMT2AtlasButton : public UButton
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atlas")
	TObjectPtr<UTexture2D> AtlasTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atlas|States")
	FMT2AtlasRect NormalRegion;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atlas|States")
	FMT2AtlasRect HoveredRegion;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atlas|States")
	FMT2AtlasRect PressedRegion;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atlas|States")
	FMT2AtlasRect DisabledRegion;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atlas|States")
	bool bUseDisabledRegion = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atlas")
	bool bUseRegionAsImageSize = true;

	UFUNCTION(BlueprintCallable, Category = "Atlas")
	void SetAtlasRegions(UTexture2D* InTexture, const FMT2AtlasRect& InNormal, const FMT2AtlasRect& InHovered, const FMT2AtlasRect& InPressed);

protected:
	virtual void SynchronizeProperties() override;

private:
	void RefreshAtlasStyle();
	FSlateBrush MakeStateBrush(const FSlateBrush& SourceBrush, const FMT2AtlasRect& Region) const;
};
