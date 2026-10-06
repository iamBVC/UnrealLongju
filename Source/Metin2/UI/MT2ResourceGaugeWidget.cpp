/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2ResourceGaugeWidget.h"
#include "Config/MT2PathSettings.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/ProgressBar.h"
#include "Engine/Texture2D.h"
#include "UI/MT2UIStyle.h"

namespace
{
	// Old client uiscript/taskbar.py: the gauge board is game/taskbar/gauge.sub, which is the
	// region (0,0)-(158,47) of ui/taskbar.tga (imported as T_taskbar). The fill bars are the
	// animated HPGauge/SPGauge/STGauge frame folders.
	const TCHAR* TaskbarTexturePath() { return UMT2PathSettings::Path(TEXT("UI_TaskbarAtlas")); }
	const FMT2AtlasRegion GaugeBoardRegion(0.0f, 0.0f, 158.0f, 47.0f);
}

void UMT2ResourceGaugeWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// Re-apply the board brush from code so the gauge never depends on the Widget Blueprint's baked
	// texture reference (which goes dangling whenever the atlas is re-imported and its asset GUID
	// changes - the classic "gauge board disappeared after a texture re-import").
	if (GaugeBackground)
	{
		UTexture2D* Taskbar = FMT2UIStyle::LoadTexture(TaskbarTexturePath());
		if (Taskbar)
		{
			GaugeBackground->SetBrush(FMT2UIStyle::AtlasBrush(Taskbar, GaugeBoardRegion));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("MT2ResourceGauge: gauge board texture '%s' not found."), TaskbarTexturePath());
		}
	}
	LoadAnimationFrames();
	// Translucent potion-regen target bars, one layer behind the live HP/MP fills.
	HealthRegenGhost = CreateRegenGhost(HealthGauge);
	ManaRegenGhost = CreateRegenGhost(ManaGauge);
	ApplyAnimationFrame();
}

UProgressBar* UMT2ResourceGaugeWidget::CreateRegenGhost(UProgressBar* Real)
{
	UPanelWidget* Parent = Real ? Real->GetParent() : nullptr;
	if (!Parent || !WidgetTree)
	{
		return nullptr;
	}

	UProgressBar* Ghost = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass());
	Ghost->SetVisibility(ESlateVisibility::HitTestInvisible);
	Ghost->SetRenderOpacity(0.5f);
	Ghost->SetPercent(0.0f);

	// Match the live bar's placement exactly, sitting one Z layer behind it so only the [current, target]
	// slice shows through under the opaque live fill.
	if (UCanvasPanel* Canvas = Cast<UCanvasPanel>(Parent))
	{
		UCanvasPanelSlot* GhostSlot = Canvas->AddChildToCanvas(Ghost);
		if (UCanvasPanelSlot* RealSlot = Cast<UCanvasPanelSlot>(Real->Slot); RealSlot && GhostSlot)
		{
			GhostSlot->SetAnchors(RealSlot->GetAnchors());
			GhostSlot->SetOffsets(RealSlot->GetOffsets());
			GhostSlot->SetAlignment(RealSlot->GetAlignment());
			// Draw order must be: gauge board < ghost target < live fill. The board and bars share a Z of
			// 0, so placing the ghost at the bar's Z and lifting the live bar one layer above it keeps the
			// translucent target under the opaque fill yet still on top of the board (rather than -1, which
			// hid it behind the board).
			const int32 RealZ = RealSlot->GetZOrder();
			GhostSlot->SetZOrder(RealZ);
			RealSlot->SetZOrder(RealZ + 1);
		}
	}
	else
	{
		Parent->AddChild(Ghost);
	}
	return Ghost;
}

void UMT2ResourceGaugeWidget::LoadAnimationFrames()
{
	auto LoadFrames = [](const TCHAR* Folder, TArray<TObjectPtr<UTexture2D>>& Frames)
	{
		Frames.Reset();
		for (int32 Index = 1; Index <= 7; ++Index)
		{
			const FString Path = UMT2PathSettings::Format(TEXT("ymir_work_ui_pattern_Name_T_Index"), TEXT("%s%02d%02d"), Folder, Index, Index);
			UTexture2D* Frame = FMT2UIStyle::LoadTexture(*Path);
			// Keep gauge fill textures fully resident; otherwise the first PIE shows streamed-in mips as
			// garbage until a play forces them in.
			if (Frame)
			{
				Frame->bIgnoreStreamingMipBias = true;
				Frame->SetForceMipLevelsToBeResident(60.0f);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("MT2ResourceGauge: fill frame '%s' not found."), *Path);
			}
			Frames.Add(Frame);
		}
	};
	LoadFrames(TEXT("hpgauge"), HealthFrames);
	LoadFrames(TEXT("spgauge"), ManaFrames);
	LoadFrames(TEXT("stgauge"), StaminaFrames);
}

void UMT2ResourceGaugeWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	FrameAccumulator += InDeltaTime;
	if (FrameAccumulator >= 0.1f)
	{
		FrameAccumulator = FMath::Fmod(FrameAccumulator, 0.1f);
		FrameIndex = (FrameIndex + 1) % 7;
		ApplyAnimationFrame();
	}
}

void UMT2ResourceGaugeWidget::ApplyAnimationFrame()
{
	auto Apply = [this](UProgressBar* Gauge, const TArray<TObjectPtr<UTexture2D>>& Frames)
	{
		if (!Gauge || !Frames.IsValidIndex(FrameIndex) || !Frames[FrameIndex]) return;
		FProgressBarStyle Style = Gauge->GetWidgetStyle();
		Style.BackgroundImage.DrawAs = ESlateBrushDrawType::NoDrawType;
		Style.FillImage = FMT2UIStyle::TextureBrush(Frames[FrameIndex]);
		Style.MarqueeImage.DrawAs = ESlateBrushDrawType::NoDrawType;
		Gauge->SetWidgetStyle(Style);
	};
	Apply(HealthGauge, HealthFrames);
	Apply(ManaGauge, ManaFrames);
	Apply(StaminaGauge, StaminaFrames);
	// The ghost target bars reuse the same animated fill (their 50% render opacity makes them read as a
	// lighter shade of the same colour).
	Apply(HealthRegenGhost, HealthFrames);
	Apply(ManaRegenGhost, ManaFrames);
}

void UMT2ResourceGaugeWidget::SetResources(float Health, float Mana, float Stamina)
{
	HealthGauge->SetPercent(FMath::Clamp(Health, 0.0f, 1.0f));
	ManaGauge->SetPercent(FMath::Clamp(Mana, 0.0f, 1.0f));
	StaminaGauge->SetPercent(FMath::Clamp(Stamina, 0.0f, 1.0f));
}

void UMT2ResourceGaugeWidget::SetResourceValues(
	float Health, float MaxHealth, float Mana, float MaxMana, float Stamina, float MaxStamina,
	float HealthRegenPool, float ManaRegenPool)
{
	SetGaugeValue(HealthGauge, NSLOCTEXT("MT2UI", "HealthGaugeName", "HP"), Health, MaxHealth);
	SetGaugeValue(ManaGauge, NSLOCTEXT("MT2UI", "ManaGaugeName", "MP"), Mana, MaxMana);
	SetGaugeValue(StaminaGauge, NSLOCTEXT("MT2UI", "StaminaGaugeName", "Stamina"), Stamina, MaxStamina);

	// Ghost target = current + pending potion recovery, clamped to the max. When no potion is pending it
	// equals the live value and is hidden behind the opaque bar, so it simply doesn't show.
	if (HealthRegenGhost)
	{
		HealthRegenGhost->SetPercent(MaxHealth > 0.0f ? FMath::Clamp((Health + HealthRegenPool) / MaxHealth, 0.0f, 1.0f) : 0.0f);
	}
	if (ManaRegenGhost)
	{
		ManaRegenGhost->SetPercent(MaxMana > 0.0f ? FMath::Clamp((Mana + ManaRegenPool) / MaxMana, 0.0f, 1.0f) : 0.0f);
	}
}

void UMT2ResourceGaugeWidget::SetGaugeValue(
	UProgressBar* Gauge, const FText& Name, float CurrentValue, float MaxValue)
{
	if (!Gauge)
	{
		return;
	}

	Gauge->SetPercent(MaxValue > 0.0f ? FMath::Clamp(CurrentValue / MaxValue, 0.0f, 1.0f) : 0.0f);
	const FText ToolTip = FText::Format(
		NSLOCTEXT("MT2UI", "ResourceGaugeToolTip", "{0}: {1} / {2}"),
		Name,
		FText::AsNumber(FMath::RoundToInt64(CurrentValue)),
		FText::AsNumber(FMath::RoundToInt64(MaxValue)));
	// RefreshResourceBars runs every frame. Reassigning an identical tooltip every frame resets
	// Slate's hover timer, so the popup can never open.
	if (!Gauge->GetToolTipText().EqualTo(ToolTip))
	{
		Gauge->SetToolTipText(ToolTip);
	}
}
