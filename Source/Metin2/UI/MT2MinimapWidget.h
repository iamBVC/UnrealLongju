/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "UI/MT2MapViewWidget.h"
#include "MT2MinimapWidget.generated.h"

class UButton;
class UCanvasPanel;
class UImage;
class UTextBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMT2MinimapFullMapRequested);

UCLASS()
class METIN2_API UMT2MinimapWidget : public UMT2MapViewWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Minimap")
	FMT2MinimapFullMapRequested OnFullMapRequested;

protected:
	virtual void NativeConstruct() override;
	virtual bool IsPlayerCentered() const override { return true; }
	virtual FVector2D GetViewCenter() const override;
	virtual float GetViewDiameterWorld() const override { return ViewDiameterWorld; }
	virtual bool ShouldDisplayActor(const AActor* Actor) const override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UCanvasPanel> OpenWindow;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UCanvasPanel> CloseWindow;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> ScaleUpButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> ScaleDownButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> HideButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> ShowButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> FullMapButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> PlayerArrow;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> LocationText;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Minimap", meta = (ClampMin = "1000", Units = "cm"))
	float ViewDiameterWorld = 30000.0f;

private:
	UFUNCTION() void ZoomIn();
	UFUNCTION() void ZoomOut();
	UFUNCTION() void HideMinimap();
	UFUNCTION() void ShowMinimap();
	UFUNCTION() void RequestFullMap();
	void RefreshLocationText();

	float LocationRefreshTimeRemaining = 0.0f;
	// Exponentially smoothed FPS so the readout doesn't flicker frame to frame.
	float SmoothedFps = 0.0f;
};
