/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Blueprint/UserWidget.h"
#include "Components/WidgetComponent.h"
#include "CoreMinimal.h"
#include "MT2GMMarkComponent.generated.h"

// The floating red YMIR logo shown over admin (GM) heads - the old client's GM mark, affect slot 0
// (EFFECT_AFFECT+0, "<locale>/effect/gm.mse" billboarding ymirred.tga on bone Bip01). Built entirely
// in native code so no Widget Blueprint asset is required.
UCLASS()
class METIN2_API UMT2GMMarkWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<class UImage> MarkImage;

	float AnimationTime = 0.0f;
};

UCLASS(ClassGroup = "MT2", meta = (BlueprintSpawnableComponent))
class METIN2_API UMT2GMMarkComponent : public UWidgetComponent
{
	GENERATED_BODY()

public:
	UMT2GMMarkComponent();

	// Called by the centralized nameplate scheduler so the GM mark uses exactly the same range.
	void RefreshFromNameplateVisibility(bool bNameplateVisible);

protected:
	virtual void BeginPlay() override;

private:
	void UpdateAttachmentHeight();
};
