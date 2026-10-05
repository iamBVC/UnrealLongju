/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2StatusEffectBarWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Characters/MT2CharacterBase.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/MT2StatusEffectComponent.h"
#include "Components/MT2StatusEffectDefinition.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Items/MT2ItemTemplate.h"
#include "Items/MT2ItemUtils.h"
#include "Player/MT2PlayerState.h"
#include "Skills/MT2SkillComponent.h"
#include "Skills/MT2SkillDefinition.h"
#include "TimerManager.h"

namespace
{
	constexpr float StatusIconSize = 48.0f;

	int64 MakeEffectVisualKey(const FMT2StatusEffect& Effect)
	{
		const uint32 Detail = Effect.SourceItemVnum > 0
			? static_cast<uint32>(Effect.SourceItemVnum) : 0u;
		return (static_cast<int64>(static_cast<uint32>(Effect.Type)) << 32) | Detail;
	}

	FText BuildEffectDescription(const UObject* Context, const FMT2StatusEffect& Effect,
		const UMT2StatusEffectDefinition& Definition)
	{
		const UMT2ItemTemplate* Item = Effect.SourceItemVnum > 0
			? MT2ItemUtils::ResolveTemplate(Context, Effect.SourceItemVnum) : nullptr;
		if (!Item)
		{
			return Definition.GetDescription(Effect);
		}

		const FText ItemName = MT2ItemUtils::GetDisplayName(Item, Effect.SourceItemVnum);
		const FText EffectValue = Definition.GetDescription(Effect);
		if (!Item->Description.IsEmpty())
		{
			if (Effect.ApplyType <= 0)
			{
				return FText::Format(NSLOCTEXT("MT2StatusEffects", "ItemTooltip", "{0}\n{1}"),
					ItemName, Item->Description);
			}
			return FText::Format(NSLOCTEXT("MT2StatusEffects", "ItemEffectTooltip", "{0}\n{1}\n{2}"),
				ItemName, Item->Description, EffectValue);
		}
		if (Effect.ApplyType <= 0)
		{
			return ItemName;
		}
		return FText::Format(NSLOCTEXT("MT2StatusEffects", "ItemEffectTooltipNoDescription", "{0}\n{1}"),
			ItemName, EffectValue);
	}
}

TSharedRef<SWidget> UMT2StatusEffectBarWidget::RebuildWidget()
{
	// Code-only usage: no Blueprint provides EffectsBox, so make one and use it as the root.
	if (!EffectsBox && WidgetTree)
	{
		EffectsBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("EffectsBox"));
		WidgetTree->RootWidget = EffectsBox;
	}
	return Super::RebuildWidget();
}

void UMT2StatusEffectBarWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (UMT2StatusEffectComponent* StatusEffects = ResolveStatusEffects())
	{
		BoundStatusEffects = StatusEffects;
		StatusEffects->OnStatusEffectsChanged.AddUniqueDynamic(this, &UMT2StatusEffectBarWidget::RefreshEffects);
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(RefreshTimer, this, &UMT2StatusEffectBarWidget::RefreshEffects, 1.0f, true);
	}
	RefreshEffects();
}

void UMT2StatusEffectBarWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimer);
	}
	if (BoundStatusEffects.IsValid())
	{
		BoundStatusEffects->OnStatusEffectsChanged.RemoveDynamic(this, &UMT2StatusEffectBarWidget::RefreshEffects);
	}
	Super::NativeDestruct();
}

UMT2StatusEffectComponent* UMT2StatusEffectBarWidget::ResolveStatusEffects() const
{
	const AMT2CharacterBase* Character = Cast<AMT2CharacterBase>(GetOwningPlayerPawn());
	return Character ? Character->GetStatusEffectComponent() : nullptr;
}

void UMT2StatusEffectBarWidget::RefreshEffects()
{
	if (!EffectsBox)
	{
		return;
	}

	// The pawn (and its component) may not have existed when we were constructed; pick it up lazily.
	UMT2StatusEffectComponent* StatusEffects = BoundStatusEffects.Get();
	if (!StatusEffects)
	{
		StatusEffects = ResolveStatusEffects();
		if (StatusEffects)
		{
			BoundStatusEffects = StatusEffects;
			StatusEffects->OnStatusEffectsChanged.AddUniqueDynamic(this, &UMT2StatusEffectBarWidget::RefreshEffects);
		}
	}

	TSet<int64> SeenKeys;
	if (StatusEffects)
	{
		for (const FMT2StatusEffect& Effect : StatusEffects->GetEffects())
		{
			// Multi-stat skills store one record per apply, but are one visible old-game affect.
			const int64 VisualKey = MakeEffectVisualKey(Effect);
			if (SeenKeys.Contains(VisualKey))
			{
				continue;
			}
			// Effects without a usable icon still apply normally, but must not reserve an empty
			// slot in the status-effect bar.
			TSoftObjectPtr<UTexture2D> EffectIcon = Effect.Icon;
			FVector4 EffectIconRegion = Effect.IconRegion;
			if (EffectIcon.IsNull() && Effect.SourceItemVnum > 0)
			{
				if (const UMT2ItemTemplate* Item =
					MT2ItemUtils::ResolveTemplate(this, Effect.SourceItemVnum))
				{
					EffectIcon = Item->Icon;
				}
			}
			if (EffectIcon.IsNull() && Effect.SourceItemVnum <= 0)
			{
				const AMT2PlayerState* PlayerState = GetOwningPlayerState<AMT2PlayerState>();
				const UMT2SkillComponent* Skills = PlayerState ? PlayerState->GetSkillComponent() : nullptr;
				const UMT2SkillDefinition* Skill = Skills ? Skills->FindSkillDefinition(Effect.Type) : nullptr;
				if (Skill)
				{
					EffectIcon = Skill->IconAtlas;
					const int32 Grade = FMath::Min(static_cast<int32>(Skills->GetSkillMastery(Effect.Type)), 2);
					const FMT2AtlasRect Region = Skill->GetIconRegion(Grade);
					EffectIconRegion = FVector4(Region.X, Region.Y, Region.Width, Region.Height);
				}
			}
			if (EffectIcon.IsNull())
			{
				continue;
			}
			UTexture2D* Texture = EffectIcon.LoadSynchronous();
			if (!Texture)
			{
				continue;
			}

			SeenKeys.Add(VisualKey);

			const UMT2StatusEffectDefinition* Definition = UMT2StatusEffectDefinition::Get(Effect.Kind);
			const FText Description = BuildEffectDescription(this, Effect, *Definition);
			FText Tooltip = Description;
			if (!Effect.IsInfinite())
			{
				const int32 Seconds = FMath::Max(0, Effect.RemainingSeconds);
				Tooltip = FText::FromString(FString::Printf(
					TEXT("%s - %d:%02d"), *Description.ToString(), Seconds / 60, Seconds % 60));
			}

			TObjectPtr<UOverlay>& Entry = EntriesByKey.FindOrAdd(VisualKey);
			if (!Entry)
			{
				Entry = NewObject<UOverlay>(this);

				// A SizeBox forces the icon geometry to StatusIconSize; sizing the image's brush/desired
				// size alone did not change the rendered size. The box hosts the hover tooltip (name +
				// remaining time); the image inside is hit-test invisible so the hover falls through to it.
				USizeBox* IconBox = NewObject<USizeBox>(this);
				IconBox->SetWidthOverride(StatusIconSize);
				IconBox->SetHeightOverride(StatusIconSize);
				IconBox->SetVisibility(ESlateVisibility::Visible);

				UImage* Image = NewObject<UImage>(this);
				Image->SetVisibility(ESlateVisibility::HitTestInvisible);
				FSlateBrush Brush;
				Brush.SetResourceObject(Texture);
				Brush.ImageSize = FVector2D(StatusIconSize);
				Brush.DrawAs = ESlateBrushDrawType::Image;
				// Skill icons are atlas sub-regions (potions use the whole texture, region W<=0). Map the
				// pixel region to normalized UVs so only that icon shows instead of the full atlas sheet.
				const float RegionWidth = EffectIconRegion.Z;
				const float RegionHeight = EffectIconRegion.W;
				const float TextureWidth = Texture->GetSizeX();
				const float TextureHeight = Texture->GetSizeY();
				if (RegionWidth > 0.0f && RegionHeight > 0.0f && TextureWidth > 0.0f && TextureHeight > 0.0f)
				{
					Brush.SetUVRegion(FBox2D(
						FVector2D(EffectIconRegion.X / TextureWidth, EffectIconRegion.Y / TextureHeight),
						FVector2D((EffectIconRegion.X + RegionWidth) / TextureWidth,
							(EffectIconRegion.Y + RegionHeight) / TextureHeight)));
				}
				Image->SetBrush(Brush);
				IconBox->AddChild(Image);
				Entry->AddChildToOverlay(IconBox);

				// Magnitude badge ("+30%", "+600") in the bottom-right corner, on top of the icon.
				UTextBlock* Badge = NewObject<UTextBlock>(this);
				Badge->SetVisibility(ESlateVisibility::HitTestInvisible);
				FSlateFontInfo Font = Badge->GetFont();
				Font.Size = 8;
				Badge->SetFont(Font);
				Badge->SetColorAndOpacity(FSlateColor(FLinearColor::White));
				Badge->SetShadowOffset(FVector2D(1.0f, 1.0f));
				Badge->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));
				if (UOverlaySlot* BadgeSlot = Entry->AddChildToOverlay(Badge))
				{
					BadgeSlot->SetHorizontalAlignment(HAlign_Center);
					BadgeSlot->SetVerticalAlignment(VAlign_Bottom);
				}

				if (UHorizontalBoxSlot* BoxSlot = EffectsBox->AddChildToHorizontalBox(Entry))
				{
					BoxSlot->SetPadding(FMargin(2.0f, 0.0f));
				}
			}

			// Child 0 = icon size box (the visible, hit-test host for the hover tooltip), child 1 = badge.
			// The image inside the box is hit-test invisible, so the tooltip must live on the box itself.
			if (UWidget* IconBox = Entry->GetChildAt(0))
			{
				IconBox->SetToolTipText(Tooltip);
			}
			if (UTextBlock* Badge = Cast<UTextBlock>(Entry->GetChildAt(1)))
			{
				Badge->SetText(Definition->GetMagnitudeLabel(Effect));
			}
		}
	}

	// Drop entries whose effect has expired.
	for (auto It = EntriesByKey.CreateIterator(); It; ++It)
	{
		if (!SeenKeys.Contains(It.Key()))
		{
			if (It.Value())
			{
				EffectsBox->RemoveChild(It.Value());
			}
			It.RemoveCurrent();
		}
	}
}
