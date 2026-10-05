/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Skills/MT2SkillDefinition.h"

float UMT2SkillDefinition::GetPowerFactor(int32 SkillLevel) const
{
	if (SkillLevel <= 0)
	{
		return 0.0f;
	}
	const float PowerPercent = SkillPowerPercentByLevel.GetRichCurveConst()->Eval(
		static_cast<float>(SkillLevel));
	return PowerPercent * PowerScale / 100.0f;
}

TSoftObjectPtr<UAnimSequence> UMT2SkillDefinition::GetCastAnimation(int32 MasteryGrade, bool bFemale) const
{
	if (CastAnimationsByGrade.IsEmpty())
	{
		return nullptr;
	}
	const FMT2SkillGradeAnimation& Grade =
		CastAnimationsByGrade[FMath::Clamp(MasteryGrade, 0, CastAnimationsByGrade.Num() - 1)];
	return bFemale ? Grade.FemaleAnimation : Grade.MaleAnimation;
}

FText UMT2SkillDefinition::GetGradeDisplayName(int32 MasteryGrade) const
{
	if (GradeNames.IsEmpty())
	{
		return FText::FromString(InternalName);
	}
	return GradeNames[FMath::Clamp(MasteryGrade, 0, GradeNames.Num() - 1)];
}

bool UMT2SkillDefinition::HasClientAttribute(const FString& AttributeName) const
{
	if (ClientAttributeNames.IsEmpty() || AttributeName.IsEmpty())
	{
		return false;
	}
	TArray<FString> Names;
	ClientAttributeNames.ParseIntoArray(Names, TEXT("|"));
	for (FString& Name : Names)
	{
		Name.TrimStartAndEndInline();
		if (Name.Equals(AttributeName, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}
