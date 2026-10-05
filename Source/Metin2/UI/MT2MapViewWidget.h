/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "UI/MT2UserWidget.h"
#include "MT2MapViewWidget.generated.h"

class AMT2MapPresentationActor;
class UBorder;
class UCanvasPanel;

UCLASS(Abstract)
class METIN2_API UMT2MapViewWidget : public UMT2UserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	void RefreshPresentation();
	void RefreshMarkers();
	virtual void UpdateViewTransform();
	// Applies a pan/zoom to the map canvas and keeps the markers at a constant on-screen size.
	virtual void SetViewTransform(float Scale, const FVector2D& Translation);
	// Uniform scale that fits the whole logical map inside the viewport.
	float ComputeFitScale(const FVector2D& Viewport) const;
	FVector2D GetLogicalMapSize() const { return LogicalMapSize; }
	virtual bool IsPlayerCentered() const { return false; }
	virtual bool UsesLiveActorMarkers() const { return true; }
	// The actual on-screen size of the map area; defaults to the map canvas geometry so it tracks
	// any Blueprint resize instead of a baked constant (which broke centering when resized).
	virtual FVector2D GetMapViewportSize() const;
	// The on-screen point (in map-canvas local space) the player should be pinned to when centering.
	// Defaults to the canvas centre; the minimap overrides this to the actual player-arrow position so
	// zooming stays anchored on the arrow even when the widget is resized and the arrow isn't dead-centre.
	virtual FVector2D GetViewCenter() const;
	virtual float GetViewDiameterWorld() const { return 0.0f; }
	virtual bool ShouldDisplayActor(const AActor* Actor) const { return true; }
	virtual bool AreMarkersInteractive() const { return false; }
	virtual float GetMarkerScreenSize(const AActor* Actor) const;
	virtual FText GetMarkerTooltip(const AActor* Actor) const { return FText::GetEmpty(); }
	virtual FLinearColor GetMarkerColor(const AActor* Actor) const;
	// True when an active quest points the local player at this actor (drives the flashing marker).
	bool IsQuestTargetActor(const AActor* Actor) const;
	// Red/white alternating colour used for quest targets on both maps.
	FLinearColor GetQuestTargetFlashColor() const;
	bool IsLocalPartyMember(const AActor* Actor) const;
	AMT2MapPresentationActor* GetPresentation() const { return Presentation.Get(); }
	FVector2D WorldToMap(const FVector& WorldLocation) const;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCanvasPanel> MapCanvas;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Map", meta = (ClampMin = "0.05", Units = "s"))
	float MarkerRefreshInterval = 0.25f;

private:
	void AddMapTiles();

	TWeakObjectPtr<AMT2MapPresentationActor> Presentation;
	TMap<TWeakObjectPtr<AActor>, TObjectPtr<UBorder>> MarkerWidgets;
	FTimerHandle MarkerRefreshTimer;
	FVector2D LogicalMapSize = FVector2D::ZeroVector;
};
