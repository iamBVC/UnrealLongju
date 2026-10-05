/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2ItemTooltipWidgets.h"

#include "Items/MT2ItemTemplate.h"

void UMT2WeaponTooltipWidget::BuildTypeSpecific(
	const UMT2ItemTemplate* Template, const AMT2PlayerState* Viewer)
{
	const UMT2ItemWeaponTemplate* Weapon = Cast<UMT2ItemWeaponTemplate>(Template);
	if (!Weapon)
	{
		return;
	}
	AddSpace();
	const int32 PhysMin = Weapon->GetPhysicalDamageMin();
	const int32 PhysMax = Weapon->GetPhysicalDamageMax();
	if (PhysMax > 0)
	{
		AddLine(PhysMax > PhysMin
			? FString::Printf(TEXT("Attack Power: %d - %d"), PhysMin, PhysMax)
			: FString::Printf(TEXT("Attack Power: %d"), PhysMin), PositiveColor);
	}
	const int32 MagicMin = Weapon->GetMagicDamageMin();
	const int32 MagicMax = Weapon->GetMagicDamageMax();
	if (MagicMax > 0)
	{
		AddLine(MagicMax > MagicMin
			? FString::Printf(TEXT("Magic Attack: %d - %d"), MagicMin, MagicMax)
			: FString::Printf(TEXT("Magic Attack: %d"), MagicMin), PositiveColor);
	}
	AddApplies(Template);
}

void UMT2ArmorTooltipWidget::BuildTypeSpecific(
	const UMT2ItemTemplate* Template, const AMT2PlayerState* Viewer)
{
	const UMT2ItemArmorTemplate* Armor = Cast<UMT2ItemArmorTemplate>(Template);
	if (!Armor)
	{
		return;
	}
	const int32 DefenseGrade = Armor->Defense;
	const int32 DefenseBonus = Armor->RefinementDefense * 2;
	if (DefenseGrade > 0)
	{
		AddSpace();
		AddLine(FString::Printf(TEXT("Defence: %d"), DefenseGrade + DefenseBonus), PositiveColor);
	}
	AddApplies(Template);
}

void UMT2AccessoryTooltipWidget::BuildTypeSpecific(
	const UMT2ItemTemplate* Template, const AMT2PlayerState* Viewer)
{
	AddSpace();
	AddApplies(Template);
}

void UMT2UseItemTooltipWidget::BuildTypeSpecific(
	const UMT2ItemTemplate* Template, const AMT2PlayerState* Viewer)
{
	const UMT2ItemUseTemplate* Use = Cast<UMT2ItemUseTemplate>(Template);
	if (!Use)
	{
		AddApplies(Template);
		return;
	}
	switch (Use->UseSubType)
	{
	case 0:  // USE_POTION: over-time restore
	case 11: // USE_POTION_NODELAY: instant restore
		if (Use->HealthRecovery > 0) AddLine(FString::Printf(TEXT("Restores %d HP"), Use->HealthRecovery), PositiveColor);
		if (Use->ManaRecovery > 0) AddLine(FString::Printf(TEXT("Restores %d MP"), Use->ManaRecovery), PositiveColor);
		if (Use->UseSubType == 11)
		{
			if (Use->HealthRecoveryPercent > 0) AddLine(FString::Printf(TEXT("Restores %d%% of Max HP"), Use->HealthRecoveryPercent), PositiveColor);
			if (Use->ManaRecoveryPercent > 0) AddLine(FString::Printf(TEXT("Restores %d%% of Max MP"), Use->ManaRecoveryPercent), PositiveColor);
		}
		break;

	case 7: // USE_ABILITY_UP: timed stat buff (VALUE0=apply, VALUE1=duration s, VALUE2=magnitude)
	{
		AddLine(FormatApply(static_cast<int32>(Use->GrantedBonus), Use->GrantedBonusValue), PositiveColor);
		const int32 Duration = Use->BuffDurationSeconds;
		if (Duration > 0)
		{
			const int32 Minutes = Duration / 60;
			const int32 Seconds = Duration % 60;
			const FString DurationText = (Minutes > 0 && Seconds > 0)
				? FString::Printf(TEXT("Duration: %dm %ds"), Minutes, Seconds)
				: (Minutes > 0 ? FString::Printf(TEXT("Duration: %d min"), Minutes)
							   : FString::Printf(TEXT("Duration: %d sec"), Seconds));
			AddLine(DurationText, NormalColor);
		}
		break;
	}

	default:
		AddApplies(Template);
		break;
	}
}
