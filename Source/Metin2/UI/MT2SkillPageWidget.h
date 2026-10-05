/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "UI/MT2UserWidget.h"
#include "MT2SkillPageWidget.generated.h"

class UButton;
class UCanvasPanel;
class UMT2AtlasImage;
class UMT2SkillComponent;
class UMT2SkillSlotWidget;
class UTextBlock;
class AMT2PlayerState;

// The character window's skill page, recreating uiscript/characterwindow.py Skill_Page 1:1:
// active-skills title bar (group name + points), the skill board with 8 active skills x 3
// mastery-grade cells at the old coordinates, and the 6x2 support grid below. Content is built
// in native code; the Blueprint only provides the page canvas this widget is injected into.
UCLASS()
class METIN2_API UMT2SkillPageWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	void RefreshSkills();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UFUNCTION() void HandleGroup1Clicked();
	UFUNCTION() void HandleGroup2Clicked();
	UFUNCTION() void HandleSkillLevelsChanged();
	UFUNCTION() void HandleSkillGroupChanged(int32 NewSkillGroup);
	UFUNCTION() void HandleSkillPointsChanged(int32 OldPoints, int32 NewPoints);
	UFUNCTION() void HandleSlotPlusClicked(int32 SkillVnum);
	UFUNCTION() void HandleSlotClicked(int32 SkillVnum);
	UFUNCTION() void HandleSlotRightClicked(int32 SkillVnum);

	void BuildHorizontalBar(UCanvasPanel* Canvas, const FVector2D& Position, float Width);
	void BindSkillComponent();
	void UnbindSkillComponent();
	UMT2SkillComponent* GetSkillComponent() const;
	AMT2PlayerState* GetOwningMT2PlayerState() const;
	void SelectGroup(int32 Group);

	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> PageCanvas;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> GroupNameText;
	UPROPERTY(Transient) TObjectPtr<UButton> GroupButton1;
	UPROPERTY(Transient) TObjectPtr<UButton> GroupButton2;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> GroupButton1Text;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> GroupButton2Text;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ActivePointsText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SupportPointsText;

	// [skill 0..7][grade 0..2] - the 24 active cells of the old board.
	UPROPERTY(Transient) TArray<TObjectPtr<UMT2SkillSlotWidget>> ActiveSlots;
	// 6x2 support cells (start index 101 in the old layout).
	UPROPERTY(Transient) TArray<TObjectPtr<UMT2SkillSlotWidget>> SupportSlots;

	TWeakObjectPtr<UMT2SkillComponent> BoundSkillComponent;
	TWeakObjectPtr<AMT2PlayerState> BoundPlayerState;

	static constexpr int32 ActiveSkillRows = 8;
	static constexpr int32 GradeCount = 3;
	static constexpr int32 SupportSlotCount = 12;
};
