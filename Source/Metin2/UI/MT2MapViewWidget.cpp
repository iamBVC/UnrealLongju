/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2MapViewWidget.h"

#include "Characters/MT2PlayerCharacter.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "Mobs/MT2Mob.h"
#include "Npcs/MT2Npc.h"
#include "Player/MT2PlayerState.h"
#include "Quests/MT2QuestManagerComponent.h"
#include "World/MT2MapPresentationActor.h"
#include "World/MT2Portal.h"

namespace
{
	constexpr float TileLogicalSize = 128.0f;
}

void UMT2MapViewWidget::NativeConstruct()
{
	Super::NativeConstruct();
	MapCanvas->SetRenderTransformPivot(FVector2D::ZeroVector);
	RefreshPresentation();
	if (UsesLiveActorMarkers())
	{
		RefreshMarkers();
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(MarkerRefreshTimer, this,
				&UMT2MapViewWidget::RefreshMarkers, MarkerRefreshInterval, true);
		}
	}
}

void UMT2MapViewWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(MarkerRefreshTimer);
	MarkerWidgets.Reset();
	Super::NativeDestruct();
}

void UMT2MapViewWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (IsPlayerCentered()) UpdateViewTransform();
}

void UMT2MapViewWidget::RefreshPresentation()
{
	AMT2MapPresentationActor* Found = nullptr;
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AMT2MapPresentationActor> It(World); It; ++It)
		{
			Found = *It;
			break;
		}
	}
	if (Presentation.Get() == Found) return;
	Presentation = Found;
	MarkerWidgets.Reset();
	MapCanvas->ClearChildren();
	AddMapTiles();
	UpdateViewTransform();
}

void UMT2MapViewWidget::AddMapTiles()
{
	const AMT2MapPresentationActor* Data = Presentation.Get();
	if (!Data || Data->MapCells.X <= 0 || Data->MapCells.Y <= 0) return;
	LogicalMapSize = FVector2D(Data->MapCells.X * TileLogicalSize, Data->MapCells.Y * TileLogicalSize);

	for (const FMT2MinimapTile& Tile : Data->MinimapTiles)
	{
		UTexture2D* Texture = Tile.Texture.LoadSynchronous();
		if (!Texture) continue;
		UImage* Image = NewObject<UImage>(MapCanvas);
		FSlateBrush Brush;
		Brush.SetResourceObject(Texture);
		Brush.ImageSize = FVector2D(TileLogicalSize);
		Image->SetBrush(Brush);
		Image->SetVisibility(ESlateVisibility::HitTestInvisible);
		UCanvasPanelSlot* CanvasSlot = MapCanvas->AddChildToCanvas(Image);
		// The whole map view is mirrored east-west: the tile column is mirrored here
		// (MapCells.X - 1 - Cell.X) and the markers get the matching X mirror in WorldToMap, so tiles and
		// markers flip together and stay aligned. No per-tile image flip: mirroring the layout already
		// reverses within-tile content as part of the single full-view horizontal flip.
		CanvasSlot->SetPosition(FVector2D(Data->MapCells.X - 1 - Tile.Cell.X, Tile.Cell.Y) * TileLogicalSize);
		CanvasSlot->SetSize(FVector2D(TileLogicalSize));
	}
}

FVector2D UMT2MapViewWidget::WorldToMap(const FVector& WorldLocation) const
{
	const AMT2MapPresentationActor* Data = Presentation.Get();
	if (!Data) return FVector2D::ZeroVector;
	const FVector2D Extent = Data->WorldMax - Data->WorldMin;
	if (Extent.X <= UE_SMALL_NUMBER || Extent.Y <= UE_SMALL_NUMBER) return FVector2D::ZeroVector;
	// Both axes are mirrored: X so the whole map view is flipped east-west (matching the mirrored tile
	// layout in AddMapTiles), Y so north is up. Tiles and markers carry the same X mirror, so they flip
	// together and stay aligned.
	const float U = (Data->WorldMax.X - WorldLocation.X) / Extent.X;
	const float V = (Data->WorldMax.Y - WorldLocation.Y) / Extent.Y;
	return FVector2D(U * LogicalMapSize.X, V * LogicalMapSize.Y);
}

bool UMT2MapViewWidget::IsQuestTargetActor(const AActor* Actor) const
{
	const APlayerController* Controller = GetOwningPlayer();
	const AMT2PlayerState* State = Controller ? Controller->GetPlayerState<AMT2PlayerState>() : nullptr;
	const UMT2QuestManagerComponent* QuestManager = State ? State->GetQuestManagerComponent() : nullptr;
	return QuestManager && QuestManager->IsQuestTargetActor(Actor);
}

FLinearColor UMT2MapViewWidget::GetQuestTargetFlashColor() const
{
	// Alternates red and white ~2x a second so a quest target stands out from the static green NPC dots,
	// the way the old game's blinking marker did.
	const UWorld* World = GetWorld();
	const float Time = World ? World->GetTimeSeconds() : 0.0f;
	const float Blend = 0.5f + 0.5f * FMath::Sin(Time * UE_PI * 4.0f);
	return FMath::Lerp(FLinearColor(1.0f, 0.05f, 0.05f, 1.0f), FLinearColor::White, Blend);
}

FLinearColor UMT2MapViewWidget::GetMarkerColor(const AActor* Actor) const
{
	// Quest targets take priority over the normal per-type colour.
	if (IsQuestTargetActor(Actor))
	{
		return GetQuestTargetFlashColor();
	}
	if (Actor->IsA<AMT2PlayerCharacter>())
	{
		return IsLocalPartyMember(Actor)
			? FLinearColor(0.0f, 0.9f, 1.0f, 1.0f)
			: FLinearColor(1.0f, 0.82f, 0.05f, 1.0f);
	}
	if (Actor->IsA<AMT2Portal>()) return FLinearColor(0.1f, 0.9f, 0.15f, 1.0f);
	if (Actor->IsA<AMT2Npc>()) return FLinearColor(0.1f, 0.9f, 0.15f, 1.0f);
	if (const AMT2Mob* Mob = Cast<AMT2Mob>(Actor))
	{
		if (Mob->GetMobRank() == EMT2MobRank::Boss || Mob->GetMobRank() == EMT2MobRank::King)
			return FLinearColor(0.72f, 0.15f, 0.95f, 1.0f);
		if (Mob->IsMetinStone()) return FLinearColor(1.0f, 0.38f, 0.02f, 1.0f);
		return FLinearColor(0.95f, 0.05f, 0.03f, 1.0f);
	}
	return FLinearColor::Transparent;
}

bool UMT2MapViewWidget::IsLocalPartyMember(const AActor* Actor) const
{
	const AMT2PlayerCharacter* Player = Cast<AMT2PlayerCharacter>(Actor);
	const AMT2PlayerState* TargetState = Player
		? Player->GetPlayerState<AMT2PlayerState>() : nullptr;
	const AMT2PlayerState* LocalState = GetOwningPlayerState<AMT2PlayerState>();
	if (!LocalState || !TargetState || TargetState == LocalState)
	{
		return false;
	}

	for (const FMT2PartyMemberData& Member : LocalState->GetPartyMemberSnapshot())
	{
		if (Member.PlayerState == TargetState ||
			(Member.PlayerId != INDEX_NONE && Member.PlayerId == TargetState->GetPlayerId()))
		{
			return true;
		}
	}
	return false;
}

float UMT2MapViewWidget::GetMarkerScreenSize(const AActor* Actor) const
{
	return Actor && Actor->IsA<AMT2PlayerCharacter>() ? 5.0f : 4.0f;
}

void UMT2MapViewWidget::RefreshMarkers()
{
	if (!IsVisible()) return;
	if (!Presentation.IsValid()) RefreshPresentation();
	if (!Presentation.IsValid() || !GetWorld()) return;

	TSet<TWeakObjectPtr<AActor>> Seen;
	auto AddMarker = [this, &Seen](AActor* Actor)
	{
		if (!ShouldDisplayActor(Actor)) return;
		const FLinearColor Color = GetMarkerColor(Actor);
		if (Color.A <= 0.0f || Actor->IsHidden()) return;
		Seen.Add(Actor);
		TObjectPtr<UBorder>& Marker = MarkerWidgets.FindOrAdd(Actor);
		if (!Marker)
		{
			Marker = NewObject<UBorder>(MapCanvas);
			Marker->SetVisibility(AreMarkersInteractive()
				? ESlateVisibility::Visible : ESlateVisibility::HitTestInvisible);
			if (AreMarkersInteractive()) Marker->SetToolTipText(GetMarkerTooltip(Actor));
			UCanvasPanelSlot* CanvasSlot = MapCanvas->AddChildToCanvas(Marker);
			CanvasSlot->SetSize(FVector2D(GetMarkerScreenSize(Actor)));
			CanvasSlot->SetAlignment(FVector2D(0.5f));
		}
		// Party membership can arrive after the player marker was created.
		Marker->SetBrushColor(Color);
		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Marker->Slot))
		{
			CanvasSlot->SetPosition(WorldToMap(Actor->GetActorLocation()));
		}
	};
	for (TActorIterator<AMT2Mob> It(GetWorld()); It; ++It)
	{
		AddMarker(*It);
	}
	for (TActorIterator<AMT2PlayerCharacter> It(GetWorld()); It; ++It)
	{
		AddMarker(*It);
	}
	for (TActorIterator<AMT2Portal> It(GetWorld()); It; ++It)
	{
		AddMarker(*It);
	}

	for (auto It = MarkerWidgets.CreateIterator(); It; ++It)
	{
		if (!Seen.Contains(It.Key()) || !It.Key().IsValid())
		{
			if (It.Value()) MapCanvas->RemoveChild(It.Value());
			It.RemoveCurrent();
		}
	}
	UpdateViewTransform();
}

FVector2D UMT2MapViewWidget::GetMapViewportSize() const
{
	if (MapCanvas)
	{
		const FVector2D Size = MapCanvas->GetCachedGeometry().GetLocalSize();
		if (Size.X > 1.0f && Size.Y > 1.0f) return Size;
	}
	return FVector2D(400.0f, 400.0f);
}

FVector2D UMT2MapViewWidget::GetViewCenter() const
{
	return GetMapViewportSize() * 0.5f;
}

float UMT2MapViewWidget::ComputeFitScale(const FVector2D& Viewport) const
{
	if (LogicalMapSize.X <= 0.0f || LogicalMapSize.Y <= 0.0f) return 1.0f;
	return FMath::Min(Viewport.X / LogicalMapSize.X, Viewport.Y / LogicalMapSize.Y);
}

void UMT2MapViewWidget::SetViewTransform(float Scale, const FVector2D& Translation)
{
	MapCanvas->SetRenderScale(FVector2D(Scale));
	MapCanvas->SetRenderTranslation(Translation);
	const float InverseScale = 1.0f / FMath::Max(Scale, UE_SMALL_NUMBER);
	for (const TPair<TWeakObjectPtr<AActor>, TObjectPtr<UBorder>>& Pair : MarkerWidgets)
	{
		if (Pair.Value)
		{
			if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Pair.Value->Slot))
			{
				const float ScreenSize = GetMarkerScreenSize(Pair.Key.Get());
				CanvasSlot->SetSize(FVector2D(ScreenSize * InverseScale));
			}
		}
	}
}

void UMT2MapViewWidget::UpdateViewTransform()
{
	if (!Presentation.IsValid() || LogicalMapSize.X <= 0.0f || LogicalMapSize.Y <= 0.0f) return;
	const FVector2D Viewport = GetMapViewportSize();
	float Scale = ComputeFitScale(Viewport);
	FVector2D Translation = (Viewport - LogicalMapSize * Scale) * 0.5f;

	if (IsPlayerCentered())
	{
		const APawn* Pawn = GetOwningPlayerPawn();
		const AMT2MapPresentationActor* Data = Presentation.Get();
		if (Pawn && Data)
		{
			const FVector2D WorldExtent = Data->WorldMax - Data->WorldMin;
			const float Diameter = FMath::Max(GetViewDiameterWorld(), 1.0f);
			Scale = FMath::Min(Viewport.X * WorldExtent.X / (Diameter * LogicalMapSize.X),
				Viewport.Y * WorldExtent.Y / (Diameter * LogicalMapSize.Y));
			// Center the player: put their map position exactly under the view centre (the arrow), so
			// zooming in/out keeps the same world point pinned there.
			Translation = GetViewCenter() - WorldToMap(Pawn->GetActorLocation()) * Scale;
		}
	}
	SetViewTransform(Scale, Translation);
}
