/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2MinimapWidget.h"

#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Game/MT2GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "World/MT2MapPresentationActor.h"

void UMT2MinimapWidget::NativeConstruct()
{
	Super::NativeConstruct();
	ScaleUpButton->OnClicked.AddUniqueDynamic(this, &UMT2MinimapWidget::ZoomIn);
	ScaleDownButton->OnClicked.AddUniqueDynamic(this, &UMT2MinimapWidget::ZoomOut);
	HideButton->OnClicked.AddUniqueDynamic(this, &UMT2MinimapWidget::HideMinimap);
	ShowButton->OnClicked.AddUniqueDynamic(this, &UMT2MinimapWidget::ShowMinimap);
	FullMapButton->OnClicked.AddUniqueDynamic(this, &UMT2MinimapWidget::RequestFullMap);
	CloseWindow->SetVisibility(ESlateVisibility::Collapsed);
	RefreshLocationText();
}

FVector2D UMT2MinimapWidget::GetViewCenter() const
{
	// Pin the player to the on-screen player arrow (the visible centre of the minimap) instead of the
	// map-canvas geometric centre. They diverge when the minimap is resized, which makes zooming drift
	// off the arrow toward a corner. MapCanvas sits inside a RetainerBox (a separate render space), so
	// converting the arrow's absolute position into it is unreliable (it blanked the map before). Instead
	// compute the arrow's centre in MapCanvas-local space from the canvas-panel layout offsets, which is
	// both retainer-safe and resize-safe (it reads whatever the current Blueprint layout is).
	const UCanvasPanelSlot* ArrowSlot = PlayerArrow ? Cast<UCanvasPanelSlot>(PlayerArrow->Slot) : nullptr;
	const UPanelWidget* ArrowParent = PlayerArrow ? PlayerArrow->GetParent() : nullptr;
	if (ArrowSlot && ArrowParent && MapCanvas)
	{
		// Sum the canvas-panel offsets from MapCanvas up to the arrow's shared parent. Non-canvas parents
		// (the RetainerBox that wraps the map) contribute nothing, which is exactly right since their
		// child fills them. This yields MapCanvas's origin expressed in the arrow's parent space.
		FVector2D MapOrigin = FVector2D::ZeroVector;
		bool bReachedArrowParent = false;
		for (UWidget* Widget = MapCanvas; Widget; Widget = Widget->GetParent())
		{
			if (const UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Widget->Slot))
			{
				MapOrigin += CanvasSlot->GetPosition();
			}
			if (Widget->GetParent() == ArrowParent)
			{
				bReachedArrowParent = true;
				break;
			}
		}
		if (bReachedArrowParent)
		{
			const FVector2D ArrowCenter = ArrowSlot->GetPosition() + ArrowSlot->GetSize() * 0.5f;
			return ArrowCenter - MapOrigin;
		}
	}
	return Super::GetViewCenter();
}

void UMT2MinimapWidget::ZoomIn() { ViewDiameterWorld = FMath::Max(7500.0f, ViewDiameterWorld * 0.75f); }
void UMT2MinimapWidget::ZoomOut() { ViewDiameterWorld = FMath::Min(30000.0f, ViewDiameterWorld / 0.75f); }
void UMT2MinimapWidget::HideMinimap()
{
	OpenWindow->SetVisibility(ESlateVisibility::Collapsed);
	CloseWindow->SetVisibility(ESlateVisibility::Visible);
}
void UMT2MinimapWidget::ShowMinimap()
{
	CloseWindow->SetVisibility(ESlateVisibility::Collapsed);
	OpenWindow->SetVisibility(ESlateVisibility::Visible);
}
void UMT2MinimapWidget::RequestFullMap() { OnFullMapRequested.Broadcast(); }

bool UMT2MinimapWidget::ShouldDisplayActor(const AActor* Actor) const
{
	const APawn* PlayerPawn = GetOwningPlayerPawn();
	return PlayerPawn && Actor != PlayerPawn &&
		FVector::DistSquared2D(PlayerPawn->GetActorLocation(), Actor->GetActorLocation()) <=
		FMath::Square(ViewDiameterWorld * 0.5f);
}

void UMT2MinimapWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (const APawn* PlayerPawn = GetOwningPlayerPawn())
	{
		// The map projection flips both X and Y, so a heading's screen direction is (-cos yaw, -sin yaw).
		// Arrow art points up (north); screen angle = yaw - 90 keeps the arrow pointing along the heading
		// in the fully X/Y-mirrored map.
		PlayerArrow->SetRenderTransformAngle(PlayerPawn->GetActorRotation().Yaw - 90.0f);
	}
	if (InDeltaTime > 0.0f)
	{
		const float InstantFps = 1.0f / InDeltaTime;
		SmoothedFps = SmoothedFps <= 0.0f ? InstantFps : FMath::Lerp(SmoothedFps, InstantFps, 0.1f);
	}
	LocationRefreshTimeRemaining -= InDeltaTime;
	if (LocationRefreshTimeRemaining <= 0.0f)
	{
		LocationRefreshTimeRemaining = 0.2f;
		RefreshLocationText();
	}
}

void UMT2MinimapWidget::RefreshLocationText()
{
	if (!LocationText) return;

	const APawn* PlayerPawn = GetOwningPlayerPawn();
	const AMT2GameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState<AMT2GameStateBase>() : nullptr;
	FString MapName;
	if (const AMT2MapPresentationActor* MapPresentation = GetPresentation())
	{
		MapName = MapPresentation->MapName;
	}
	if (MapName.IsEmpty() && GameState) MapName = GameState->GetMapId();
	if (MapName.IsEmpty()) MapName = TEXT("Unknown Map");

	const FVector Location = PlayerPawn ? PlayerPawn->GetActorLocation() : FVector::ZeroVector;

	// Ping from the local player's PlayerState (compressed ping is in 4ms units; the helper returns ms).
	int32 PingMs = 0;
	if (const APlayerController* PC = GetOwningPlayer())
	{
		if (const APlayerState* PlayerState = PC->PlayerState)
		{
			PingMs = FMath::RoundToInt(PlayerState->GetPingInMilliseconds());
		}
	}

	LocationText->SetText(FText::FromString(FString::Printf(
		TEXT("%s  CH %d\nX %.0f  Y %.0f  Z %.0f\nFPS %d  Ping %d ms"),
		*MapName, GameState ? GameState->GetServerChannel() : 1,
		Location.X, Location.Y, Location.Z,
		FMath::RoundToInt(SmoothedFps), PingMs)));
}
