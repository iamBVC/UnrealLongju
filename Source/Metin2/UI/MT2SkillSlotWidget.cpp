/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2SkillSlotWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Characters/MT2PlayerCharacter.h"
#include "Components/MT2StatusEffectComponent.h"
#include "Player/MT2PlayerState.h"
#include "Skills/MT2SkillComponent.h"
#include "Skills/MT2SkillDefinition.h"
#include "Skills/MT2SkillTypes.h"
#include "Stats/MT2CombatStatsComponent.h"
#include "Stats/MT2PrimaryStatsComponent.h"
#include "UI/MT2AtlasImage.h"
#include "UI/MT2SkillTooltipWidget.h"
#include "UI/MT2SlotEffectWidget.h"
#include "UI/MT2UIStyle.h"

namespace
{
	// Public.dds slot_base.sub - the 32x32 empty cell background every old slot grid uses.
	const FMT2AtlasRegion SlotBaseRegion(0.0f, 348.0f, 32.0f, 380.0f);
	// Windows.dds btn_plus_{up,over,down}.sub - the small 13x13 level-up button.
	const FMT2AtlasRegion PlusUpRegion(495.0f, 135.0f, 508.0f, 148.0f);
	const FMT2AtlasRegion PlusOverRegion(482.0f, 135.0f, 495.0f, 148.0f);
	const FMT2AtlasRegion PlusDownRegion(469.0f, 135.0f, 482.0f, 148.0f);
}

TSharedRef<SWidget> UMT2SkillSlotWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("SlotCanvas"));
		WidgetTree->RootWidget = RootCanvas;

		UTexture2D* PublicTexture = FMT2UIStyle::LoadTexture(TEXT("/Game/ymir_work/ui/T_public.T_public"));
		UTexture2D* WindowsTexture = FMT2UIStyle::LoadTexture(TEXT("/Game/ymir_work/ui/T_windows.T_windows"));

		BackgroundImage = WidgetTree->ConstructWidget<UMT2AtlasImage>(UMT2AtlasImage::StaticClass(), TEXT("SlotBase"));
		BackgroundImage->SetAtlas(PublicTexture, FMT2AtlasRect(
			SlotBaseRegion.Left, SlotBaseRegion.Top, SlotBaseRegion.Size().X, SlotBaseRegion.Size().Y));
		FMT2UIStyle::Place(RootCanvas, BackgroundImage, FVector2D(0.0, 0.0), FVector2D(32.0, 32.0));

		IconImage = WidgetTree->ConstructWidget<UMT2AtlasImage>(UMT2AtlasImage::StaticClass(), TEXT("SkillIcon"));
		IconImage->SetVisibility(ESlateVisibility::Collapsed);
		FMT2UIStyle::Place(RootCanvas, IconImage, FVector2D(0.0, 0.0), FVector2D(32.0, 32.0));

		// CSlotWindow::ActivateSlot used these exact 13 Public.dds frames, advancing every four
		// legacy UI updates. It marks toggle skills in both the character page and quick bar.
		ActiveEffectWidget = WidgetTree->ConstructWidget<UMT2SlotEffectWidget>(
			UMT2SlotEffectWidget::StaticClass(), TEXT("ActiveSkillEffect"));
		ActiveEffectWidget->SetEffectVisible(false);
		FMT2UIStyle::Place(RootCanvas, ActiveEffectWidget, FVector2D(0.0, 0.0), FVector2D(32.0, 32.0));

		// Translucent drain overlay over the icon while the skill cools down (old clock sweep).
		CooldownBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("CooldownBar"));
		FProgressBarStyle BarStyle;
		BarStyle.BackgroundImage.DrawAs = ESlateBrushDrawType::NoDrawType;
		BarStyle.FillImage.DrawAs = ESlateBrushDrawType::Image;
		BarStyle.FillImage.TintColor = FSlateColor(FLinearColor::White);
		CooldownBar->SetWidgetStyle(BarStyle);
		CooldownBar->SetBarFillType(EProgressBarFillType::BottomToTop);
		CooldownBar->SetFillColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f));
		CooldownBar->SetVisibility(ESlateVisibility::Collapsed);
		FMT2UIStyle::Place(RootCanvas, CooldownBar, FVector2D(0.0, 0.0), FVector2D(32.0, 32.0));

		SlotButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("SlotButton"));
		FButtonStyle TransparentStyle;
		TransparentStyle.Normal.DrawAs = ESlateBrushDrawType::NoDrawType;
		TransparentStyle.Hovered.DrawAs = ESlateBrushDrawType::NoDrawType;
		TransparentStyle.Pressed.DrawAs = ESlateBrushDrawType::NoDrawType;
		SlotButton->SetStyle(TransparentStyle);
		SlotButton->OnClicked.AddUniqueDynamic(this, &UMT2SkillSlotWidget::HandleSlotClicked);
		FMT2UIStyle::Place(RootCanvas, SlotButton, FVector2D(0.0, 0.0), FVector2D(32.0, 32.0));

		// Bottom-left, so the plus button hanging off the bottom-right corner never covers it.
		CountText = FMT2UIStyle::Label(*WidgetTree, FText::GetEmpty(), 8, ETextJustify::Left);
		CountText->SetShadowOffset(FVector2D(1.0, 1.0));
		CountText->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f));
		FMT2UIStyle::Place(RootCanvas, CountText, FVector2D(2.0, 20.0), FVector2D(28.0, 11.0));

		PlusButton = FMT2UIStyle::AtlasButton(
			*WidgetTree, WindowsTexture, PlusUpRegion, PlusOverRegion, PlusDownRegion);
		PlusButton->OnClicked.AddUniqueDynamic(this, &UMT2SkillSlotWidget::HandlePlusClicked);
		PlusButton->SetVisibility(ESlateVisibility::Collapsed);
		// The old client hangs the level-up button off the slot's bottom-right corner.
		FMT2UIStyle::Place(RootCanvas, PlusButton, FVector2D(21.0, 21.0), FVector2D(13.0, 13.0));

		// The mode can be set before the cell is built, so re-apply it now the widgets exist.
		ApplyDisplayOnly();
		if (const UMT2SkillDefinition* Definition = SkillDefinition.Get())
		{
			SetSkill(Definition, DisplayGrade);
			SetLevel(SkillLevel, bCurrentGradeCell);
		}
	}
	return Super::RebuildWidget();
}

void UMT2SkillSlotWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BindStatSources();
}

void UMT2SkillSlotWidget::NativeDestruct()
{
	UnbindStatSources();
	Super::NativeDestruct();
}

void UMT2SkillSlotWidget::SetDisplayOnly(bool bInDisplayOnly)
{
	bDisplayOnly = bInDisplayOnly;
	ApplyDisplayOnly();
}

void UMT2SkillSlotWidget::ApplyDisplayOnly()
{
	if (BackgroundImage)
	{
		BackgroundImage->SetVisibility(bDisplayOnly
			? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	// Dropping the button lets the host's mouse handling see the click; the icon below stays
	// hit-testable so hovering the cell still resolves this widget's tooltip.
	if (SlotButton)
	{
		SlotButton->SetVisibility(bDisplayOnly
			? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
	if (bDisplayOnly && PlusButton)
	{
		PlusButton->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (IconImage && IconImage->GetVisibility() != ESlateVisibility::Collapsed)
	{
		IconImage->SetVisibility(GetIconVisibility());
	}
}

ESlateVisibility UMT2SkillSlotWidget::GetIconVisibility() const
{
	return bDisplayOnly ? ESlateVisibility::Visible : ESlateVisibility::HitTestInvisible;
}

void UMT2SkillSlotWidget::SetSkill(const UMT2SkillDefinition* Definition, int32 Grade)
{
	SkillVnum = Definition ? Definition->Vnum : 0;
	SkillDefinition = Definition;
	DisplayGrade = Grade;
	RefreshTooltip();
	if (!IconImage)
	{
		return;
	}
	UTexture2D* Atlas = Definition ? Definition->IconAtlas.LoadSynchronous() : nullptr;
	const FMT2AtlasRect Region = Definition ? Definition->GetIconRegion(Grade) : FMT2AtlasRect(0, 0, 0, 0);
	if (Atlas && Region.IsValid())
	{
		IconImage->SetAtlas(Atlas, Region);
		IconImage->SetVisibility(GetIconVisibility());
	}
	else
	{
		IconImage->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UMT2SkillSlotWidget::SetEmpty()
{
	SkillVnum = 0;
	SkillDefinition.Reset();
	SetToolTip(nullptr);
	if (CooldownBar)
	{
		CooldownBar->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (IconImage)
	{
		IconImage->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (ActiveEffectWidget)
	{
		ActiveEffectWidget->SetEffectVisible(false);
	}
	bActiveSkill = false;
	if (CountText)
	{
		CountText->SetText(FText::GetEmpty());
	}
	if (PlusButton)
	{
		PlusButton->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UMT2SkillSlotWidget::SetLevel(int32 InSkillLevel, bool bCurrentGrade)
{
	bCurrentGradeCell = bCurrentGrade;
	if (SkillLevel != InSkillLevel)
	{
		SkillLevel = InSkillLevel;
		RefreshTooltip();
	}
	if (!CountText || !IconImage)
	{
		return;
	}

	// Non-current grades render dimmed with no count - like the old page where only the reached
	// grade's cell is lit.
	IconImage->SetRenderOpacity(bCurrentGrade ? 1.0f : 0.45f);
	if (!bCurrentGrade || SkillLevel <= 0)
	{
		CountText->SetText(FText::GetEmpty());
		return;
	}

	// Old SetSlotCountNew: the in-grade level with the mastery prefix (17, M3, G10, P).
	const EMT2SkillMastery Mastery = MT2SkillMastery::FromLevel(SkillLevel);
	FString Display;
	switch (Mastery)
	{
	case EMT2SkillMastery::Normal: Display = FString::FromInt(SkillLevel); break;
	case EMT2SkillMastery::Master: Display = FString::Printf(TEXT("M%d"), SkillLevel - 19); break;
	case EMT2SkillMastery::GrandMaster: Display = FString::Printf(TEXT("G%d"), SkillLevel - 29); break;
	case EMT2SkillMastery::PerfectMaster: Display = TEXT("P"); break;
	}
	CountText->SetText(FText::FromString(Display));
}

void UMT2SkillSlotWidget::SetPlusVisible(bool bVisible)
{
	if (PlusButton)
	{
		PlusButton->SetVisibility(bVisible && !bDisplayOnly
			? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void UMT2SkillSlotWidget::RefreshTooltip()
{
	const UMT2SkillDefinition* Definition = SkillDefinition.Get();
	if (!Definition)
	{
		SetToolTip(nullptr);
		return;
	}
	if (!SkillTooltip)
	{
		SkillTooltip = CreateWidget<UMT2SkillTooltipWidget>(
			GetOwningPlayer(), UMT2SkillTooltipWidget::StaticClass());
	}
	if (SkillTooltip)
	{
		SkillTooltip->SetSkill(Definition, SkillLevel);
		SetToolTip(SkillTooltip);
	}
}

void UMT2SkillSlotWidget::BindStatSources()
{
	const APlayerController* Controller = GetOwningPlayer();
	AMT2PlayerState* State = Controller ? Controller->GetPlayerState<AMT2PlayerState>() : nullptr;
	AMT2PlayerCharacter* Character = Cast<AMT2PlayerCharacter>(GetOwningPlayerPawn());
	UMT2PrimaryStatsComponent* StatePrimaryStats = State ? State->GetPrimaryStatsComponent() : nullptr;
	UMT2PrimaryStatsComponent* CharacterPrimaryStats = Character ? Character->GetPrimaryStatsComponent() : nullptr;
	UMT2CombatStatsComponent* CombatStats = Character ? Character->GetCombatStatsComponent() : nullptr;

	if (BoundStatePrimaryStats.Get() == StatePrimaryStats
		&& BoundCharacterPrimaryStats.Get() == CharacterPrimaryStats
		&& BoundCombatStats.Get() == CombatStats)
	{
		return;
	}

	UnbindStatSources();
	BoundStatePrimaryStats = StatePrimaryStats;
	BoundCharacterPrimaryStats = CharacterPrimaryStats;
	BoundCombatStats = CombatStats;

	if (StatePrimaryStats)
	{
		StatePrimaryStats->OnPrimaryStatsChanged.AddUniqueDynamic(
			this, &UMT2SkillSlotWidget::HandlePrimaryStatsChanged);
	}
	if (CharacterPrimaryStats && CharacterPrimaryStats != StatePrimaryStats)
	{
		CharacterPrimaryStats->OnPrimaryStatsChanged.AddUniqueDynamic(
			this, &UMT2SkillSlotWidget::HandlePrimaryStatsChanged);
	}
	if (CombatStats)
	{
		CombatStats->OnCombatStatsChanged.AddUniqueDynamic(
			this, &UMT2SkillSlotWidget::HandleCombatStatsChanged);
	}
	RefreshTooltip();
}

void UMT2SkillSlotWidget::UnbindStatSources()
{
	if (UMT2PrimaryStatsComponent* Stats = BoundStatePrimaryStats.Get())
	{
		Stats->OnPrimaryStatsChanged.RemoveDynamic(this, &UMT2SkillSlotWidget::HandlePrimaryStatsChanged);
	}
	if (UMT2PrimaryStatsComponent* Stats = BoundCharacterPrimaryStats.Get())
	{
		Stats->OnPrimaryStatsChanged.RemoveDynamic(this, &UMT2SkillSlotWidget::HandlePrimaryStatsChanged);
	}
	if (UMT2CombatStatsComponent* Stats = BoundCombatStats.Get())
	{
		Stats->OnCombatStatsChanged.RemoveDynamic(this, &UMT2SkillSlotWidget::HandleCombatStatsChanged);
	}
	BoundStatePrimaryStats.Reset();
	BoundCharacterPrimaryStats.Reset();
	BoundCombatStats.Reset();
}

void UMT2SkillSlotWidget::HandlePrimaryStatsChanged(FMT2PrimaryStats, FMT2PrimaryStats)
{
	RefreshTooltip();
}

void UMT2SkillSlotWidget::HandleCombatStatsChanged(FMT2CombatStats, FMT2CombatStats)
{
	RefreshTooltip();
}

void UMT2SkillSlotWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	BindStatSources();

	const AMT2PlayerCharacter* Character = Cast<AMT2PlayerCharacter>(GetOwningPlayerPawn());
	const UMT2StatusEffectComponent* StatusEffects = Character
		? Character->GetStatusEffectComponent() : nullptr;
	const bool bNowActive = bCurrentGradeCell && SkillVnum > 0 && StatusEffects
		&& StatusEffects->HasEffectType(SkillVnum);
	if (bNowActive != bActiveSkill)
	{
		bActiveSkill = bNowActive;
		if (ActiveEffectWidget)
		{
			ActiveEffectWidget->SetEffectVisible(bActiveSkill);
		}
	}

	if (!CooldownBar || SkillVnum <= 0)
	{
		return;
	}

	const APlayerController* Controller = GetOwningPlayer();
	const AMT2PlayerState* State = Controller ? Controller->GetPlayerState<AMT2PlayerState>() : nullptr;
	const UMT2SkillComponent* Skills = State ? State->GetSkillComponent() : nullptr;
	// Quickbar cells are display-only and must always reflect the replicated value, even if their
	// delegate was bound after a fast-array update. This also swaps the atlas region at M/G/P.
	if (bDisplayOnly && Skills && SkillVnum > 0)
	{
		const int32 CurrentLevel = Skills->GetSkillLevel(SkillVnum);
		if (CurrentLevel != SkillLevel)
		{
			const UMT2SkillDefinition* Definition = SkillDefinition.Get();
			const int32 CurrentGrade = FMath::Min(
				static_cast<int32>(MT2SkillMastery::FromLevel(CurrentLevel)), 2);
			SetSkill(Definition, CurrentGrade);
			SetLevel(CurrentLevel, true);
		}
	}
	float Duration = 0.0f;
	const float Remaining = Skills ? Skills->GetCooldownRemaining(SkillVnum, Duration) : 0.0f;
	const bool bCooling = Remaining > 0.0f && Duration > 0.0f;

	if (bCooling)
	{
		CooldownBar->SetVisibility(ESlateVisibility::HitTestInvisible);
		CooldownBar->SetPercent(Remaining / Duration);
	}
	else if (CooldownBar->GetVisibility() != ESlateVisibility::Collapsed)
	{
		CooldownBar->SetVisibility(ESlateVisibility::Collapsed);
	}

	// Cold-blue tint while cooling, so the icon itself reads as unavailable.
	if (bCooling != bOnCooldown && IconImage)
	{
		bOnCooldown = bCooling;
		IconImage->SetColorAndOpacity(bCooling
			? FLinearColor(0.45f, 0.5f, 0.7f, 1.0f) : FLinearColor::White);
	}
}

FReply UMT2SkillSlotWidget::NativeOnPreviewMouseButtonDown(
	const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// The transparent overlay button handles left clicks; right clicks need catching here.
	// Display-only cells stay out of it entirely and let the host handle both buttons.
	if (!bDisplayOnly && SkillVnum > 0 && InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		OnSlotRightClicked.Broadcast(SkillVnum);
		return FReply::Handled();
	}
	return Super::NativeOnPreviewMouseButtonDown(InGeometry, InMouseEvent);
}

void UMT2SkillSlotWidget::HandleSlotClicked()
{
	if (SkillVnum > 0)
	{
		OnSlotClicked.Broadcast(SkillVnum);
	}
}

void UMT2SkillSlotWidget::HandlePlusClicked()
{
	if (SkillVnum > 0)
	{
		OnPlusClicked.Broadcast(SkillVnum);
	}
}
