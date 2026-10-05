/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2SkillTooltipWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Characters/MT2PlayerCharacter.h"
#include "Player/MT2PlayerState.h"
#include "Skills/MT2SkillComponent.h"
#include "Skills/MT2SkillDefinition.h"
#include "Skills/MT2SkillFormula.h"
#include "Skills/MT2SkillTypes.h"

namespace
{
	const FLinearColor TitleColor(1.0f, 0.89f, 0.42f);
	const FLinearColor BodyColor(0.85f, 0.85f, 0.85f);
	// uitooltip.py ENABLE_COLOR / DISABLE_COLOR: the reached level is lit, the next one greyed.
	const FLinearColor StatColor(0.6f, 0.85f, 1.0f);
	const FLinearColor NextLevelColor(0.45f, 0.45f, 0.45f);
	const FLinearColor CostColor(0.7f, 0.7f, 0.55f);
	const FLinearColor NegativeColor(0.85f, 0.3f, 0.3f);
	const FLinearColor RequirementColor(0.66f, 0.66f, 0.85f);

	// uitooltip.py AFFECT_NAME_DICT: the POINT_ON name -> the label the old tooltip prints.
	// "HP" is the attack line - negative HP polys are damage, positive ones are a heal.
	const TCHAR* GetAffectLabel(const FString& PointOnName, bool bHeal)
	{
		if (PointOnName == TEXT("HP")) return bHeal ? TEXT("Heal: ") : TEXT("Attack Power: ");
		if (PointOnName == TEXT("ATT_GRADE")) return TEXT("Attack Value: ");
		if (PointOnName == TEXT("DEF_GRADE")) return TEXT("Defence: ");
		if (PointOnName == TEXT("ATT_SPEED")) return TEXT("Attack Speed: ");
		if (PointOnName == TEXT("MOV_SPEED")) return TEXT("Movement Speed: ");
		if (PointOnName == TEXT("DODGE")) return TEXT("Evasion Chance: ");
		if (PointOnName == TEXT("RESIST_NORMAL")) return TEXT("Melee Resistance: ");
		if (PointOnName == TEXT("REFLECT_MELEE")) return TEXT("Melee Reflection: ");
		return nullptr;
	}

	// uitooltip.py AFFECT_APPEND_TEXT_DICT.
	const TCHAR* GetAffectSuffix(const FString& PointOnName)
	{
		return (PointOnName == TEXT("DODGE") || PointOnName == TEXT("RESIST_NORMAL")
			|| PointOnName == TEXT("REFLECT_MELEE")) ? TEXT("%") : TEXT("");
	}
}

TSharedRef<SWidget> UMT2SkillTooltipWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		RootBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TooltipBorder"));
		RootBorder->SetBrushColor(FLinearColor(0.02f, 0.02f, 0.03f, 0.92f));
		RootBorder->SetPadding(FMargin(8.0f, 6.0f));
		LinesBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TooltipLines"));
		RootBorder->SetContent(LinesBox);
		WidgetTree->RootWidget = RootBorder;
	}
	return Super::RebuildWidget();
}

void UMT2SkillTooltipWidget::AddLine(const FString& Text, const FLinearColor& Color, int32 FontSize)
{
	UTextBlock* Line = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Line->SetText(FText::FromString(Text));
	FSlateFontInfo Font = Line->GetFont();
	Font.Size = FontSize;
	Line->SetFont(Font);
	Line->SetColorAndOpacity(FSlateColor(Color));
	Line->SetAutoWrapText(true);
	if (UVerticalBoxSlot* LineSlot = LinesBox->AddChildToVerticalBox(Line))
	{
		LineSlot->SetPadding(FMargin(0.0f, 1.0f));
	}
	// Old tooltips are ~200px wide; wrapping keeps long descriptions in shape.
	Line->SetWrapTextAt(220.0f);
}

void UMT2SkillTooltipWidget::SetSkill(const UMT2SkillDefinition* Definition, int32 SkillLevel)
{
	// Ensure the tree exists even before the widget was ever displayed.
	TakeWidget();
	if (!LinesBox || !Definition)
	{
		return;
	}
	LinesBox->ClearChildren();

	// Old layout (uitooltip.py SetSkillNew): title + grade name, level limit, description, then a
	// block for the current level and a greyed one for the next, and finally the requirements.
	const EMT2SkillMastery Mastery = MT2SkillMastery::FromLevel(SkillLevel);
	const int32 Grade = FMath::Min(static_cast<int32>(Mastery), 2);
	const FString Name = Definition->GetGradeDisplayName(Grade).ToString();
	AddLine(Name.IsEmpty() ? Definition->InternalName : Name, TitleColor, 11);

	AppendLevelLimit(Definition);

	if (!Definition->Description.IsEmpty())
	{
		AddLine(Definition->Description.ToString(), BodyColor);
	}

	// Current level.
	if (SkillLevel > 0)
	{
		const bool bMaxed = SkillLevel >= Definition->MaxLevel;
		AddLine(bMaxed
			? FString::Printf(TEXT("Level: %d (Master)"), SkillLevel)
			: FString::Printf(TEXT("Level: %d"), SkillLevel), BodyColor);
		AppendLevelBlock(Definition, SkillLevel, StatColor);
	}

	// Next level, greyed - the old tooltip always previews what one more point buys.
	if (SkillLevel < Definition->MaxLevel)
	{
		AddLine(FString::Printf(TEXT("Next level: %d/%d"),
			SkillLevel + 1, Definition->MaxLevel), NextLevelColor);
		AppendLevelBlock(Definition, SkillLevel + 1, NextLevelColor);
	}

	AppendRequirements(Definition);
}

void UMT2SkillTooltipWidget::AppendLevelLimit(const UMT2SkillDefinition* Definition)
{
	if (Definition->LevelLimit <= 0)
	{
		return;
	}
	const AMT2PlayerState* State = GetOwningPlayerState<AMT2PlayerState>();
	const int32 PlayerLevel = State ? State->GetCharacterLevel() : 0;
	// Old tooltip paints an unmet requirement red.
	AddLine(FString::Printf(TEXT("Required level: %d"), Definition->LevelLimit),
		PlayerLevel < Definition->LevelLimit ? NegativeColor : BodyColor);
}

void UMT2SkillTooltipWidget::AppendLevelBlock(
	const UMT2SkillDefinition* Definition, int32 Level, const FLinearColor& Color)
{
	const AMT2PlayerCharacter* Player = Cast<AMT2PlayerCharacter>(GetOwningPlayerPawn());

	for (const FMT2SkillApplyDefinition& Apply : Definition->Applies)
	{
		if (Apply.PointOnName.IsEmpty() || Apply.PointOnName == TEXT("NONE"))
		{
			continue;
		}
		if (Apply.bGrandMasterOnly && MT2SkillMastery::FromLevel(Level) < EMT2SkillMastery::GrandMaster)
		{
			continue;
		}

		// The old client evaluates the point poly twice, forcing its random terms low then high, so
		// the value prints as a range. Without a pawn (no stats to read) fall back to the baked
		// per-level curve, which is the same poly with default stats.
		float MinValue = Apply.ValueByLevel.GetRichCurveConst()->Eval(Level);
		float MaxValue = MinValue;
		if (Player && !Apply.ValueFormula.IsEmpty())
		{
			double Evaluated = 0.0;
			if (MT2SkillFormula::Evaluate(Apply.ValueFormula,
				Player->BuildSkillFormulaVariables(Definition, Level, EMT2SkillFormulaValue::Min), Evaluated))
			{
				MinValue = static_cast<float>(Evaluated);
			}
			if (MT2SkillFormula::Evaluate(Apply.ValueFormula,
				Player->BuildSkillFormulaVariables(Definition, Level, EMT2SkillFormulaValue::Max), Evaluated))
			{
				MaxValue = static_cast<float>(Evaluated);
			}
		}

		// Old rule: an HP poly is damage while it is negative (printed as its magnitude), and a heal
		// once it is positive. This attack line was missing entirely.
		const bool bHeal = Apply.PointOnName == TEXT("HP") && MinValue >= 0.0f && MaxValue >= 0.0f;
		if (Apply.PointOnName == TEXT("HP") && !bHeal)
		{
			MinValue *= -1.0f;
			MaxValue *= -1.0f;
		}
		const TCHAR* Label = GetAffectLabel(Apply.PointOnName, bHeal);
		if (!Label)
		{
			// Unmapped POINT_ON: the old tooltip skips it rather than printing a raw name.
			continue;
		}

		FString Line = Label + FString::FromInt(FMath::RoundToInt(MinValue));
		if (FMath::RoundToInt(MinValue) != FMath::RoundToInt(MaxValue))
		{
			Line += TEXT(" - ") + FString::FromInt(FMath::RoundToInt(MaxValue));
		}
		Line += GetAffectSuffix(Apply.PointOnName);
		AddLine(Line, Color);

		const float Duration = Apply.DurationByLevel.GetRichCurveConst()->Eval(Level);
		if (Duration > 0.0f)
		{
			AddLine(FString::Printf(TEXT("Duration: %.0fs"), Duration), Color);
		}
	}

	const float Cooldown = Definition->CooldownByLevel.GetRichCurveConst()->Eval(Level);
	if (Cooldown > 0.0f)
	{
		AddLine(FString::Printf(TEXT("Cooldown: %.0fs"), Cooldown), Color);
	}
	const float SPCost = Definition->SPCostByLevel.GetRichCurveConst()->Eval(Level);
	if (SPCost > 0.0f)
	{
		// Old AppendNeedSP / AppendNeedHP: USE_HP_AS_COST skills spend HP instead.
		AddLine(FString::Printf(TEXT("%s: %.0f"),
			Definition->HasSkillFlag(EMT2SkillFlag::UseHPAsCost) ? TEXT("Required HP") : TEXT("Required SP"),
			SPCost), Color);
	}
}

void UMT2SkillTooltipWidget::AppendRequirements(const UMT2SkillDefinition* Definition)
{
	if (Definition->PrerequisiteSkillVnum <= 0)
	{
		return;
	}
	const AMT2PlayerState* State = GetOwningPlayerState<AMT2PlayerState>();
	const UMT2SkillComponent* Skills = State ? State->GetSkillComponent() : nullptr;
	const UMT2SkillDefinition* Required =
		Skills ? Skills->FindSkillDefinition(Definition->PrerequisiteSkillVnum) : nullptr;
	const int32 CurrentLevel = Skills ? Skills->GetSkillLevel(Definition->PrerequisiteSkillVnum) : 0;

	const FString RequiredName = Required
		? Required->GetGradeDisplayName(0).ToString()
		: FString::Printf(TEXT("skill %d"), Definition->PrerequisiteSkillVnum);
	// Old AppendSkillRequirement: green once satisfied, red while not.
	AddLine(FString::Printf(TEXT("Requires %s level %d"),
		*RequiredName, Definition->PrerequisiteSkillLevel),
		CurrentLevel >= Definition->PrerequisiteSkillLevel ? RequirementColor : NegativeColor);
}
