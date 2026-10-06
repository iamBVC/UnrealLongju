/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2QuestArrowComponent.h"
#include "Config/MT2PathSettings.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Mobs/MT2Mob.h"
#include "Player/MT2PlayerState.h"
#include "Quests/MT2QuestManagerComponent.h"
#include "TimerManager.h"
#include "UI/MT2UIStyle.h"

namespace
{
	// Reuses the minimap atlas's arrow art, flipped to point down at the NPC. Swap this region (or the
	// texture) if dedicated quest-marker art is imported later.
	const TCHAR* ArrowTexturePath() { return UMT2PathSettings::Path(TEXT("ymir_work_ui_T_minimap")); }
	const FMT2AtlasRegion ArrowRegion(240.0f, 152.0f, 250.0f, 163.0f);
	constexpr float ArrowDrawSize = 34.0f;

}

TSharedRef<SWidget> UMT2QuestArrowWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UImage* Arrow = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("QuestArrow"));
		if (UTexture2D* Atlas = FMT2UIStyle::LoadTexture(ArrowTexturePath()))
		{
			FSlateBrush Brush = FMT2UIStyle::AtlasBrush(Atlas, ArrowRegion);
			Brush.ImageSize = FVector2D(ArrowDrawSize);
			Arrow->SetBrush(Brush);
		}
		// The atlas art points up; rotate it to point down at the NPC beneath.
		Arrow->SetRenderTransformPivot(FVector2D(0.5f));
		Arrow->SetRenderTransformAngle(180.0f);
		Arrow->SetColorAndOpacity(FLinearColor(1.0f, 0.9f, 0.25f, 1.0f));
		WidgetTree->RootWidget = Arrow;
	}
	return Super::RebuildWidget();
}

UMT2QuestArrowComponent::UMT2QuestArrowComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	// Screen-space so the arrow stays readable at any camera distance, like the nameplates.
	SetWidgetSpace(EWidgetSpace::Screen);
	SetDrawSize(FVector2D(ArrowDrawSize));
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	SetVisibility(false);
	// Purely cosmetic client-side decoration.
	SetIsReplicated(false);
}

void UMT2QuestArrowComponent::BeginPlay()
{
	Super::BeginPlay();

	// Dedicated servers have no viewer, so the arrow is never built there.
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		SetVisibility(false);
		return;
	}

	if (!GetWidget())
	{
		if (UMT2QuestArrowWidget* Arrow =
			CreateWidget<UMT2QuestArrowWidget>(World, UMT2QuestArrowWidget::StaticClass()))
		{
			SetWidget(Arrow);
		}
	}
	SetRelativeLocation(FVector(0.0f, 0.0f, HoverHeight));

	// Quest targets change rarely, so poll slowly instead of testing every frame.
	World->GetTimerManager().SetTimer(
		RefreshTimer, FTimerDelegate::CreateUObject(this, &UMT2QuestArrowComponent::RefreshTargetState),
		0.5f, true);
	RefreshTargetState();
}

bool UMT2QuestArrowComponent::IsQuestTargetForLocalPlayer() const
{
	const AMT2Mob* Mob = Cast<AMT2Mob>(GetOwner());
	const UWorld* World = GetWorld();
	if (!Mob || !World)
	{
		return false;
	}
	const APlayerController* Controller = World->GetFirstPlayerController();
	const AMT2PlayerState* State = Controller ? Controller->GetPlayerState<AMT2PlayerState>() : nullptr;
	const UMT2QuestManagerComponent* QuestManager = State ? State->GetQuestManagerComponent() : nullptr;
	return QuestManager && QuestManager->IsQuestTargetActor(Mob);
}

void UMT2QuestArrowComponent::RefreshTargetState()
{
	const bool bTarget = IsQuestTargetForLocalPlayer();
	if (bTarget == bIsQuestTarget)
	{
		return;
	}
	bIsQuestTarget = bTarget;
	SetVisibility(bTarget);
	if (bTarget)
	{
		SetComponentTickEnabled(true);
		bPendingTickDisable = false;
	}
	else
	{
		// Let it tick once more so the on-screen widget is actually removed, then stop.
		bPendingTickDisable = true;
	}
}

void UMT2QuestArrowComponent::TickComponent(
	float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bIsQuestTarget)
	{
		// The tick above has now taken the hidden widget off screen, so the tick can stop.
		if (bPendingTickDisable)
		{
			bPendingTickDisable = false;
			SetComponentTickEnabled(false);
		}
		return;
	}
	BobTime += DeltaTime * BobSpeed;
	SetRelativeLocation(FVector(0.0f, 0.0f, HoverHeight + FMath::Sin(BobTime) * BobAmplitude));
}
