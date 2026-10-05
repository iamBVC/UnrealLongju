/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Items/MT2ItemUtils.h"

#include "Core/MT2VnumRegistrySubsystem.h"
#include "Core/MT2WorldUtils.h"
#include "Engine/GameInstance.h"
#include "Items/MT2ItemTemplate.h"
#include "Items/MT2ItemTypes.h"

const UMT2ItemTemplate* MT2ItemUtils::ResolveTemplate(const UObject* WorldContextObject, int32 Vnum)
{
	if (Vnum <= 0)
	{
		return nullptr;
	}
	UGameInstance* GameInstance = MT2WorldUtils::GetGameInstance(WorldContextObject);
	UMT2VnumRegistrySubsystem* Registry = GameInstance
		? GameInstance->GetSubsystem<UMT2VnumRegistrySubsystem>() : nullptr;
	const TSubclassOf<UMT2ItemTemplate> TemplateClass = Registry
		? Registry->ResolveItemTemplateClass(Vnum) : nullptr;
	return TemplateClass.GetDefaultObject();
}

int32 MT2ItemUtils::GetInventorySize(const UObject* WorldContextObject, int32 Vnum)
{
	const UMT2ItemTemplate* Template = ResolveTemplate(WorldContextObject, Vnum);
	return Template ? FMath::Clamp(Template->InventorySize, 1, 3) : 1;
}

FText MT2ItemUtils::GetDisplayName(const UMT2ItemTemplate* Template, int32 FallbackVnum)
{
	if (!Template)
	{
		return FallbackVnum > 0 ? FText::AsNumber(FallbackVnum) : FText::GetEmpty();
	}
	return Template->DisplayName.IsEmpty()
		? FText::FromString(Template->InternalName) : Template->DisplayName;
}

bool MT2ItemUtils::HaveSameInstanceData(const FMT2ItemSlot& Left, const FMT2ItemSlot& Right)
{
	if (Left.Vnum != Right.Vnum ||
		Left.SkillVnum != Right.SkillVnum ||
		Left.AutoRecoveryRemainingAmount != Right.AutoRecoveryRemainingAmount ||
		Left.AutoRecoveryMaximumAmount != Right.AutoRecoveryMaximumAmount ||
		Left.bAutoRecoveryActive != Right.bAutoRecoveryActive ||
		Left.MetinSockets.Num() != Right.MetinSockets.Num() ||
		!(Left.AccessorySockets == Right.AccessorySockets) ||
		Left.Bonuses.Num() != Right.Bonuses.Num())
	{
		return false;
	}
	for (int32 Index = 0; Index < Left.MetinSockets.Num(); ++Index)
	{
		const FMT2MetinSocket& A = Left.MetinSockets[Index];
		const FMT2MetinSocket& B = Right.MetinSockets[Index];
		if (A.Type != B.Type || A.Stone.Get() != B.Stone.Get())
		{
			return false;
		}
	}
	for (int32 Index = 0; Index < Left.Bonuses.Num(); ++Index)
	{
		const FMT2ItemBonus& A = Left.Bonuses[Index];
		const FMT2ItemBonus& B = Right.Bonuses[Index];
		if (A.Type != B.Type || A.Value != B.Value || A.Kind != B.Kind)
		{
			return false;
		}
	}
	return true;
}
