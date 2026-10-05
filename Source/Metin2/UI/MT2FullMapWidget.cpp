/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2FullMapWidget.h"

#include "Components/Button.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Quests/MT2QuestManagerComponent.h"
#include "World/MT2MapPresentationActor.h"
#include "Mobs/MT2Mob.h"
#include "Party/MT2Party.h"
#include "Player/MT2PlayerController.h"
#include "Player/MT2PlayerState.h"

void UMT2FullMapWidget::NativeConstruct()
{
	Super::NativeConstruct();
	CloseButton->OnClicked.AddUniqueDynamic(this, &UMT2FullMapWidget::CloseWindow);
	if (const AMT2MapPresentationActor* Data = GetPresentation())
	{
		MapNameText->SetText(FText::FromString(
			Data->MapName.IsEmpty() ? Data->MapId : Data->MapName));
	}
	if (AMT2PlayerController* Controller = Cast<AMT2PlayerController>(GetOwningPlayer()))
	{
		Controller->OnFullMapNpcMarkersChanged.AddUObject(
			this, &UMT2FullMapWidget::HandleNpcMarkersChanged);
	}
	BuildNpcMarkers();
	RefreshPartyBinding();
	RefreshLocalPlayerMarker();
	SetVisibility(ESlateVisibility::Collapsed);
}

void UMT2FullMapWidget::NativeDestruct()
{
	if (AMT2PlayerController* Controller = Cast<AMT2PlayerController>(GetOwningPlayer()))
	{
		Controller->OnFullMapNpcMarkersChanged.RemoveAll(this);
	}
	UnbindParty();
	if (AMT2PlayerState* State = BoundPlayerState.Get())
	{
		State->OnPartyChanged.RemoveDynamic(this, &UMT2FullMapWidget::HandlePartyChanged);
		State->OnPartyMemberSnapshotChanged.RemoveDynamic(
			this, &UMT2FullMapWidget::HandlePartyMembersChanged);
	}
	BoundPlayerState.Reset();
	NpcMarkerWidgets.Reset();
	PartyMarkerWidgets.Reset();
	LocalPlayerMarker = nullptr;
	Super::NativeDestruct();
}

void UMT2FullMapWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshPartyBinding();
	RefreshLocalPlayerMarker();
	RefreshQuestTargetMarkers();
}

void UMT2FullMapWidget::HandleNpcMarkersChanged()
{
	bNpcMarkersBuilt = false;
	NpcMarkerVnums.Reset();
	for (UBorder* Marker : NpcMarkerWidgets)
	{
		if (Marker && MapCanvas)
		{
			MapCanvas->RemoveChild(Marker);
		}
	}
	NpcMarkerWidgets.Reset();
	BuildNpcMarkers();
}

void UMT2FullMapWidget::ToggleWindow()
{
	SetVisibility(IsVisible() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	if (IsVisible())
	{
		// Re-fit the whole map to the frame each time it's opened.
		bViewInitialized = false;
		RefreshPresentation();
		UpdateViewTransform();
		BuildNpcMarkers();
		RefreshPartyBinding();
		RefreshPartyMarkers();
		RefreshLocalPlayerMarker();
	}
}

void UMT2FullMapWidget::RefreshPartyBinding()
{
	AMT2PlayerState* State = GetOwningPlayerState<AMT2PlayerState>();
	if (BoundPlayerState.Get() != State)
	{
		if (AMT2PlayerState* Previous = BoundPlayerState.Get())
		{
			Previous->OnPartyChanged.RemoveDynamic(this, &UMT2FullMapWidget::HandlePartyChanged);
			Previous->OnPartyMemberSnapshotChanged.RemoveDynamic(
				this, &UMT2FullMapWidget::HandlePartyMembersChanged);
		}
		BoundPlayerState = State;
		if (State)
		{
			State->OnPartyChanged.AddUniqueDynamic(this, &UMT2FullMapWidget::HandlePartyChanged);
			State->OnPartyMemberSnapshotChanged.AddUniqueDynamic(
				this, &UMT2FullMapWidget::HandlePartyMembersChanged);
		}
		RefreshPartyMarkers();
	}

	AMT2Party* Party = State ? State->GetParty() : nullptr;
	if (BoundParty.Get() != Party)
	{
		UnbindParty();
		BoundParty = Party;
		if (Party)
		{
			Party->OnMembersChanged.AddUniqueDynamic(
				this, &UMT2FullMapWidget::HandlePartyMembersChanged);
		}
		RefreshPartyMarkers();
	}
}

void UMT2FullMapWidget::UnbindParty()
{
	if (AMT2Party* Party = BoundParty.Get())
	{
		Party->OnMembersChanged.RemoveDynamic(
			this, &UMT2FullMapWidget::HandlePartyMembersChanged);
	}
	BoundParty.Reset();
}

void UMT2FullMapWidget::RefreshPartyMarkers()
{
	if (!MapCanvas || !GetPresentation())
	{
		return;
	}
	const AMT2PlayerState* LocalState = BoundPlayerState.Get();
	TSet<FString> ActiveKeys;
	if (LocalState)
	{
		for (const FMT2PartyMemberData& Member : LocalState->GetPartyMemberSnapshot())
		{
			if (!Member.bHasWorldLocation || Member.PlayerState == LocalState ||
				(Member.PlayerId != INDEX_NONE && LocalState &&
					Member.PlayerId == LocalState->GetPlayerId()))
			{
				continue;
			}
			const FString Key = !Member.CharacterName.IsEmpty()
				? Member.CharacterName : FString::FromInt(Member.PlayerId);
			ActiveKeys.Add(Key);
			TObjectPtr<UBorder>& Marker = PartyMarkerWidgets.FindOrAdd(Key);
			if (!Marker)
			{
				Marker = NewObject<UBorder>(MapCanvas);
				Marker->SetBrushColor(FLinearColor(0.0f, 0.9f, 1.0f, 1.0f));
				Marker->SetVisibility(ESlateVisibility::Visible);
				UCanvasPanelSlot* MarkerCanvasSlot = MapCanvas->AddChildToCanvas(Marker);
				MarkerCanvasSlot->SetAlignment(FVector2D(0.5f));
			}
			Marker->SetToolTipText(FText::FromString(Member.CharacterName));
			if (UCanvasPanelSlot* MarkerCanvasSlot = Cast<UCanvasPanelSlot>(Marker->Slot))
			{
				MarkerCanvasSlot->SetPosition(WorldToMap(Member.WorldLocation));
				MarkerCanvasSlot->SetSize(FVector2D(
					8.0f / FMath::Max(MapScale, UE_SMALL_NUMBER)));
			}
		}
	}

	for (auto It = PartyMarkerWidgets.CreateIterator(); It; ++It)
	{
		if (!ActiveKeys.Contains(It.Key()))
		{
			if (It.Value()) MapCanvas->RemoveChild(It.Value());
			It.RemoveCurrent();
		}
	}
}

void UMT2FullMapWidget::HandlePartyChanged(AMT2Party*, AMT2Party*)
{
	RefreshPartyBinding();
}

void UMT2FullMapWidget::HandlePartyMembersChanged()
{
	RefreshPartyMarkers();
}

void UMT2FullMapWidget::RefreshLocalPlayerMarker()
{
	if (!MapCanvas || !GetPresentation())
	{
		return;
	}

	const APawn* PlayerPawn = GetOwningPlayerPawn();
	if (!PlayerPawn)
	{
		if (LocalPlayerMarker)
		{
			LocalPlayerMarker->SetVisibility(ESlateVisibility::Collapsed);
		}
		return;
	}

	if (!LocalPlayerMarker)
	{
		LocalPlayerMarker = NewObject<UBorder>(MapCanvas);
		LocalPlayerMarker->SetBrushColor(FLinearColor(1.0f, 0.82f, 0.05f, 1.0f));
		LocalPlayerMarker->SetVisibility(ESlateVisibility::Visible);
		if (const AMT2PlayerState* State = GetOwningPlayerState<AMT2PlayerState>())
		{
			LocalPlayerMarker->SetToolTipText(FText::FromString(State->GetCharacterName()));
		}
		UCanvasPanelSlot* MarkerSlot = MapCanvas->AddChildToCanvas(LocalPlayerMarker);
		MarkerSlot->SetAlignment(FVector2D(0.5f));
		MarkerSlot->SetZOrder(10);
	}

	LocalPlayerMarker->SetVisibility(ESlateVisibility::Visible);
	if (UCanvasPanelSlot* MarkerSlot = Cast<UCanvasPanelSlot>(LocalPlayerMarker->Slot))
	{
		MarkerSlot->SetPosition(WorldToMap(PlayerPawn->GetActorLocation()));
		MarkerSlot->SetSize(FVector2D(8.0f / FMath::Max(MapScale, UE_SMALL_NUMBER)));
	}
}

void UMT2FullMapWidget::BuildNpcMarkers()
{
	if (bNpcMarkersBuilt || !MapCanvas || !GetPresentation())
	{
		return;
	}
	const AMT2PlayerController* Controller = Cast<AMT2PlayerController>(GetOwningPlayer());
	if (!Controller)
	{
		return;
	}

	const TArray<FMT2FullMapNpcMarker>& MarkerData = Controller->GetFullMapNpcMarkers();
	for (const FMT2FullMapNpcMarker& Data : MarkerData)
	{
		UBorder* Marker = NewObject<UBorder>(MapCanvas);
		Marker->SetBrushColor(FLinearColor(0.1f, 0.9f, 0.15f, 1.0f));
		NpcMarkerVnums.Add(Data.Vnum);
		Marker->SetVisibility(ESlateVisibility::Visible);
		Marker->SetToolTipText(FText::FromString(Data.DisplayName));
		UCanvasPanelSlot* MarkerSlot = MapCanvas->AddChildToCanvas(Marker);
		MarkerSlot->SetPosition(WorldToMap(Data.Location));
		MarkerSlot->SetSize(FVector2D(8.0f / FMath::Max(MapScale, UE_SMALL_NUMBER)));
		MarkerSlot->SetAlignment(FVector2D(0.5f));
		NpcMarkerWidgets.Add(Marker);
	}
	bNpcMarkersBuilt = !MarkerData.IsEmpty();
}

void UMT2FullMapWidget::RefreshQuestTargetMarkers()
{
	const APlayerController* Controller = GetOwningPlayer();
	const AMT2PlayerState* State = Controller ? Controller->GetPlayerState<AMT2PlayerState>() : nullptr;
	const UMT2QuestManagerComponent* QuestManager = State ? State->GetQuestManagerComponent() : nullptr;
	if (!QuestManager)
	{
		return;
	}
	const FLinearColor FlashColour = GetQuestTargetFlashColor();
	for (int32 Index = 0; Index < NpcMarkerWidgets.Num(); ++Index)
	{
		UBorder* Marker = NpcMarkerWidgets[Index].Get();
		if (!Marker || !NpcMarkerVnums.IsValidIndex(Index))
		{
			continue;
		}
		Marker->SetBrushColor(QuestManager->IsQuestTargetVnum(NpcMarkerVnums[Index])
			? FlashColour : FLinearColor(0.1f, 0.9f, 0.15f, 1.0f));
	}
}

void UMT2FullMapWidget::UpdateViewTransform()
{
	if (!GetPresentation() || GetLogicalMapSize().X <= 0.0f || GetLogicalMapSize().Y <= 0.0f) return;
	const FVector2D Viewport = GetMapViewportSize();
	if (!bViewInitialized)
	{
		// Start showing the whole map centered in the (500x500) frame; wheel zoom takes over from here.
		FitScale = ComputeFitScale(Viewport);
		MapScale = FitScale;
		MapTranslation = (Viewport - GetLogicalMapSize() * MapScale) * 0.5f;
		bViewInitialized = true;
	}
	SetViewTransform(MapScale, MapTranslation);
}

void UMT2FullMapWidget::SetViewTransform(float Scale, const FVector2D& Translation)
{
	Super::SetViewTransform(Scale, Translation);
	const float MarkerSize = 8.0f / FMath::Max(Scale, UE_SMALL_NUMBER);
	for (UBorder* Marker : NpcMarkerWidgets)
	{
		if (Marker)
		{
			if (UCanvasPanelSlot* MarkerSlot = Cast<UCanvasPanelSlot>(Marker->Slot))
			{
				MarkerSlot->SetSize(FVector2D(MarkerSize));
			}
		}
	}
	for (const TPair<FString, TObjectPtr<UBorder>>& Pair : PartyMarkerWidgets)
	{
		if (Pair.Value)
		{
			if (UCanvasPanelSlot* MarkerSlot = Cast<UCanvasPanelSlot>(Pair.Value->Slot))
			{
				MarkerSlot->SetSize(FVector2D(MarkerSize));
			}
		}
	}
	if (LocalPlayerMarker)
	{
		if (UCanvasPanelSlot* MarkerSlot = Cast<UCanvasPanelSlot>(LocalPlayerMarker->Slot))
		{
			MarkerSlot->SetSize(FVector2D(MarkerSize));
		}
	}
}

FReply UMT2FullMapWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (!bViewInitialized)
	{
		UpdateViewTransform();
	}
	const FVector2D LocalCursor = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
	const float Factor = InMouseEvent.GetWheelDelta() > 0.0f ? 1.2f : 1.0f / 1.2f;
	// Never zoom out past the whole map; allow up to 8x in.
	const float NewScale = FMath::Clamp(MapScale * Factor, FitScale, FitScale * 8.0f);
	if (!FMath::IsNearlyEqual(NewScale, MapScale))
	{
		// Keep the map point under the cursor fixed while zooming.
		const FVector2D MapPoint = (LocalCursor - MapTranslation) / FMath::Max(MapScale, UE_SMALL_NUMBER);
		MapTranslation = LocalCursor - MapPoint * NewScale;
		MapScale = NewScale;
		SetViewTransform(MapScale, MapTranslation);
	}
	return FReply::Handled();
}

void UMT2FullMapWidget::CloseWindow() { SetVisibility(ESlateVisibility::Collapsed); }

bool UMT2FullMapWidget::ShouldDisplayActor(const AActor* Actor) const
{
	const AMT2Mob* Mob = Cast<AMT2Mob>(Actor);
	if (!Mob) return false;
	const EMT2MobType Type = Mob->GetMobType();
	return Type == EMT2MobType::NPC || Type == EMT2MobType::Warp || Type == EMT2MobType::Goto;
}

FText UMT2FullMapWidget::GetMarkerTooltip(const AActor* Actor) const
{
	if (const AMT2Mob* Mob = Cast<AMT2Mob>(Actor))
	{
		return FText::FromString(Mob->GetMobDisplayName());
	}
	return FText::GetEmpty();
}

FLinearColor UMT2FullMapWidget::GetMarkerColor(const AActor* Actor) const
{
	// The full map paints every NPC green, except the ones a quest is pointing at.
	if (IsQuestTargetActor(Actor))
	{
		return GetQuestTargetFlashColor();
	}
	return FLinearColor(0.1f, 0.9f, 0.15f, 1.0f);
}

FReply UMT2FullMapWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	const FVector2D Local = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (Local.Y <= 30.0f)
		{
			// Title bar: drag the whole window.
			bDragging = true;
			DragStartMouse = InMouseEvent.GetScreenSpacePosition();
			DragStartTranslation = GetRenderTransform().Translation;
			return FReply::Handled().CaptureMouse(TakeWidget());
		}
		// Map body: pan the map.
		bPanningMap = true;
		DragStartMouse = InMouseEvent.GetScreenSpacePosition();
		DragStartTranslation = MapTranslation;
		return FReply::Handled().CaptureMouse(TakeWidget());
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UMT2FullMapWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDragging && HasMouseCapture())
	{
		SetRenderTranslation(DragStartTranslation +
			(InMouseEvent.GetScreenSpacePosition() - DragStartMouse) / FMath::Max(InGeometry.Scale, UE_SMALL_NUMBER));
		return FReply::Handled();
	}
	if (bPanningMap && HasMouseCapture())
	{
		MapTranslation = DragStartTranslation +
			(InMouseEvent.GetScreenSpacePosition() - DragStartMouse) / FMath::Max(InGeometry.Scale, UE_SMALL_NUMBER);
		SetViewTransform(MapScale, MapTranslation);
		return FReply::Handled();
	}
	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

FReply UMT2FullMapWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if ((bDragging || bPanningMap) && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDragging = false;
		bPanningMap = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}
