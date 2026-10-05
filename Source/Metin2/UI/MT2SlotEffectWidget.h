/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "UI/MT2UserWidget.h"
#include "MT2SlotEffectWidget.generated.h"

class UMT2AtlasImage;
class UTexture2D;

// Shared recreation of CSlotWindow's 13-frame slotactiveeffect animation. Inventory and taskbar
// attach this code widget over their existing Blueprint-authored icon, preserving designer layouts.
UCLASS()
class METIN2_API UMT2SlotEffectWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	void SetEffectVisible(bool bVisible, const FLinearColor& Tint = FLinearColor::White);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UPROPERTY(Transient) TObjectPtr<UMT2AtlasImage> EffectImage;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> EffectAtlas;
	float ElapsedSeconds = 0.0f;
};
