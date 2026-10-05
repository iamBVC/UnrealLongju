/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Items/MT2InventoryComponent.h"

#include "Items/MT2ItemBonusSettings.h"
#include "Items/MT2ItemBonusUtils.h"
#include "Items/MT2ItemTemplate.h"

EMT2ItemBonusApplyResult UMT2InventoryComponent::ApplyBonusConsumable(
	int32 SourceSlot, int32 TargetSlot, bool bTargetEquipped)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || (!bTargetEquipped && SourceSlot == TargetSlot) ||
		!Slots.IsValidIndex(SourceSlot) || Slots[SourceSlot].IsEmpty())
	{
		return EMT2ItemBonusApplyResult::Invalid;
	}
	TArray<FMT2ItemSlot>& TargetContainer = bTargetEquipped ? Equipment : Slots;
	if (!TargetContainer.IsValidIndex(TargetSlot) || TargetContainer[TargetSlot].IsEmpty())
	{
		return EMT2ItemBonusApplyResult::Invalid;
	}

	const UMT2ItemBonusSettings* Settings = GetDefault<UMT2ItemBonusSettings>();
	const int32 SourceVnum = Slots[SourceSlot].Vnum;
	const UMT2ItemTemplate* TargetTemplate = ResolveTemplate(TargetContainer[TargetSlot].Vnum);
	const EMT2ItemBonusTarget TargetType = MT2ItemBonusUtils::ResolveTarget(TargetTemplate);
	if (!Settings || TargetType == EMT2ItemBonusTarget::None)
	{
		return EMT2ItemBonusApplyResult::Invalid;
	}

	FMT2ItemSlot& Target = TargetContainer[TargetSlot];
	const auto CountKind = [&Target](EMT2ItemBonusKind Kind)
	{
		int32 Count = 0;
		for (const FMT2ItemBonus& Bonus : Target.Bonuses)
		{
			Count += Bonus.IsValid() && Bonus.Kind == Kind ? 1 : 0;
		}
		return Count;
	};
	const int32 NormalCount = CountKind(EMT2ItemBonusKind::Normal);
	const int32 RareCount = CountKind(EMT2ItemBonusKind::Rare);
	bool bValidUse = false;
	bool bBonusChanged = false;

	if (SourceVnum == Settings->AddNormalBonusItemVnum && NormalCount < Settings->NormalBonusAddLimit)
	{
		if (FMath::FRand() <= Settings->AddNormalBonusSuccessChance)
		{
			bBonusChanged = MT2ItemBonusUtils::AddRandomBonus(
				Target, Settings->NormalBonuses, Settings->AddNormalValueLevelWeights,
				TargetType, EMT2ItemBonusKind::Normal);
			bValidUse = bBonusChanged;
		}
		else
		{
			bValidUse = true;
		}
	}
	else if (SourceVnum == Settings->ChangeNormalBonusesItemVnum && NormalCount > 0)
	{
		bValidUse = true;
		const TArray<FMT2ItemBonus> OriginalBonuses = Target.Bonuses;
		Target.Bonuses.RemoveAll([](const FMT2ItemBonus& Bonus)
		{
			return Bonus.Kind == EMT2ItemBonusKind::Normal;
		});
		const UMT2ItemEquipmentTemplate* EquipmentTarget = Cast<UMT2ItemEquipmentTemplate>(TargetTemplate);
		if (EquipmentTarget && EquipmentTarget->BonusAddonType == Settings->DamageAddonType &&
			NormalCount >= 2)
		{
			MT2ItemBonusUtils::AddDamageAddon(Target, Settings->DamageAddonFormula);
		}
		while (CountKind(EMT2ItemBonusKind::Normal) < NormalCount)
		{
			if (!MT2ItemBonusUtils::AddRandomBonus(
				Target, Settings->NormalBonuses, Settings->ChangeNormalValueLevelWeights,
				TargetType, EMT2ItemBonusKind::Normal))
			{
				break;
			}
		}
		if (CountKind(EMT2ItemBonusKind::Normal) != NormalCount)
		{
			Target.Bonuses = OriginalBonuses;
			bValidUse = false;
		}
		else
		{
			bBonusChanged = true;
		}
	}
	else if (SourceVnum == Settings->AddFifthBonusItemVnum &&
		NormalCount == Settings->NormalBonusAddLimit && NormalCount < Settings->NormalBonusMaximum)
	{
		bBonusChanged = MT2ItemBonusUtils::AddRandomBonus(
			Target, Settings->NormalBonuses, Settings->AddNormalValueLevelWeights,
			TargetType, EMT2ItemBonusKind::Normal);
		bValidUse = bBonusChanged;
	}
	else if (SourceVnum == Settings->AddRareBonusItemVnum && RareCount < Settings->RareBonusMaximum)
	{
		bBonusChanged = MT2ItemBonusUtils::AddRandomBonus(
			Target, Settings->RareBonuses, Settings->RareValueLevelWeights,
			TargetType, EMT2ItemBonusKind::Rare);
		bValidUse = bBonusChanged;
	}
	else if (SourceVnum == Settings->ChangeRareBonusesItemVnum && RareCount > 0)
	{
		bValidUse = true;
		const TArray<FMT2ItemBonus> OriginalBonuses = Target.Bonuses;
		Target.Bonuses.RemoveAll([](const FMT2ItemBonus& Bonus)
		{
			return Bonus.Kind == EMT2ItemBonusKind::Rare;
		});
		for (int32 Index = 0; Index < RareCount; ++Index)
		{
			if (!MT2ItemBonusUtils::AddRandomBonus(
				Target, Settings->RareBonuses, Settings->RareValueLevelWeights,
				TargetType, EMT2ItemBonusKind::Rare))
			{
				break;
			}
		}
		if (CountKind(EMT2ItemBonusKind::Rare) != RareCount)
		{
			Target.Bonuses = OriginalBonuses;
			bValidUse = false;
		}
		else
		{
			bBonusChanged = true;
		}
	}

	if (!bValidUse)
	{
		return EMT2ItemBonusApplyResult::Invalid;
	}
	if (--Slots[SourceSlot].Count <= 0)
	{
		Slots[SourceSlot] = FMT2ItemSlot();
	}
	OnInventoryChanged.Broadcast();
	if (bTargetEquipped)
	{
		OnEquipmentChanged.Broadcast();
	}
	return bBonusChanged ? EMT2ItemBonusApplyResult::Success : EMT2ItemBonusApplyResult::Failed;
}
