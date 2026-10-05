/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2EmpireFlagImage.h"

#include "Config/MT2GameplaySettings.h"
#include "Engine/Texture2D.h"

void UMT2EmpireFlagImage::SetEmpire(EMT2Empire NewEmpire)
{
	Empire = NewEmpire;
	RefreshEmpireBrush();
}

void UMT2EmpireFlagImage::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	RefreshEmpireBrush();
}

void UMT2EmpireFlagImage::RefreshEmpireBrush()
{
	const UMT2GameplaySettings& Settings = UMT2GameplaySettings::Get();
	const FMT2AtlasRect* Region = Settings.EmpireFlagRegions.Find(Empire);
	UTexture2D* Texture = Region ? Settings.EmpireFlagAtlas.LoadSynchronous() : nullptr;
	const bool bHasFlag = Texture && Region && Region->IsValid();
	SetVisibility(bHasFlag ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (!bHasFlag)
	{
		return;
	}

	bUseRegionAsImageSize = false;
	SetDesiredSizeOverride(Settings.EmpireFlagDisplaySize);
	SetAtlas(Texture, *Region);
}
