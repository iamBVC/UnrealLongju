/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2GMMarkComponent.h"
#include "Config/MT2PathSettings.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CapsuleComponent.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "Player/MT2PlayerState.h"

namespace
{
	// locale/en/effect/ymirred.tga - the texture gm.mse billboards over GM heads.
	FSoftObjectPath GMMarkTexturePath() { return FSoftObjectPath(UMT2PathSettings::Path(TEXT("locale_en_effect_T_ymirred"))); }
}

TSharedRef<SWidget> UMT2GMMarkWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		MarkImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("MarkImage"));
		if (UTexture2D* MarkTexture = Cast<UTexture2D>(GMMarkTexturePath().TryLoad()))
		{
			MarkImage->SetBrushFromTexture(MarkTexture);
			MarkImage->SetDesiredSizeOverride(FVector2D(64.0f, 64.0f));
		}
		WidgetTree->RootWidget = MarkImage;
	}
	return Super::RebuildWidget();
}

void UMT2GMMarkWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!MarkImage)
	{
		return;
	}

	// Smooth ping-pong: the logo starts dim at the head, rises while brightening to full opacity at
	// the top, then sinks and dims back to 35% - one seamless cosine cycle, no snapping.
	constexpr float CycleDuration = 2.0f;
	constexpr float RiseDistance = 40.0f;
	AnimationTime = FMath::Fmod(AnimationTime + InDeltaTime, CycleDuration);
	const float Wave = 0.5f - 0.5f * FMath::Cos(AnimationTime / CycleDuration * 2.0f * UE_PI);

	MarkImage->SetRenderTranslation(FVector2D(0.0f, -RiseDistance * Wave));
	MarkImage->SetRenderOpacity(Wave);
	const float Scale = 1.0f + 0.06f * Wave;
	MarkImage->SetRenderScale(FVector2D(Scale, Scale));
}

UMT2GMMarkComponent::UMT2GMMarkComponent()
{
	SetWidgetSpace(EWidgetSpace::Screen);
	// Tall enough that the rising animation never clips at the top of the draw area.
	SetDrawSize(FVector2D(64.0f, 64.0f));
	SetPivot(FVector2D(0.5f, 1.0f));
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	SetWindowFocusable(false);
	SetWidgetClass(UMT2GMMarkWidget::StaticClass());
	SetHiddenInGame(true);
}

void UMT2GMMarkComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!GetOwner() || GetOwner()->GetNetMode() == NM_DedicatedServer)
	{
		SetHiddenInGame(true);
		return;
	}
	InitWidget();
	// Screen-space widget: keep it out of Slate hit testing so the floating mark never eats a
	// click-to-move (the NoCollision above only covers 3D world traces).
	if (UUserWidget* MarkWidgetObject = GetUserWidgetObject())
	{
		MarkWidgetObject->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	UpdateAttachmentHeight();
}

void UMT2GMMarkComponent::RefreshFromNameplateVisibility(bool bNameplateVisible)
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const AMT2PlayerState* State = Pawn ? Pawn->GetPlayerState<AMT2PlayerState>() : nullptr;
	const bool bShowMark = bNameplateVisible && State && State->IsAdmin();
	SetHiddenInGame(!bShowMark);
	if (bShowMark) UpdateAttachmentHeight();
}

void UMT2GMMarkComponent::UpdateAttachmentHeight()
{
	// gm.mse floats the mark just above the head (emission at Z~110 from the feet); the nameplate
	// already sits at the capsule top, so the mark hovers a bit higher to clear it.
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
		{
			SetRelativeLocation(FVector(0.0f, 0.0f, Capsule->GetScaledCapsuleHalfHeight() + 30.0f));
		}
	}
}
