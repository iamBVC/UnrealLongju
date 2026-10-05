/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Items/MT2InventoryComponent.h"

#include "Items/MT2ItemTemplate.h"

EMT2MetinSocketApplyResult UMT2InventoryComponent::ApplyMetinStone(
	int32 SourceSlot, int32 TargetSlot, bool bTargetEquipped)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Slots.IsValidIndex(SourceSlot) ||
		Slots[SourceSlot].IsEmpty())
	{
		return EMT2MetinSocketApplyResult::Invalid;
	}

	TArray<FMT2ItemSlot>& TargetContainer = bTargetEquipped ? Equipment : Slots;
	if (!TargetContainer.IsValidIndex(TargetSlot) || TargetContainer[TargetSlot].IsEmpty() ||
		(!bTargetEquipped && SourceSlot == TargetSlot))
	{
		return EMT2MetinSocketApplyResult::Invalid;
	}

	const UMT2ItemMetinStoneTemplate* Stone =
		Cast<UMT2ItemMetinStoneTemplate>(ResolveTemplate(Slots[SourceSlot].Vnum));
	const UMT2ItemEquipmentTemplate* EquipmentTemplate =
		Cast<UMT2ItemEquipmentTemplate>(ResolveTemplate(TargetContainer[TargetSlot].Vnum));
	if (!Stone || !EquipmentTemplate)
	{
		return EMT2MetinSocketApplyResult::Incompatible;
	}
	if ((Stone->CompatibleWearFlags & EquipmentTemplate->WearFlags) == 0)
	{
		return EMT2MetinSocketApplyResult::Incompatible;
	}

	FMT2ItemSlot& Target = TargetContainer[TargetSlot];
	if (Target.MetinSockets.IsEmpty() &&
		!EquipmentTemplate->DefaultInstanceData.MetinSockets.IsEmpty())
	{
		Target.MetinSockets = EquipmentTemplate->DefaultInstanceData.MetinSockets;
	}
	if (Target.MetinSockets.IsEmpty())
	{
		for (int32 Index = 0;
			Index < FMath::Clamp(EquipmentTemplate->DefaultSilverSocketCount, 0, 3); ++Index)
		{
			Target.MetinSockets.AddDefaulted_GetRef().Type = EMT2MetinSocketType::Silver;
		}
		for (int32 Index = 0;
			Index < FMath::Clamp(EquipmentTemplate->DefaultGoldSocketCount, 0,
				3 - Target.MetinSockets.Num()); ++Index)
		{
			Target.MetinSockets.AddDefaulted_GetRef().Type = EMT2MetinSocketType::Gold;
		}
	}
	const bool bHasCategory = Target.MetinSockets.ContainsByPredicate(
		[Stone](const FMT2MetinSocket& Socket)
	{
		const UMT2ItemMetinStoneTemplate* Existing = Socket.Stone.GetDefaultObject();
		return Existing && Existing->Vnum != 28960 &&
			Existing->StoneCategory == Stone->StoneCategory;
	});
	if (bHasCategory)
	{
		return EMT2MetinSocketApplyResult::DuplicateStone;
	}

	FMT2MetinSocket* OpenSocket = Target.MetinSockets.FindByPredicate(
		[Stone](const FMT2MetinSocket& Socket)
		{
			const int32 SocketGrade = Socket.Type == EMT2MetinSocketType::Gold ? 2 : 1;
			return !Socket.Stone && SocketGrade >= Stone->RequiredSocketGrade;
		});
	if (!OpenSocket)
	{
		return EMT2MetinSocketApplyResult::NoSocket;
	}

	const TSubclassOf<UMT2ItemMetinStoneTemplate> StoneClass(
		const_cast<UClass*>(Stone->GetClass()));
	if (--Slots[SourceSlot].Count <= 0)
	{
		Slots[SourceSlot] = FMT2ItemSlot();
	}

	const bool bSucceeded = FMath::FRandRange(0.0f, 100.0f) <=
		FMath::Clamp(Stone->AttachmentSuccessPercent, 0.0f, 100.0f);
	if (bSucceeded)
	{
		OpenSocket->Stone = StoneClass;
	}
	else
	{
		const UMT2ItemMetinStoneTemplate* Broken =
			Cast<UMT2ItemMetinStoneTemplate>(ResolveTemplate(28960));
		OpenSocket->Stone = Broken
			? TSubclassOf<UMT2ItemMetinStoneTemplate>(const_cast<UClass*>(Broken->GetClass()))
			: nullptr;
	}

	OnInventoryChanged.Broadcast();
	if (bTargetEquipped)
	{
		OnEquipmentChanged.Broadcast();
	}
	return bSucceeded
		? EMT2MetinSocketApplyResult::Success
		: EMT2MetinSocketApplyResult::Failed;
}
