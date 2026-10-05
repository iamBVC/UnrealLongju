/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Stats/MT2StatTypes.h"
#include "UI/MT2UserWidget.h"
#include "MT2SkillSlotWidget.generated.h"

class UButton;
class UCanvasPanel;
class UMT2AtlasImage;
class UMT2CombatStatsComponent;
class UMT2PrimaryStatsComponent;
class UMT2SkillDefinition;
class UMT2SkillTooltipWidget;
class UMT2SlotEffectWidget;
class UProgressBar;
class UTextBlock;
class UTexture2D;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMT2SkillSlotClickedSignature, int32, SkillVnum);

// One 32x32 skill cell of the character window: slot background, atlas icon, level count text
// bottom-right, and a plus button shown when the skill can be leveled - the old CSlotWindow cell.
// Built entirely in native code; the page widget lays these out at the old client's coordinates.
UCLASS()
class METIN2_API UMT2SkillSlotWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	// Grade = which mastery column this cell represents (0 Normal, 1 Master, 2 Grand Master).
	void SetSkill(const UMT2SkillDefinition* Definition, int32 Grade);
	void SetEmpty();
	// Level display, old SetSlotCountNew: in-grade level with the M/G prefix look ("M1"..).
	void SetLevel(int32 SkillLevel, bool bCurrentGrade);
	void SetPlusVisible(bool bVisible);

	// Display-only cells still draw the icon, level and cooldown drain and still raise the tooltip,
	// but never handle clicks or show the plus button, so a host widget keeps its own mouse
	// behavior - the quick slot bar embeds one of these and drives use/carry/clear itself. The host
	// already paints slot_base, so the cell drops its own background too.
	void SetDisplayOnly(bool bInDisplayOnly);

	int32 GetSkillVnum() const { return SkillVnum; }

	UPROPERTY(BlueprintAssignable, Category = "Skills")
	FMT2SkillSlotClickedSignature OnSlotClicked;

	UPROPERTY(BlueprintAssignable, Category = "Skills")
	FMT2SkillSlotClickedSignature OnPlusClicked;

	UPROPERTY(BlueprintAssignable, Category = "Skills")
	FMT2SkillSlotClickedSignature OnSlotRightClicked;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnPreviewMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	UFUNCTION() void HandleSlotClicked();
	UFUNCTION() void HandlePlusClicked();
	UFUNCTION() void HandlePrimaryStatsChanged(FMT2PrimaryStats OldStats, FMT2PrimaryStats NewStats);
	UFUNCTION() void HandleCombatStatsChanged(FMT2CombatStats OldStats, FMT2CombatStats NewStats);

	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> RootCanvas;
	UPROPERTY(Transient) TObjectPtr<UMT2AtlasImage> BackgroundImage;
	UPROPERTY(Transient) TObjectPtr<UMT2AtlasImage> IconImage;
	UPROPERTY(Transient) TObjectPtr<UMT2SlotEffectWidget> ActiveEffectWidget;
	UPROPERTY(Transient) TObjectPtr<UButton> SlotButton;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CountText;
	UPROPERTY(Transient) TObjectPtr<UButton> PlusButton;
	UPROPERTY(Transient) TObjectPtr<UProgressBar> CooldownBar;
	UPROPERTY(Transient) TObjectPtr<UMT2SkillTooltipWidget> SkillTooltip;

	void RefreshTooltip();
	void BindStatSources();
	void UnbindStatSources();
	void ApplyDisplayOnly();
	ESlateVisibility GetIconVisibility() const;

	TWeakObjectPtr<const UMT2SkillDefinition> SkillDefinition;
	TWeakObjectPtr<UMT2PrimaryStatsComponent> BoundStatePrimaryStats;
	TWeakObjectPtr<UMT2PrimaryStatsComponent> BoundCharacterPrimaryStats;
	TWeakObjectPtr<UMT2CombatStatsComponent> BoundCombatStats;
	int32 SkillVnum = 0;
	int32 SkillLevel = 0;
	int32 DisplayGrade = 0;
	bool bOnCooldown = false;
	bool bActiveSkill = false;
	bool bDisplayOnly = false;
	bool bCurrentGradeCell = false;
};
