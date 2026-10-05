/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "UI/MT2MapViewWidget.h"
#include "MT2FullMapWidget.generated.h"

class UButton;
class UBorder;
class UTextBlock;
class AMT2Party;
class AMT2PlayerState;

UCLASS()
class METIN2_API UMT2FullMapWidget : public UMT2MapViewWidget
{
	GENERATED_BODY()

public:
	void ToggleWindow();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	// The full map keeps its own zoom/pan (mouse wheel) instead of always refitting the whole map.
	virtual void UpdateViewTransform() override;
	virtual void SetViewTransform(float Scale, const FVector2D& Translation) override;
	virtual bool UsesLiveActorMarkers() const override { return false; }
	virtual bool ShouldDisplayActor(const AActor* Actor) const override;
	virtual bool AreMarkersInteractive() const override { return true; }
	virtual float GetMarkerScreenSize(const AActor* Actor) const override { return 8.0f; }
	virtual FText GetMarkerTooltip(const AActor* Actor) const override;
	virtual FLinearColor GetMarkerColor(const AActor* Actor) const override;

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> CloseButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> MapNameText;

private:
	UFUNCTION() void CloseWindow();
	void BuildNpcMarkers();
	// Repaints quest-target NPC markers so they flash like the minimap's.
	void RefreshQuestTargetMarkers();
	void HandleNpcMarkersChanged();
	void RefreshPartyBinding();
	void RefreshPartyMarkers();
	void RefreshLocalPlayerMarker();
	void UnbindParty();
	UFUNCTION() void HandlePartyChanged(AMT2Party* OldParty, AMT2Party* NewParty);
	UFUNCTION() void HandlePartyMembersChanged();
	bool bDragging = false;      // dragging the window by its title bar
	bool bPanningMap = false;    // left-drag on the map body pans the map
	FVector2D DragStartMouse = FVector2D::ZeroVector;
	FVector2D DragStartTranslation = FVector2D::ZeroVector;

	// Persistent map zoom/pan. Initialized to fit-the-whole-map when the window opens, then driven by
	// the mouse wheel. FitScale is the lower zoom bound (never smaller than the whole map).
	float MapScale = 0.0f;
	FVector2D MapTranslation = FVector2D::ZeroVector;
	float FitScale = 1.0f;
	bool bViewInitialized = false;
	bool bNpcMarkersBuilt = false;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> NpcMarkerWidgets;

	// Vnum per NPC marker (index-aligned with NpcMarkerWidgets) so quest targets can be recoloured
	// each tick. The full map draws from a replicated snapshot rather than live actors, so it cannot
	// go through the actor-based marker colouring the minimap uses.
	TArray<int32> NpcMarkerVnums;
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UBorder>> PartyMarkerWidgets;
	UPROPERTY(Transient)
	TObjectPtr<UBorder> LocalPlayerMarker;
	TWeakObjectPtr<AMT2PlayerState> BoundPlayerState;
	TWeakObjectPtr<AMT2Party> BoundParty;
};
