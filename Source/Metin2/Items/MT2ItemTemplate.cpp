/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Items/MT2ItemTemplate.h"

#include "UI/MT2ItemTooltipWidget.h"
#include "UI/MT2ItemTooltipWidgets.h"
#include "Skills/MT2SkillSet.h"

void UMT2ItemTemplate::PostLoad()
{
	Super::PostLoad();
	if (!Sockets_DEPRECATED.IsEmpty())
	{
		Sockets_DEPRECATED.Reset();
	}
}

FMT2ItemInstanceData UMT2ItemTemplate::MakeInstanceData(int32 Count) const
{
	FMT2ItemInstanceData Result = DefaultInstanceData;
	Result.Count = FMath::Clamp(Count, 1, GetMaxStackSize());
	InitializeGeneratedInstance(Result);
	return Result;
}

void UMT2ItemAutoRecoveryTemplate::InitializeGeneratedInstance(
	FMT2ItemInstanceData& InOutData) const
{
	if (InOutData.AutoRecoveryMaximumAmount <= 0)
	{
		InOutData.AutoRecoveryMaximumAmount = FMath::Max(RecoveryCapacity, 0);
		InOutData.AutoRecoveryRemainingAmount = InOutData.AutoRecoveryMaximumAmount;
	}
	InOutData.AutoRecoveryRemainingAmount = FMath::Clamp(
		InOutData.AutoRecoveryRemainingAmount, 0, InOutData.AutoRecoveryMaximumAmount);
}

void UMT2ItemSkillBookTemplate::InitializeGeneratedInstance(
	FMT2ItemInstanceData& InOutData) const
{
	if (InOutData.SkillVnum > 0)
	{
		return;
	}
	if (SkillVnum > 0)
	{
		InOutData.SkillVnum = SkillVnum;
		return;
	}

	// The old server rolls vnum 50300 from the available job skills and stores the selected skill
	// in socket 0. Imported definitions are the authority here, so nonexistent skills cannot roll.
	const TArray<int32> SkillVnums = UMT2SkillSet::GetSkillVnumsInRange(1, 111);
	if (!SkillVnums.IsEmpty())
	{
		InOutData.SkillVnum = SkillVnums[FMath::RandHelper(SkillVnums.Num())];
	}
}

TSubclassOf<UMT2ItemTooltipWidget> UMT2ItemTemplate::ResolveTooltipClass() const
{
	if (TooltipWidgetOverride)
	{
		return TooltipWidgetOverride;
	}
	return GetDefaultTooltipClass();
}

TSubclassOf<UMT2ItemTooltipWidget> UMT2ItemTemplate::GetDefaultTooltipClass() const
{
	// Base/material/quest/etc.: name + description + any bonuses.
	return UMT2ItemTooltipWidget::StaticClass();
}

TSubclassOf<UMT2ItemTooltipWidget> UMT2ItemWeaponTemplate::GetDefaultTooltipClass() const
{
	return UMT2WeaponTooltipWidget::StaticClass();
}

TSubclassOf<UMT2ItemTooltipWidget> UMT2ItemArmorTemplate::GetDefaultTooltipClass() const
{
	return UMT2ArmorTooltipWidget::StaticClass();
}

TSubclassOf<UMT2ItemTooltipWidget> UMT2ItemRingTemplate::GetDefaultTooltipClass() const
{
	return UMT2AccessoryTooltipWidget::StaticClass();
}

TSubclassOf<UMT2ItemTooltipWidget> UMT2ItemBeltTemplate::GetDefaultTooltipClass() const
{
	return UMT2AccessoryTooltipWidget::StaticClass();
}

TSubclassOf<UMT2ItemTooltipWidget> UMT2ItemUseTemplate::GetDefaultTooltipClass() const
{
	return UMT2UseItemTooltipWidget::StaticClass();
}
