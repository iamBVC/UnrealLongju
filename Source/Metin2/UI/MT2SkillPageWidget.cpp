/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2SkillPageWidget.h"
#include "Config/MT2PathSettings.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Characters/MT2PlayerCharacter.h"
#include "Engine/LocalPlayer.h"
#include "Player/MT2PlayerState.h"
#include "Skills/MT2SkillComponent.h"
#include "Skills/MT2SkillDefinition.h"
#include "Skills/MT2SkillSet.h"
#include "Skills/MT2SkillTypes.h"
#include "UI/MT2AtlasImage.h"
#include "UI/MT2CursorCarrySubsystem.h"
#include "UI/MT2SkillSlotWidget.h"
#include "UI/MT2UIStyle.h"

namespace
{
	// characterwindow.py Skill_Page geometry.
	constexpr float ActiveBarY = 17.0f;
	constexpr float BarX = 15.0f;
	constexpr float BarWidth = 223.0f;
	constexpr float BarHeight = 17.0f;
	constexpr float SupportBarY = 200.0f;
	const FVector2D BoardPosition(13.0f, 38.0f);
	const FVector2D ActiveSlotOrigin(16.0f, 38.0f);
	const FVector2D SupportGridOrigin(18.0f, 221.0f);

	// Windows.dds fragments.
	const FMT2AtlasRegion SkillBoardRegion(0.0f, 0.0f, 227.0f, 152.0f);
	const FMT2AtlasRegion GroupButtonUpRegion(461.0f, 116.0f, 504.0f, 132.0f);
	const FMT2AtlasRegion GroupButtonOverRegion(227.0f, 135.0f, 270.0f, 151.0f);
	const FMT2AtlasRegion GroupButtonDownRegion(270.0f, 135.0f, 313.0f, 151.0f);
	// locale windows.dds label_uppt.sub (the "points" label plate).
	const FMT2AtlasRegion PointsLabelRegion(405.0f, 89.0f, 480.0f, 103.0f);

	const FLinearColor TitleTextColor(1.0f, 0.89f, 0.678f);

	UTextBlock* MakeBarText(UWidgetTree& Tree, const FText& Text)
	{
		UTextBlock* Label = FMT2UIStyle::Label(Tree, Text, 10, ETextJustify::Left);
		Label->SetColorAndOpacity(FSlateColor(TitleTextColor));
		Label->SetShadowOffset(FVector2D(1.0, 1.0));
		Label->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f));
		return Label;
	}
}

TSharedRef<SWidget> UMT2SkillPageWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		PageCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("SkillPageCanvas"));
		WidgetTree->RootWidget = PageCanvas;

		UTexture2D* WindowsTexture = FMT2UIStyle::LoadTexture(UMT2PathSettings::Path(TEXT("UI_WindowsAtlas")));
		UTexture2D* LocaleWindowsTexture =
			FMT2UIStyle::LoadTexture(UMT2PathSettings::Path(TEXT("locale_en_ui_windows_T_windows")));

		// ---- Active skills title bar ----
		BuildHorizontalBar(PageCanvas, FVector2D(BarX, ActiveBarY), BarWidth);

		GroupNameText = MakeBarText(*WidgetTree, FText::GetEmpty());
		FMT2UIStyle::Place(PageCanvas, GroupNameText, FVector2D(BarX + 7.0, ActiveBarY + 2.0), FVector2D(120.0, 13.0));

		auto MakeGroupButton = [&](const TCHAR* Name, TObjectPtr<UTextBlock>& OutText) -> UButton*
		{
			UButton* Button = FMT2UIStyle::AtlasButton(
				*WidgetTree, WindowsTexture, GroupButtonUpRegion, GroupButtonOverRegion, GroupButtonDownRegion);
			OutText = FMT2UIStyle::Label(*WidgetTree, FText::GetEmpty(), 9, ETextJustify::Center);
			OutText->SetColorAndOpacity(FSlateColor(TitleTextColor));
			Button->AddChild(OutText);
			if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(OutText->Slot))
			{
				ButtonSlot->SetPadding(FMargin(0.0f));
				ButtonSlot->SetHorizontalAlignment(HAlign_Center);
				ButtonSlot->SetVerticalAlignment(VAlign_Center);
			}
			return Button;
		};
		GroupButton1 = MakeGroupButton(TEXT("GroupButton1"), GroupButton1Text);
		GroupButton1->OnClicked.AddUniqueDynamic(this, &UMT2SkillPageWidget::HandleGroup1Clicked);
		FMT2UIStyle::Place(PageCanvas, GroupButton1, FVector2D(BarX + 5.0, ActiveBarY + 2.0), FVector2D(43.0, 16.0));
		GroupButton2 = MakeGroupButton(TEXT("GroupButton2"), GroupButton2Text);
		GroupButton2->OnClicked.AddUniqueDynamic(this, &UMT2SkillPageWidget::HandleGroup2Clicked);
		FMT2UIStyle::Place(PageCanvas, GroupButton2, FVector2D(BarX + 50.0, ActiveBarY + 2.0), FVector2D(43.0, 16.0));

		{
			UMT2AtlasImage* Label = WidgetTree->ConstructWidget<UMT2AtlasImage>(UMT2AtlasImage::StaticClass());
			Label->SetAtlas(LocaleWindowsTexture, FMT2AtlasRect(
				PointsLabelRegion.Left, PointsLabelRegion.Top,
				PointsLabelRegion.Size().X, PointsLabelRegion.Size().Y));
			FMT2UIStyle::Place(PageCanvas, Label, FVector2D(BarX + 145.0, ActiveBarY + 3.0), FVector2D(75.0, 14.0));
			ActivePointsText = FMT2UIStyle::Label(*WidgetTree, FText::AsNumber(0), 9, ETextJustify::Center);
			FMT2UIStyle::Place(PageCanvas, ActivePointsText,
				FVector2D(BarX + 145.0 + 47.0, ActiveBarY + 3.0), FVector2D(28.0, 14.0));
		}

		// ---- Skill board + the 8x3 active cells ----
		{
			UMT2AtlasImage* Board = WidgetTree->ConstructWidget<UMT2AtlasImage>(UMT2AtlasImage::StaticClass());
			Board->SetAtlas(WindowsTexture, FMT2AtlasRect(
				SkillBoardRegion.Left, SkillBoardRegion.Top,
				SkillBoardRegion.Size().X, SkillBoardRegion.Size().Y));
			FMT2UIStyle::Place(PageCanvas, Board, BoardPosition, SkillBoardRegion.Size());
		}

		ActiveSlots.Reset();
		for (int32 SkillIndex = 0; SkillIndex < ActiveSkillRows; ++SkillIndex)
		{
			const bool bRightColumn = (SkillIndex % 2) != 0;
			const int32 Row = SkillIndex / 2;
			for (int32 Grade = 0; Grade < GradeCount; ++Grade)
			{
				// uiscript slot offsets: columns 1/38/75 (left skills) and 113/150/187 (right),
				// rows 4/40/76/112.
				const float CellX = (bRightColumn ? 113.0f : 1.0f) + Grade * 37.0f;
				const float CellY = 4.0f + Row * 36.0f;
				UMT2SkillSlotWidget* SlotWidget = CreateWidget<UMT2SkillSlotWidget>(
					GetOwningPlayer(), UMT2SkillSlotWidget::StaticClass());
				SlotWidget->OnPlusClicked.AddUniqueDynamic(this, &UMT2SkillPageWidget::HandleSlotPlusClicked);
				SlotWidget->OnSlotClicked.AddUniqueDynamic(this, &UMT2SkillPageWidget::HandleSlotClicked);
				SlotWidget->OnSlotRightClicked.AddUniqueDynamic(this, &UMT2SkillPageWidget::HandleSlotRightClicked);
				FMT2UIStyle::Place(PageCanvas, SlotWidget,
					ActiveSlotOrigin + FVector2D(CellX, CellY), FVector2D(32.0, 32.0));
				ActiveSlots.Add(SlotWidget);
			}
		}

		// ---- Support skills ----
		BuildHorizontalBar(PageCanvas, FVector2D(BarX, SupportBarY), BarWidth);
		UTextBlock* SupportTitle = MakeBarText(*WidgetTree,
			NSLOCTEXT("MT2SkillPage", "SupportSkills", "Support Skills"));
		FMT2UIStyle::Place(PageCanvas, SupportTitle, FVector2D(BarX + 7.0, SupportBarY + 2.0), FVector2D(120.0, 13.0));
		{
			UMT2AtlasImage* Label = WidgetTree->ConstructWidget<UMT2AtlasImage>(UMT2AtlasImage::StaticClass());
			Label->SetAtlas(LocaleWindowsTexture, FMT2AtlasRect(
				PointsLabelRegion.Left, PointsLabelRegion.Top,
				PointsLabelRegion.Size().X, PointsLabelRegion.Size().Y));
			FMT2UIStyle::Place(PageCanvas, Label, FVector2D(BarX + 145.0, SupportBarY + 3.0), FVector2D(75.0, 14.0));
			SupportPointsText = FMT2UIStyle::Label(*WidgetTree, FText::AsNumber(0), 9, ETextJustify::Center);
			FMT2UIStyle::Place(PageCanvas, SupportPointsText,
				FVector2D(BarX + 145.0 + 47.0, SupportBarY + 3.0), FVector2D(28.0, 14.0));
		}

		SupportSlots.Reset();
		for (int32 Index = 0; Index < SupportSlotCount; ++Index)
		{
			// grid_table x18 y221, 6x2, step 32 + blank 5/4.
			const int32 Column = Index % 6;
			const int32 Row = Index / 6;
			UMT2SkillSlotWidget* SlotWidget = CreateWidget<UMT2SkillSlotWidget>(
				GetOwningPlayer(), UMT2SkillSlotWidget::StaticClass());
			SlotWidget->OnPlusClicked.AddUniqueDynamic(this, &UMT2SkillPageWidget::HandleSlotPlusClicked);
			SlotWidget->OnSlotClicked.AddUniqueDynamic(this, &UMT2SkillPageWidget::HandleSlotClicked);
				SlotWidget->OnSlotRightClicked.AddUniqueDynamic(this, &UMT2SkillPageWidget::HandleSlotRightClicked);
			FMT2UIStyle::Place(PageCanvas, SlotWidget,
				SupportGridOrigin + FVector2D(Column * 37.0, Row * 36.0), FVector2D(32.0, 32.0));
			SupportSlots.Add(SlotWidget);
		}
	}
	return Super::RebuildWidget();
}

void UMT2SkillPageWidget::BuildHorizontalBar(UCanvasPanel* Canvas, const FVector2D& Position, float Width)
{
	// ui.py HorizontalBar: left cap + tiled center + right cap, 17px tall.
	UTexture2D* LeftTexture = FMT2UIStyle::LoadTexture(UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_horizontalbar_left")));
	UTexture2D* CenterTexture = FMT2UIStyle::LoadTexture(UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_horizontalbar_center")));
	UTexture2D* RightTexture = FMT2UIStyle::LoadTexture(UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_T_horizontalbar_right")));
	const float CapWidth = LeftTexture ? LeftTexture->GetSizeX() : 8.0f;

	FMT2UIStyle::Place(Canvas, FMT2UIStyle::Image(*WidgetTree, LeftTexture),
		Position, FVector2D(CapWidth, BarHeight));
	FMT2UIStyle::Place(Canvas, FMT2UIStyle::Image(*WidgetTree, CenterTexture, true),
		Position + FVector2D(CapWidth, 0.0), FVector2D(Width - CapWidth * 2.0, BarHeight));
	FMT2UIStyle::Place(Canvas, FMT2UIStyle::Image(*WidgetTree, RightTexture),
		Position + FVector2D(Width - CapWidth, 0.0), FVector2D(CapWidth, BarHeight));
}

void UMT2SkillPageWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BindSkillComponent();
	RefreshSkills();
}

void UMT2SkillPageWidget::NativeDestruct()
{
	UnbindSkillComponent();
	Super::NativeDestruct();
}

void UMT2SkillPageWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	// The page is often constructed before the PlayerState (and its skill component) replicates;
	// keep retrying until the first successful bind, then delegates drive all refreshes.
	if (!BoundSkillComponent.IsValid() && GetSkillComponent())
	{
		RefreshSkills();
	}
}

AMT2PlayerState* UMT2SkillPageWidget::GetOwningMT2PlayerState() const
{
	const APlayerController* Controller = GetOwningPlayer();
	return Controller ? Controller->GetPlayerState<AMT2PlayerState>() : nullptr;
}

UMT2SkillComponent* UMT2SkillPageWidget::GetSkillComponent() const
{
	const AMT2PlayerState* State = GetOwningMT2PlayerState();
	return State ? State->GetSkillComponent() : nullptr;
}

void UMT2SkillPageWidget::BindSkillComponent()
{
	UMT2SkillComponent* Skills = GetSkillComponent();
	AMT2PlayerState* State = GetOwningMT2PlayerState();
	if (BoundSkillComponent.Get() == Skills && BoundPlayerState.Get() == State)
	{
		return;
	}
	UnbindSkillComponent();
	BoundSkillComponent = Skills;
	BoundPlayerState = State;
	if (Skills)
	{
		Skills->OnSkillLevelsChanged.AddUniqueDynamic(this, &UMT2SkillPageWidget::HandleSkillLevelsChanged);
		Skills->OnSkillGroupChanged.AddUniqueDynamic(this, &UMT2SkillPageWidget::HandleSkillGroupChanged);
	}
	if (State)
	{
		State->OnSkillPointsChanged.AddUniqueDynamic(this, &UMT2SkillPageWidget::HandleSkillPointsChanged);
	}
}

void UMT2SkillPageWidget::UnbindSkillComponent()
{
	if (UMT2SkillComponent* Skills = BoundSkillComponent.Get())
	{
		Skills->OnSkillLevelsChanged.RemoveDynamic(this, &UMT2SkillPageWidget::HandleSkillLevelsChanged);
		Skills->OnSkillGroupChanged.RemoveDynamic(this, &UMT2SkillPageWidget::HandleSkillGroupChanged);
	}
	if (AMT2PlayerState* State = BoundPlayerState.Get())
	{
		State->OnSkillPointsChanged.RemoveDynamic(this, &UMT2SkillPageWidget::HandleSkillPointsChanged);
	}
	BoundSkillComponent.Reset();
	BoundPlayerState.Reset();
}

void UMT2SkillPageWidget::RefreshSkills()
{
	BindSkillComponent();
	UMT2SkillComponent* Skills = BoundSkillComponent.Get();
	AMT2PlayerState* State = BoundPlayerState.Get();
	if (!Skills || !State || ActiveSlots.IsEmpty())
	{
		return;
	}

	const int32 SkillPoints = State->GetUnspentSkillPoints();
	if (ActivePointsText)
	{
		ActivePointsText->SetText(FText::AsNumber(SkillPoints));
	}
	if (SupportPointsText)
	{
		// One shared pool for now (the old game split active/support points).
		SupportPointsText->SetText(FText::AsNumber(SkillPoints));
	}

	const int32 SkillGroup = Skills->GetSkillGroup();
	const EMT2CharacterRace Race = State->GetCharacterAppearance().Race;
	const UMT2SkillSet* ActiveSet = Skills->GetSkillSet();

	// Before the level-5 choice the bar offers the two groups; afterwards it names the chosen one.
	const bool bChoosingGroup = SkillGroup == 0;
	if (GroupNameText)
	{
		GroupNameText->SetVisibility(bChoosingGroup ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
		GroupNameText->SetText(ActiveSet ? ActiveSet->GroupDisplayName : FText::GetEmpty());
	}
	if (GroupButton1 && GroupButton2)
	{
		GroupButton1->SetVisibility(bChoosingGroup ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		GroupButton2->SetVisibility(bChoosingGroup ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (bChoosingGroup)
		{
			const UMT2SkillSet* Set1 = UMT2SkillSet::FindSkillSet(Race, 1);
			const UMT2SkillSet* Set2 = UMT2SkillSet::FindSkillSet(Race, 2);
			GroupButton1Text->SetText(Set1 ? Set1->GroupDisplayName : FText::FromString(TEXT("Group 1")));
			GroupButton2Text->SetText(Set2 ? Set2->GroupDisplayName : FText::FromString(TEXT("Group 2")));
		}
	}

	// Active board: only the set's Active-type skills occupy the 8 rows (horse skills ride in the
	// set but live on their own page in the old client).
	TArray<UMT2SkillDefinition*> BoardSkills;
	if (ActiveSet)
	{
		for (UMT2SkillDefinition* Definition : ActiveSet->ActiveSkills)
		{
			if (Definition && Definition->SkillType == EMT2SkillType::Active)
			{
				BoardSkills.Add(Definition);
			}
		}
	}
	for (int32 SkillIndex = 0; SkillIndex < ActiveSkillRows; ++SkillIndex)
	{
		UMT2SkillDefinition* Definition =
			BoardSkills.IsValidIndex(SkillIndex) ? BoardSkills[SkillIndex] : nullptr;
		const int32 Level = Definition ? Skills->GetSkillLevel(Definition->Vnum) : 0;
		const EMT2SkillMastery Mastery = MT2SkillMastery::FromLevel(Level);
		const int32 CurrentGrade = FMath::Min(static_cast<int32>(Mastery), GradeCount - 1);
		for (int32 Grade = 0; Grade < GradeCount; ++Grade)
		{
			UMT2SkillSlotWidget* Cell = ActiveSlots[SkillIndex * GradeCount + Grade];
			if (!Definition)
			{
				Cell->SetEmpty();
				continue;
			}
			Cell->SetSkill(Definition, Grade);
			Cell->SetLevel(Level, Grade == CurrentGrade);
			Cell->SetPlusVisible(Grade == 0 && Skills->CanLearnSkillByPoint(Definition->Vnum));
		}
	}

	for (int32 Index = 0; Index < SupportSlots.Num(); ++Index)
	{
		UMT2SkillDefinition* Definition = ActiveSet && ActiveSet->SupportSkills.IsValidIndex(Index)
			? ActiveSet->SupportSkills[Index].Get() : nullptr;
		UMT2SkillSlotWidget* Cell = SupportSlots[Index];
		if (!Definition)
		{
			Cell->SetEmpty();
			continue;
		}
		Cell->SetSkill(Definition, 0);
		Cell->SetLevel(Skills->GetSkillLevel(Definition->Vnum), true);
		Cell->SetPlusVisible(Skills->CanLearnSkillByPoint(Definition->Vnum));
	}
}

void UMT2SkillPageWidget::SelectGroup(int32 Group)
{
	if (UMT2SkillComponent* Skills = GetSkillComponent())
	{
		if (Skills->GetSkillGroup() == 0)
		{
			Skills->ServerSelectSkillGroup(Group);
		}
	}
}

void UMT2SkillPageWidget::HandleGroup1Clicked() { SelectGroup(1); }
void UMT2SkillPageWidget::HandleGroup2Clicked() { SelectGroup(2); }
void UMT2SkillPageWidget::HandleSkillLevelsChanged() { RefreshSkills(); }
void UMT2SkillPageWidget::HandleSkillGroupChanged(int32) { RefreshSkills(); }
void UMT2SkillPageWidget::HandleSkillPointsChanged(int32, int32) { RefreshSkills(); }

void UMT2SkillPageWidget::HandleSlotPlusClicked(int32 SkillVnum)
{
	if (UMT2SkillComponent* Skills = GetSkillComponent())
	{
		Skills->ServerLearnSkillByPoint(SkillVnum);
	}
}

void UMT2SkillPageWidget::HandleSlotClicked(int32 SkillVnum)
{
	// Left click picks the skill up onto the cursor for taskbar placement.
	UMT2SkillComponent* Skills = GetSkillComponent();
	const UMT2SkillDefinition* Definition = Skills ? Skills->FindSkillDefinition(SkillVnum) : nullptr;
	UMT2CursorCarrySubsystem* Carry = GetOwningLocalPlayer()
		? GetOwningLocalPlayer()->GetSubsystem<UMT2CursorCarrySubsystem>() : nullptr;
	if (!Definition || !Carry || Skills->GetSkillLevel(SkillVnum) <= 0)
	{
		return;
	}
	if (Carry->IsCarrying())
	{
		Carry->CancelCarry();
		return;
	}
	UTexture2D* Atlas = Definition->IconAtlas.LoadSynchronous();
	const FMT2AtlasRect Region = Definition->GetIconRegion(0);
	if (Atlas && Region.IsValid())
	{
		Carry->BeginCarry(EMT2CarryKind::Skill, SkillVnum, INDEX_NONE, FMT2UIStyle::AtlasBrush(
			Atlas, FMT2AtlasRegion(Region.X, Region.Y, Region.X + Region.Width, Region.Y + Region.Height)));
	}
}

void UMT2SkillPageWidget::HandleSlotRightClicked(int32 SkillVnum)
{
	// Right click uses the skill (toggle skills flip on/off).
	if (AMT2PlayerCharacter* Character = Cast<AMT2PlayerCharacter>(GetOwningPlayerPawn()))
	{
		Character->ServerUseSkill(SkillVnum);
	}
}
