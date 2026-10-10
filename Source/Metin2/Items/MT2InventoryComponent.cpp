/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Items/MT2InventoryComponent.h"
#include "Loot/MT2LootTable.h"

#include "Items/MT2ItemBonusSettings.h"
#include "Items/MT2ItemBonusUtils.h"

#include "Characters/MT2PlayerCharacter.h"
#include "Components/MT2HealthComponent.h"
#include "Components/MT2ManaComponent.h"
#include "Components/MT2StatusEffectComponent.h"
#include "Components/MT2StatusEffectDefinition.h"
#include "Config/MT2GameplaySettings.h"
#include "Core/MT2VnumRegistrySubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Items/MT2Item.h"
#include "Items/MT2InventoryLayout.h"
#include "Items/MT2ItemTemplate.h"
#include "Items/MT2ItemUtils.h"
#include "Items/Use/MT2ItemActionTemplate.h"
#include "Items/MT2WorldItem.h"
#include "Net/UnrealNetwork.h"
#include "Player/MT2PlayerState.h"
#include "Player/MT2PlayerController.h"
#include "Quests/MT2QuestManagerComponent.h"
#include "Stats/MT2PrimaryStatsComponent.h"

namespace
{
}

UMT2InventoryComponent::UMT2InventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UMT2InventoryComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (Slots.Num() != SlotCount)
		{
			Slots.SetNum(SlotCount);
		}
		if (Equipment.Num() != EquipmentCount)
		{
			Equipment.SetNum(EquipmentCount);
		}
	}
}

void UMT2InventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UMT2InventoryComponent, Slots, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UMT2InventoryComponent, Equipment, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UMT2InventoryComponent, PublicAppearance, COND_SkipOwner);
}

void UMT2InventoryComponent::PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);
	RefreshPublicAppearance();
}

void UMT2InventoryComponent::RefreshPublicAppearance()
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	PublicAppearance.BodyVnum = Equipment.IsValidIndex(0) && !Equipment[0].IsEmpty() ? Equipment[0].Vnum : 0;
	PublicAppearance.WeaponVnum = Equipment.IsValidIndex(4) && !Equipment[4].IsEmpty() ? Equipment[4].Vnum : 0;
}

void UMT2InventoryComponent::OnRep_PublicAppearance()
{
	// Observer compatibility view contains template identifiers only, never private instance data.
	Equipment.Reset(); Equipment.SetNum(EquipmentCount);
	Equipment[0].Vnum = PublicAppearance.BodyVnum;
	Equipment[0].Count = PublicAppearance.BodyVnum > 0 ? 1 : 0;
	Equipment[4].Vnum = PublicAppearance.WeaponVnum;
	Equipment[4].Count = PublicAppearance.WeaponVnum > 0 ? 1 : 0;
	OnEquipmentChanged.Broadcast();
}

const UMT2ItemTemplate* UMT2InventoryComponent::ResolveTemplate(int32 Vnum) const
{
	return MT2ItemUtils::ResolveTemplate(this, Vnum);
}

int32 UMT2InventoryComponent::GetItemSize(int32 Vnum) const
{
	const UMT2ItemTemplate* Template = ResolveTemplate(Vnum);
	return Template ? FMath::Clamp(Template->InventorySize, 1, 3) : 1;
}

bool UMT2InventoryComponent::HasEmptySpaceForItem(int32 Vnum) const
{
	const UMT2ItemTemplate* Template = ResolveTemplate(Vnum);
	if (!Template) { return false; }
	const int32 Size = FMath::Clamp(Template->InventorySize, 1, 3);
	for (int32 TopSlot = 0; TopSlot < Slots.Num(); ++TopSlot)
	{
		if (Slots[TopSlot].IsEmpty() && CanPlaceAt(TopSlot, Size)) { return true; }
	}
	return false;
}

int32 UMT2InventoryComponent::GetWearPosition(int32 Vnum) const
{
	if (Cast<UMT2ItemRodTemplate>(ResolveTemplate(Vnum))) { return static_cast<int32>(EMT2ItemWearFlag::Weapon); }
	const UMT2ItemEquipmentTemplate* EquipmentTemplate = Cast<UMT2ItemEquipmentTemplate>(ResolveTemplate(Vnum));
	if (!EquipmentTemplate || EquipmentTemplate->Vnum <= 0)
	{
		return INDEX_NONE;
	}

	auto FindFirstEmptyPosition = [this](int32 First, int32 Last) -> int32
	{
		for (int32 Position = First; Position <= Last; ++Position)
		{
			if (Equipment.IsValidIndex(Position) && Equipment[Position].IsEmpty())
			{
				return Position;
			}
		}
		return INDEX_NONE;
	};

	// These item types select equipment positions independently of item_proto's wearable mask.
	if (const UMT2ItemCostumeTemplate* Costume = Cast<UMT2ItemCostumeTemplate>(EquipmentTemplate))
	{
		if (Costume->CostumeSubType == 0) return static_cast<int32>(EMT2ItemWearFlag::CostumeBody);
		if (Costume->CostumeSubType == 1) return static_cast<int32>(EMT2ItemWearFlag::CostumeHair);
		return INDEX_NONE;
	}
	if (EquipmentTemplate->IsA<UMT2ItemRingTemplate>())
	{
		const int32 Ring1 = static_cast<int32>(EMT2ItemWearFlag::Ring1);
		return Equipment.IsValidIndex(Ring1) && !Equipment[Ring1].IsEmpty()
			? static_cast<int32>(EMT2ItemWearFlag::Ring2) : Ring1;
	}
	if (EquipmentTemplate->IsA<UMT2ItemBeltTemplate>())
	{
		return static_cast<int32>(EMT2ItemWearFlag::Belt);
	}

	// WearFlags stores the original item_proto EItemWearableFlag mask. Its bit indices diverge
	// from EWearPositions after EAR, so CountTrailingZeros put shields into UNIQUE2 (slot 8).
	const uint32 WearFlags = static_cast<uint32>(EquipmentTemplate->WearFlags);
	if (WearFlags & (1u << 0)) return static_cast<int32>(EMT2ItemWearFlag::Body);
	if (WearFlags & (1u << 1)) return static_cast<int32>(EMT2ItemWearFlag::Head);
	if (WearFlags & (1u << 2)) return static_cast<int32>(EMT2ItemWearFlag::Foots);
	if (WearFlags & (1u << 3)) return static_cast<int32>(EMT2ItemWearFlag::Wrist);
	if (WearFlags & (1u << 4)) return static_cast<int32>(EMT2ItemWearFlag::Weapon);
	if (WearFlags & (1u << 8)) return static_cast<int32>(EMT2ItemWearFlag::Shield);
	if (WearFlags & (1u << 5)) return static_cast<int32>(EMT2ItemWearFlag::Neck);
	if (WearFlags & (1u << 6)) return static_cast<int32>(EMT2ItemWearFlag::Ear);
	if (WearFlags & (1u << 9)) return static_cast<int32>(EMT2ItemWearFlag::Arrow);
	if (WearFlags & (1u << 7))
	{
		const int32 Unique1 = static_cast<int32>(EMT2ItemWearFlag::Unique1);
		return Equipment.IsValidIndex(Unique1) && !Equipment[Unique1].IsEmpty()
			? static_cast<int32>(EMT2ItemWearFlag::Unique2) : Unique1;
	}
	if (WearFlags & (1u << 11))
	{
		return FindFirstEmptyPosition(
			static_cast<int32>(EMT2ItemWearFlag::Ability1),
			static_cast<int32>(EMT2ItemWearFlag::Ability8));
	}
	return INDEX_NONE;
}

void UMT2InventoryComponent::RestoreItems(
	const TArray<FMT2ItemSlot>& InSlots, const TArray<FMT2ItemSlot>& InEquipment)
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	Slots.SetNum(SlotCount);
	Equipment.SetNum(EquipmentCount);
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		Slots[Index] = InSlots.IsValidIndex(Index) ? InSlots[Index] : FMT2ItemSlot();
	}
	for (int32 Index = 0; Index < Equipment.Num(); ++Index)
	{
		Equipment[Index] = InEquipment.IsValidIndex(Index) ? InEquipment[Index] : FMT2ItemSlot();
	}
	// Saves written before wearable-mask mapping was corrected stored shields in slot 8
	// (UNIQUE2). Repair that one unambiguous legacy placement while loading the character.
	const int32 LegacyShieldSlot = 8;
	const int32 ShieldSlot = static_cast<int32>(EMT2ItemWearFlag::Shield);
	if (Equipment.IsValidIndex(LegacyShieldSlot) && Equipment.IsValidIndex(ShieldSlot) &&
		!Equipment[LegacyShieldSlot].IsEmpty() && Equipment[ShieldSlot].IsEmpty())
	{
		const UMT2ItemEquipmentTemplate* LegacyTemplate = Cast<UMT2ItemEquipmentTemplate>(
			ResolveTemplate(Equipment[LegacyShieldSlot].Vnum));
		if (LegacyTemplate && (static_cast<uint32>(LegacyTemplate->WearFlags) & (1u << 8)) != 0)
		{
			Equipment[ShieldSlot] = Equipment[LegacyShieldSlot];
			Equipment[LegacyShieldSlot] = FMT2ItemSlot();
		}
	}
	OnInventoryChanged.Broadcast();
	OnEquipmentChanged.Broadcast();
}

bool UMT2InventoryComponent::IsCellOccupied(int32 Cell, int32 IgnoreSlotA, int32 IgnoreSlotB) const
{
	if (!Slots.IsValidIndex(Cell))
	{
		return true;
	}

	// Slots only stores each item's top/anchor cell. Reconstruct every existing item's vertical
	// footprint so covered cells are treated as occupied too.
	for (int32 AnchorSlot = 0; AnchorSlot < Slots.Num(); ++AnchorSlot)
	{
		if (AnchorSlot == IgnoreSlotA || AnchorSlot == IgnoreSlotB || Slots[AnchorSlot].IsEmpty())
		{
			continue;
		}

		const int32 ExistingSize = GetItemSize(Slots[AnchorSlot].Vnum);
		for (int32 Offset = 0; Offset < ExistingSize; ++Offset)
		{
			if (AnchorSlot + Offset * MT2InventoryLayout::Columns == Cell)
			{
				return true;
			}
		}
	}
	return false;
}

bool UMT2InventoryComponent::CanPlaceAt(
	int32 TopSlot, int32 Size, int32 IgnoreSlotA, int32 IgnoreSlotB) const
{
	if (!Slots.IsValidIndex(TopSlot))
	{
		return false;
	}
	// Multi-cell items extend downward within one page and cannot spill into the next inventory page.
	const int32 LocalTopSlot = TopSlot % PageSlotCount;
	const int32 TopRow = LocalTopSlot / MT2InventoryLayout::Columns;
	if (TopRow + Size > MT2InventoryLayout::Rows)
	{
		return false;
	}
	for (int32 Cell = 0; Cell < Size; ++Cell)
	{
		const int32 Index = TopSlot + Cell * MT2InventoryLayout::Columns;
		if (!Slots.IsValidIndex(Index))
		{
			return false;
		}
		if (IsCellOccupied(Index, IgnoreSlotA, IgnoreSlotB))
		{
			return false;
		}
	}
	return true;
}

bool UMT2InventoryComponent::AddItemByVnum(int32 Vnum, int32 Count)
{
	return AddItemByVnumPartial(Vnum, Count) > 0;
}

int32 UMT2InventoryComponent::AddItemByVnumPartial(int32 Vnum, int32 Count)
{
	const UMT2ItemTemplate* Template = ResolveTemplate(Vnum);
	if (!Template)
	{
		return 0;
	}
	FMT2ItemSlot Item(Vnum, Template->MakeInstanceData(Count));
	// This API accepts an aggregate quantity and may distribute it across several max-size stacks.
	Item.Count = Count;
	const int32 Added = AddItemSlotPartialInternal(Item, true);
	NotifyItemReceived(Item, Added);
	return Added;
}

void UMT2InventoryComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(AutoRecoveryTimer);
	}
	Super::EndPlay(EndPlayReason);
}

int32 UMT2InventoryComponent::AddItemInstancePartial(const UMT2Item* Item)
{
	if (!Item || !Item->IsValidItem())
	{
		return 0;
	}
	const FMT2ItemSlot ItemSlot = Item->ToItemSlot();
	const int32 Added = AddItemSlotPartialInternal(ItemSlot, false);
	NotifyItemReceived(ItemSlot, Added);
	return Added;
}

int32 UMT2InventoryComponent::AddItemSlotPartial(const FMT2ItemSlot& Item)
{
	const int32 Added = AddItemSlotPartialInternal(Item, false);
	NotifyItemReceived(Item, Added);
	return Added;
}

void UMT2InventoryComponent::NotifyItemReceived(const FMT2ItemSlot& Item, int32 AddedCount) const
{
	if (AddedCount <= 0) return;
	const AMT2PlayerCharacter* Character = Cast<AMT2PlayerCharacter>(GetOwner());
	if (AMT2PlayerController* Controller = Character
		? Cast<AMT2PlayerController>(Character->GetController()) : nullptr)
	{
		Controller->ClientNotifyItemReceived(Item.Vnum, AddedCount, Item.SkillVnum);
	}
}

int32 UMT2InventoryComponent::AddItemSlotPartialInternal(
	const FMT2ItemSlot& Item, bool bGenerateIntrinsicBonuses)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Item.Vnum <= 0 || Item.Count <= 0)
	{
		return 0;
	}
	const UMT2ItemTemplate* Template = ResolveTemplate(Item.Vnum);
	if (!Template)
	{
		return 0;
	}
	FMT2ItemSlot GeneratedItem = Item;
	Template->InitializeGeneratedInstance(GeneratedItem);
	GeneratedItem.bNewlyAcquired = true;
	if (Slots.Num() != SlotCount)
	{
		Slots.SetNum(SlotCount);
	}

	const int32 MaxStack = Template->GetMaxStackSize();
	const int32 Size = FMath::Clamp(Template->InventorySize, 1, 3);
	int32 Remaining = GeneratedItem.Count;

	if (MaxStack > 1)
	{
		for (FMT2ItemSlot& Slot : Slots)
		{
			if (Remaining <= 0) break;
			if (Slot.Count < MaxStack && MT2ItemUtils::HaveSameInstanceData(Slot, GeneratedItem))
			{
				const int32 Added = FMath::Min(Remaining, MaxStack - Slot.Count);
				Slot.Count += Added;
				Slot.bNewlyAcquired = true;
				Remaining -= Added;
			}
		}
	}

	// Cells covered by an already-placed multi-cell item aren't free even though the flat array only
	// stores the item at its top cell, so test the whole run with CanPlaceAt before claiming a slot.
	for (int32 TopSlot = 0; TopSlot < Slots.Num() && Remaining > 0; ++TopSlot)
	{
		if (Slots[TopSlot].IsEmpty() && CanPlaceAt(TopSlot, Size))
		{
			Slots[TopSlot] = GeneratedItem;
			Slots[TopSlot].Count = FMath::Min(Remaining, MaxStack);
			const UMT2ItemBonusSettings* BonusSettings = GetDefault<UMT2ItemBonusSettings>();
			const UMT2ItemEquipmentTemplate* EquipmentTemplate =
				Cast<UMT2ItemEquipmentTemplate>(Template);
			if (bGenerateIntrinsicBonuses && Slots[TopSlot].Bonuses.IsEmpty() &&
				EquipmentTemplate &&
				EquipmentTemplate->BonusAddonType == BonusSettings->DamageAddonType)
			{
				MT2ItemBonusUtils::AddDamageAddon(Slots[TopSlot], BonusSettings->DamageAddonFormula);
			}
			Remaining -= Slots[TopSlot].Count;
		}
	}

	if (Remaining == GeneratedItem.Count)
	{
		return 0;
	}
	OnInventoryChanged.Broadcast();
	return GeneratedItem.Count - Remaining;
}

EMT2LootCrateOpenResult UMT2InventoryComponent::OpenLootCrate(int32 CrateSlot, int32 KeySlot)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Slots.IsValidIndex(CrateSlot) ||
		Slots[CrateSlot].IsEmpty() || CrateSlot == KeySlot)
	{
		return EMT2LootCrateOpenResult::Invalid;
	}
	const UMT2ItemLootCrateTemplate* Crate =
		Cast<UMT2ItemLootCrateTemplate>(ResolveTemplate(Slots[CrateSlot].Vnum));
	if (!Crate)
	{
		return EMT2LootCrateOpenResult::Invalid;
	}

	const FMT2ItemLimit* LevelLimit = Crate->Limits.FindByPredicate(
		[](const FMT2ItemLimit& Limit)
		{
			return Limit.Type == EMT2ItemLimitType::Level;
		});
	const AMT2PlayerCharacter* Character = Cast<AMT2PlayerCharacter>(GetOwner());
	const AMT2PlayerState* OpeningPlayerState = Character
		? Character->GetPlayerState<AMT2PlayerState>() : nullptr;
	if (LevelLimit && LevelLimit->Value > 0 &&
		(!OpeningPlayerState || OpeningPlayerState->GetCharacterLevel() < LevelLimit->Value))
	{
		return EMT2LootCrateOpenResult::LevelTooLow;
	}

	const UMT2ItemTemplate* RequiredKey = Crate->RequiredKeyTemplate.GetDefaultObject();
	if (RequiredKey)
	{
		if (!Slots.IsValidIndex(KeySlot) || Slots[KeySlot].IsEmpty())
		{
			return EMT2LootCrateOpenResult::KeyRequired;
		}
		if (Slots[KeySlot].Vnum != RequiredKey->Vnum)
		{
			return EMT2LootCrateOpenResult::WrongKey;
		}
	}
	else if (KeySlot != INDEX_NONE)
	{
		return EMT2LootCrateOpenResult::WrongKey;
	}

	const UMT2LootTable* LootTable = Crate->LootTable.GetDefaultObject();
	if (!LootTable || !LootTable->HasValidRewards())
	{
		return EMT2LootCrateOpenResult::NoRewards;
	}

	auto ConsumeOne = [this](int32 SlotIndex)
	{
		if (--Slots[SlotIndex].Count <= 0)
		{
			Slots[SlotIndex] = FMT2ItemSlot();
		}
	};
	ConsumeOne(CrateSlot);
	if (RequiredKey)
	{
		ConsumeOne(KeySlot);
	}
	OnInventoryChanged.Broadcast();

	AMT2PlayerCharacter* MutableCharacter = Cast<AMT2PlayerCharacter>(GetOwner());
	AMT2PlayerState* PlayerState = MutableCharacter
		? MutableCharacter->GetPlayerState<AMT2PlayerState>() : nullptr;
	const FString OwnerId = AMT2WorldItem::ResolvePlayerIdentity(PlayerState);
	const FString OwnerName = PlayerState ? PlayerState->GetCharacterName() : FString();

	for (const FMT2LootTableResult& Result : LootTable->Roll())
	{
		if (Result.Kind == EMT2LootCrateRewardKind::Yang)
		{
			if (PlayerState)
			{
				PlayerState->AddYang(Result.Amount);
			}
			continue;
		}
		if (Result.Kind == EMT2LootCrateRewardKind::Experience)
		{
			if (PlayerState)
			{
				PlayerState->AddExperience(Result.Amount);
			}
			continue;
		}

		if (Result.Item.IsEmpty())
		{
			continue;
		}
		FMT2ItemSlot RewardSlot = Result.Item;
		RewardSlot.Count -= AddItemSlotPartial(RewardSlot);
		if (RewardSlot.Count > 0 && MutableCharacter)
		{
			AMT2WorldItem::SpawnWorldItemInstance(
				GetWorld(), MutableCharacter->GetActorLocation(), RewardSlot, MutableCharacter,
				OwnerId, OwnerName,
				UMT2GameplaySettings::Get().LootCrateOverflowOwnershipSeconds);
		}
	}
	return EMT2LootCrateOpenResult::Success;
}

bool UMT2InventoryComponent::MoveItem(int32 FromSlot, int32 ToSlot)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() ||
		!Slots.IsValidIndex(FromSlot) || !Slots.IsValidIndex(ToSlot) || FromSlot == ToSlot ||
		Slots[FromSlot].IsEmpty())
	{
		return false;
	}

	const int32 Size = GetItemSize(Slots[FromSlot].Vnum);
	// Moving into empty space: the whole run below the target must be free, ignoring the complete
	// footprint of the moving item.
	if (Slots[ToSlot].IsEmpty())
	{
		if (!CanPlaceAt(ToSlot, Size, FromSlot))
		{
			return false;
		}
		Slots[ToSlot] = Slots[FromSlot];
		Slots[FromSlot] = FMT2ItemSlot();
	}
	else
	{
		const int32 TargetSize = GetItemSize(Slots[ToSlot].Vnum);
		// A swap removes both items first, then both footprints must fit at their new anchors.
		if (!CanPlaceAt(ToSlot, Size, FromSlot, ToSlot) ||
			!CanPlaceAt(FromSlot, TargetSize, FromSlot, ToSlot))
		{
			return false;
		}
		Swap(Slots[FromSlot], Slots[ToSlot]);
	}
	OnInventoryChanged.Broadcast();
	return true;
}

bool UMT2InventoryComponent::UseItem(int32 InventorySlot)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() ||
		!Slots.IsValidIndex(InventorySlot) || Slots[InventorySlot].IsEmpty())
	{
		return false;
	}

	const UMT2ItemTemplate* ItemTemplate = ResolveTemplate(Slots[InventorySlot].Vnum);
	AMT2PlayerCharacter* Character = Cast<AMT2PlayerCharacter>(GetOwner());
	if (!ItemTemplate || !Character)
	{
		return false;
	}
	const UMT2ItemActionTemplate* ActionTemplate = Cast<UMT2ItemActionTemplate>(ItemTemplate);
	auto ExecuteAction = [&]() -> bool
	{
		if (!ActionTemplate)
		{
			return false;
		}
		const EMT2ItemUseExecution Result =
			ActionTemplate->ExecuteUse(*Character, *this, InventorySlot);
		if (Result == EMT2ItemUseExecution::Consume)
		{
			return ConsumeItemAtSlot(InventorySlot);
		}
		return Result == EMT2ItemUseExecution::Accepted;
	};
	if (ActionTemplate && ActionTemplate->ExecuteBeforeQuestItemUse())
	{
		return ExecuteAction();
	}

	// Quests get first refusal on a used item (the old `when <vnum>.use` trigger), and the slot travels
	// with the event so item.* reads resolve against this exact item. A quest that handles the use
	// consumes it entirely, so a quest item never also runs the generic consumable path.
	if (AMT2PlayerState* QuestOwner = Character->GetPlayerState<AMT2PlayerState>())
	{
		if (UMT2QuestManagerComponent* QuestManager = QuestOwner->GetQuestManagerComponent())
		{
			if (QuestManager->DispatchEvent(
				EMT2QuestEvent::ItemUse, Slots[InventorySlot].Vnum, nullptr, InventorySlot))
			{
				return true;
			}
		}
	}

	if (ActionTemplate)
	{
		return ExecuteAction();
	}

	const UMT2ItemUseTemplate* UseTemplate = Cast<UMT2ItemUseTemplate>(ItemTemplate);
	if (!UseTemplate)
	{
		return false;
	}
	// Old EUseSubTypes (common/enums.h): USE_POTION=0, USE_ABILITY_UP=7, USE_POTION_NODELAY=11.
	// Every effect goes through the one status-effect component; each kind's logic lives in its own
	// UMT2StatusEffectDefinition subclass. See Docs/OldGameResearch/PotionsAndAffects.md.
	bool bUsed = false;
	const int32 ItemVnum = Slots[InventorySlot].Vnum;
	const UMT2GameplaySettings& GameplaySettings = UMT2GameplaySettings::Get();
	const bool bCooldownBypassItem =
		ItemVnum == GameplaySettings.SkillBookCooldownBypassItemVnum;
	const bool bGuaranteedReadItem =
		ItemVnum == GameplaySettings.SkillBookGuaranteedSuccessItemVnum;
	if (bCooldownBypassItem || bGuaranteedReadItem)
	{
		UMT2StatusEffectComponent* StatusEffects = Character->GetStatusEffectComponent();
		const int32 AffectType = bCooldownBypassItem
			? MT2AffectId::SkillBookNoCooldown
			: MT2AffectId::SkillBookGuaranteedSuccess;
		if (!StatusEffects || StatusEffects->HasEffectType(AffectType))
		{
			return false;
		}
		FMT2StatusEffect Effect;
		Effect.Type = AffectType;
		Effect.SourceItemVnum = ItemVnum;
		Effect.RemainingSeconds = -1;
		Effect.Icon = UseTemplate->Icon;
		bUsed = StatusEffects->AddEffect(Effect, false);
	}
	else switch (UseTemplate->UseSubType)
	{
	case 6: // USE_BAIT
		return ApplyFishingBait(InventorySlot);
	case 0: // USE_POTION: add VALUE0 HP / VALUE1 SP into a single over-time recovery pool per resource.
	{
		UMT2StatusEffectComponent* StatusEffects = Character->GetStatusEffectComponent();
		if (!StatusEffects)
		{
			return false;
		}
		// One shared pool per resource: a drink adds to whatever is still pending, capped so the target
		// (current + pool) never exceeds the maximum, and the pool drains at a constant rate regardless
		// of how many potions are queued. Taking damage lowers current, so the reachable target follows
		// it down automatically. Rejected (no consume) once current+pending already reaches the max.
		const auto QueueRecovery = [&](EMT2StatusEffectKind Kind, int32 AffectType, int32 Amount, float Current, float Max)
		{
			if (Amount <= 0)
			{
				return;
			}
			const int32 Headroom = FMath::Max(0, FMath::FloorToInt(Max - Current));
			const int32 Existing = StatusEffects->GetEffectValueByType(AffectType);
			const int32 NewPool = FMath::Min(Existing + Amount, Headroom);
			if (NewPool <= Existing)
			{
				return; // already full or nothing more to gain
			}
			FMT2StatusEffect Effect;
			Effect.Kind = Kind;
			Effect.Type = AffectType;
			Effect.SourceItemVnum = ItemVnum;
			Effect.ApplyValue = NewPool;    // total HP/SP still to recover
			Effect.RemainingSeconds = -1;   // pool-terminated, not on a timer
			Effect.bRemoveOnDeath = true;
			Effect.Icon = UseTemplate->Icon;
			bUsed |= StatusEffects->AddEffect(Effect, /*bOverride*/ true); // override = merge the pool
		};
		if (UMT2HealthComponent* Health = Character->GetHealthComponent())
		{
			QueueRecovery(EMT2StatusEffectKind::HealthRecovery, MT2AffectId::HealthRecovery,
				UseTemplate->HealthRecovery, Health->GetHealth(), Health->GetMaxHealth());
		}
		if (UMT2ManaComponent* Mana = Character->GetManaComponent())
		{
			QueueRecovery(EMT2StatusEffectKind::ManaRecovery, MT2AffectId::ManaRecovery,
				UseTemplate->ManaRecovery, Mana->GetMana(), Mana->GetMaxMana());
		}
		break;
	}
	case 7: // USE_ABILITY_UP: VALUE0=apply type, VALUE1=duration(s), VALUE2=magnitude(%). Non-stacking.
	{
		UMT2StatusEffectComponent* StatusEffects = Character->GetStatusEffectComponent();
		if (!StatusEffects || UseTemplate->BuffDurationSeconds <= 0)
		{
			return false;
		}
		FMT2StatusEffect Effect;
		Effect.SourceItemVnum = ItemVnum;
		Effect.ApplyType = static_cast<int32>(UseTemplate->GrantedBonus);
		Effect.ApplyValue = UseTemplate->GrantedBonusValue;
		Effect.RemainingSeconds = UseTemplate->BuffDurationSeconds;
		Effect.bRemoveOnDeath = true;
		Effect.Icon = UseTemplate->Icon;
		if (UseTemplate->GrantedBonus == EMT2ItemBonusType::AttackSpeed)
		{
			Effect.Kind = EMT2StatusEffectKind::AttackSpeed;
			Effect.Type = MT2AffectId::AttackSpeed;
		}
		else if (UseTemplate->GrantedBonus == EMT2ItemBonusType::MovementSpeed)
		{
			Effect.Kind = EMT2StatusEffectKind::MovementSpeed;
			Effect.Type = MT2AffectId::MovementSpeed;
		}
		else
		{
			return false; // only attack/move-speed buffs are wired up for now
		}
		bUsed = StatusEffects->AddEffect(Effect, false); // rejected (false) if the buff is still active
		break;
	}
	case 8: // USE_AFFECT: VALUE0=affect id, VALUE1=apply type, VALUE2=value, VALUE3=duration.
	{
		UMT2StatusEffectComponent* StatusEffects = Character->GetStatusEffectComponent();
		if (!StatusEffects || UseTemplate->AffectType <= 0 ||
			UseTemplate->GrantedBonus == EMT2ItemBonusType::None ||
			UseTemplate->BuffDurationSeconds <= 0)
		{
			return false;
		}
		const int32 ApplyType = static_cast<int32>(UseTemplate->GrantedBonus);
		if (StatusEffects->GetEffects().ContainsByPredicate(
			[UseTemplate, ApplyType](const FMT2StatusEffect& Existing)
			{
				return Existing.Type == UseTemplate->AffectType && Existing.ApplyType == ApplyType;
			}))
		{
			return false;
		}
		FMT2StatusEffect Effect;
		Effect.Kind = EMT2StatusEffectKind::StatBonus;
		Effect.Type = UseTemplate->AffectType;
		Effect.SourceItemVnum = ItemVnum;
		Effect.ApplyType = ApplyType;
		Effect.ApplyValue = UseTemplate->GrantedBonusValue;
		Effect.RemainingSeconds = UseTemplate->BuffDurationSeconds;
		Effect.Icon = UseTemplate->Icon;
		bUsed = StatusEffects->AddEffect(Effect, false);
		break;
	}
	case 10: // USE_SPECIAL: toggle a finite automatic HP/MP recovery reservoir.
	{
		const UMT2ItemAutoRecoveryTemplate* AutoRecovery =
			Cast<UMT2ItemAutoRecoveryTemplate>(UseTemplate);
		return AutoRecovery && ToggleAutoRecoveryItem(InventorySlot, *AutoRecovery);
	}
	case 11: // USE_POTION_NODELAY: instant flat (VALUE0/1) + percent-of-max (VALUE3/4) HP/SP restore.
	{
		if (UMT2HealthComponent* Health = Character->GetHealthComponent())
		{
			const float Recovery = UseTemplate->HealthRecovery +
				Health->GetMaxHealth() * UseTemplate->HealthRecoveryPercent / 100.0f;
			if (Recovery > 0.0f && Health->GetHealth() < Health->GetMaxHealth())
			{
				Health->SetCurrentValue(FMath::Min(Health->GetHealth() + Recovery, Health->GetMaxHealth()));
				bUsed = true;
			}
		}
		if (UMT2ManaComponent* Mana = Character->GetManaComponent())
		{
			const float Recovery = UseTemplate->ManaRecovery +
				Mana->GetMaxMana() * UseTemplate->ManaRecoveryPercent / 100.0f;
			if (Recovery > 0.0f && Mana->GetMana() < Mana->GetMaxMana())
			{
				Mana->SetCurrentValue(FMath::Min(Mana->GetMana() + Recovery, Mana->GetMaxMana()));
				bUsed = true;
			}
		}
		break;
	}
	default:
		return false;
	}

	if (!bUsed)
	{
		return false;
	}

	if (--Slots[InventorySlot].Count <= 0)
	{
		Slots[InventorySlot] = FMT2ItemSlot();
	}
	OnInventoryChanged.Broadcast();
	return true;
}

int64 UMT2InventoryComponent::CountItemsInVnumRange(int32 FirstVnum, int32 LastVnum) const
{
	int64 Total = 0;
	for (const FMT2ItemSlot& Slot : Slots)
	{
		if (!Slot.IsEmpty() && Slot.Vnum >= FirstVnum && Slot.Vnum <= LastVnum)
		{
			Total += Slot.Count;
		}
	}
	return Total;
}

bool UMT2InventoryComponent::RemoveItemsInVnumRange(int32 Count, int32 FirstVnum, int32 LastVnum)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Count <= 0 || FirstVnum < 0 ||
		CountItemsInVnumRange(FirstVnum, LastVnum) < Count)
	{
		return false;
	}
	TArray<int32> Candidates;
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		if (!Slots[Index].IsEmpty() && Slots[Index].Vnum >= FirstVnum && Slots[Index].Vnum <= LastVnum)
		{
			Candidates.Add(Index);
		}
	}
	Candidates.Sort([this](int32 A, int32 B)
	{
		return Slots[A].Vnum == Slots[B].Vnum ? A < B : Slots[A].Vnum < Slots[B].Vnum;
	});
	// Preflight is complete before touching any stack. Publish one inventory change after commit,
	// so observers cannot interleave a second mutation between legacy per-vnum removals.
	int32 Remaining = Count;
	TArray<FMT2ItemSlot> RecoveryItems;
	for (int32 Index : Candidates)
	{
		const int32 Taken = FMath::Min(Remaining, Slots[Index].Count);
		if (Taken > 0 && Slots[Index].bAutoRecoveryActive) { RecoveryItems.Add(Slots[Index]); }
		Slots[Index].Count -= Taken;
		Remaining -= Taken;
		if (Slots[Index].Count <= 0) { Slots[Index] = FMT2ItemSlot(); }
		if (Remaining == 0) { break; }
	}
	// Affect delegates can run external code, so defer them until no live slot is being edited.
	for (const FMT2ItemSlot& Item : RecoveryItems) { StopAutoRecoveryEffectForSlot(Item); }
	OnInventoryChanged.Broadcast();
	return true;
}

int32 UMT2InventoryComponent::RemoveItemByVnum(int32 Vnum, int32 Count)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Vnum <= 0 || Count <= 0)
	{
		return 0;
	}
	// All-or-nothing: quest costs shouldn't half-consume a player's items when they're short.
	if (CountItemByVnum(Vnum) < Count)
	{
		return 0;
	}
	int32 Remaining = Count;
	for (int32 Index = 0; Index < Slots.Num() && Remaining > 0; ++Index)
	{
		if (Slots[Index].Vnum != Vnum || Slots[Index].Count <= 0)
		{
			continue;
		}
		const int32 Taken = FMath::Min(Remaining, Slots[Index].Count);
		if (Taken > 0 && Slots[Index].bAutoRecoveryActive)
		{
			StopAutoRecoveryEffectForSlot(Slots[Index]);
		}
		Slots[Index].Count -= Taken;
		Remaining -= Taken;
		if (Slots[Index].Count <= 0)
		{
			Slots[Index] = FMT2ItemSlot();
		}
	}
	OnInventoryChanged.Broadcast();
	return Count - Remaining;
}

bool UMT2InventoryComponent::SetItemSocketValue(int32 InventorySlot, int32 SocketIndex, int32 Value)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || SocketIndex < 0 ||
		!Slots.IsValidIndex(InventorySlot) || Slots[InventorySlot].IsEmpty())
	{
		return false;
	}
	FMT2ItemSlot& Item = Slots[InventorySlot];
	// Quest items are given scratch sockets on demand; a stone mounted later overrides the reported
	// value, so growing the array here cannot disturb real socket contents.
	if (Item.MetinSockets.Num() <= SocketIndex)
	{
		Item.MetinSockets.SetNum(SocketIndex + 1);
	}
	Item.MetinSockets[SocketIndex].Value = Value;
	OnInventoryChanged.Broadcast();
	return true;
}

bool UMT2InventoryComponent::SetItemBonusValue(
	int32 InventorySlot, int32 BonusIndex, int32 ApplyType, int32 Value)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || BonusIndex < 0 || BonusIndex >= 7 ||
		!Slots.IsValidIndex(InventorySlot) || Slots[InventorySlot].IsEmpty() ||
		!StaticEnum<EMT2ItemBonusType>()->IsValidEnumValue(ApplyType))
	{
		return false;
	}
	TArray<FMT2ItemBonus>& Bonuses = Slots[InventorySlot].Bonuses;
	if (Bonuses.Num() <= BonusIndex) { Bonuses.SetNum(BonusIndex + 1); }
	FMT2ItemBonus& Bonus = Bonuses[BonusIndex];
	Bonus.Type = static_cast<EMT2ItemBonusType>(ApplyType);
	Bonus.Value = Value;
	Bonus.Kind = BonusIndex >= 5 ? EMT2ItemBonusKind::Rare : EMT2ItemBonusKind::Normal;
	OnInventoryChanged.Broadcast();
	return true;
}

bool UMT2InventoryComponent::CopyAndReplaceQuestItem(
	int32 InventorySlot, const FMT2ItemSlot& ExpectedSource, int32 ResultVnum,
	const TMap<int32, int32>& Materials)
{
	return CopyAndReplaceQuestItemWithTemplates(InventorySlot, ExpectedSource,
		ResultVnum, ResolveTemplate(ExpectedSource.Vnum), ResolveTemplate(ResultVnum), Materials);
}

bool UMT2InventoryComponent::CopyAndReplaceQuestItemWithTemplates(
	int32 InventorySlot, const FMT2ItemSlot& ExpectedSource,
	int32 ResultVnum, const UMT2ItemTemplate* SourceTemplate, const UMT2ItemTemplate* ResultTemplate,
	const TMap<int32, int32>& Materials)
{
	const auto CoversVnum = [](const UMT2ItemTemplate* Template, int32 Vnum)
	{
		return Template && Template->Vnum > 0 && Vnum >= Template->Vnum &&
			static_cast<int64>(Vnum) <= static_cast<int64>(Template->Vnum) + FMath::Max(Template->VnumRange, 0);
	};
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Slots.IsValidIndex(InventorySlot) ||
		ExpectedSource.IsEmpty() || ExpectedSource.Count != 1 || ExpectedSource.bAutoRecoveryActive ||
		!CoversVnum(SourceTemplate, ExpectedSource.Vnum) ||
		!SourceTemplate->IsA<UMT2ItemEquipmentTemplate>() || !ResultTemplate ||
		!ResultTemplate->IsA<UMT2ItemEquipmentTemplate>() || !CoversVnum(ResultTemplate, ResultVnum) ||
		ResultVnum == ExpectedSource.Vnum ||
		!CanPlaceAt(InventorySlot, FMath::Clamp(ResultTemplate->InventorySize, 1, 3), InventorySlot))
	{
		return false;
	}
	FMT2ItemSlot ComparableSource = ExpectedSource;
	ComparableSource.bNewlyAcquired = Slots[InventorySlot].bNewlyAcquired;
	if (!FMT2ItemSlot::StaticStruct()->CompareScriptStruct(&Slots[InventorySlot], &ComparableSource, 0)) { return false; }
	const UMT2ItemArmorTemplate* SourceArmor = Cast<UMT2ItemArmorTemplate>(SourceTemplate);
	const UMT2ItemArmorTemplate* ResultArmor = Cast<UMT2ItemArmorTemplate>(ResultTemplate);
	const bool bAccessory = SourceTemplate->IsA<UMT2ItemBeltTemplate>() || (SourceArmor &&
		(SourceArmor->ArmorSubType == 3 || SourceArmor->ArmorSubType == 5 || SourceArmor->ArmorSubType == 6));
	const bool bResultAccessory = ResultTemplate->IsA<UMT2ItemBeltTemplate>() || (ResultArmor &&
		(ResultArmor->ArmorSubType == 3 || ResultArmor->ArmorSubType == 5 || ResultArmor->ArmorSubType == 6));
	if (bAccessory != bResultAccessory || ExpectedSource.MetinSockets.Num() > 3) { return false; }

	FMT2ItemSlot Replacement;
	Replacement.Vnum = ResultVnum;
	Replacement.Bonuses = ExpectedSource.Bonuses;
	Replacement.bNewlyAcquired = true;
	if (bAccessory)
	{
		Replacement.AccessorySockets = ExpectedSource.AccessorySockets;
	}
	else
	{
		// ITEM_MANAGER::CopyAllAttrTo opens silver sockets, then packs surviving stones from the
		// front. Broken Metin (28960) is discarded; unrelated per-item runtime state is not copied.
		Replacement.MetinSockets.SetNum(ExpectedSource.MetinSockets.Num());
		int32 StoneSlot = 0;
		for (const FMT2MetinSocket& Socket : ExpectedSource.MetinSockets)
		{
			const UMT2ItemMetinStoneTemplate* Stone = Socket.Stone.GetDefaultObject();
			if (Stone && Stone->Vnum <= 0) { return false; }
			if (Stone && Stone->Vnum != 28960)
			{
				Replacement.MetinSockets[StoneSlot++].Stone = Socket.Stone;
			}
			else if (!Stone && Socket.Value > 2 && Socket.Value != 28960)
			{
				return false; // Unknown numeric socket payload: do not silently destroy it.
			}
		}
	}

	TArray<FMT2ItemSlot> Candidate = Slots;
	for (const auto& Material : Materials)
	{
		if (Material.Key <= 0 || Material.Value <= 0 || Material.Key == ExpectedSource.Vnum) { return false; }
		int32 Remaining = Material.Value;
		for (int32 Index = 0; Index < Candidate.Num() && Remaining > 0; ++Index)
		{
			FMT2ItemSlot& Slot = Candidate[Index];
			if (Index == InventorySlot || Slot.IsEmpty() || Slot.Vnum != Material.Key) { continue; }
			if (Slot.bAutoRecoveryActive) { return false; }
			const int32 Removed = FMath::Min(Remaining, Slot.Count);
			Remaining -= Removed;
			Slot.Count -= Removed;
			if (Slot.Count == 0) { Slot = FMT2ItemSlot(); }
		}
		if (Remaining > 0) { return false; }
	}
	Candidate[InventorySlot] = MoveTemp(Replacement);
	Slots = MoveTemp(Candidate);
	OnInventoryChanged.Broadcast();
	GetOwner()->ForceNetUpdate();
	return true;
}

bool UMT2InventoryComponent::ConsumeItemAtSlot(int32 InventorySlot)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() ||
		!Slots.IsValidIndex(InventorySlot) || Slots[InventorySlot].IsEmpty())
	{
		return false;
	}
	StopAutoRecoveryEffectForSlot(Slots[InventorySlot]);
	if (--Slots[InventorySlot].Count <= 0)
	{
		Slots[InventorySlot] = FMT2ItemSlot();
	}
	OnInventoryChanged.Broadcast();
	return true;
}

const UMT2ItemTemplate* UMT2InventoryComponent::GetTemplateAtSlot(int32 InventorySlot) const
{
	return Slots.IsValidIndex(InventorySlot) ? ResolveTemplate(Slots[InventorySlot].Vnum) : nullptr;
}

bool UMT2InventoryComponent::ExtractItemForDrop(
	int32 InventorySlot, int32 Count, FMT2ItemSlot& OutDroppedItem)
{
	OutDroppedItem = FMT2ItemSlot();
	if (!GetOwner() || !GetOwner()->HasAuthority() ||
		!Slots.IsValidIndex(InventorySlot) || Slots[InventorySlot].IsEmpty())
	{
		return false;
	}

	const UMT2ItemTemplate* Template = ResolveTemplate(Slots[InventorySlot].Vnum);
	constexpr int32 AntiDrop = 1 << 7;
	constexpr int32 AntiGive = 1 << 13;
	if (!Template || Slots[InventorySlot].bAutoRecoveryActive ||
		(Template->AntiFlags & (AntiDrop | AntiGive)) != 0)
	{
		return false;
	}

	const int32 DropCount = Count <= 0
		? Slots[InventorySlot].Count : FMath::Min(Count, Slots[InventorySlot].Count);
	if (DropCount <= 0)
	{
		return false;
	}

	// Preserve every per-instance field (bonuses, sockets, skill-book skill and recovery state).
	// Constructing only from Vnum/count made a successfully dropped split stack lose its identity.
	OutDroppedItem = Slots[InventorySlot];
	OutDroppedItem.Count = DropCount;
	OutDroppedItem.bNewlyAcquired = false;
	Slots[InventorySlot].Count -= DropCount;
	if (Slots[InventorySlot].Count <= 0)
	{
		Slots[InventorySlot] = FMT2ItemSlot();
	}
	OnInventoryChanged.Broadcast();
	return true;
}

void UMT2InventoryComponent::ServerMarkItemSeen_Implementation(int32 InventorySlot)
{
	if (!Slots.IsValidIndex(InventorySlot) || Slots[InventorySlot].IsEmpty() ||
		!Slots[InventorySlot].bNewlyAcquired)
	{
		return;
	}
	Slots[InventorySlot].bNewlyAcquired = false;
	OnInventoryChanged.Broadcast();
}

bool UMT2InventoryComponent::ExecuteAtomicTrade(
	UMT2InventoryComponent& Other,
	const TArray<int32>& OwnSlots, const TArray<FMT2ItemSlot>& OwnExpected,
	const TArray<int32>& OtherSlots, const TArray<FMT2ItemSlot>& OtherExpected)
{
	if (&Other == this || !GetOwner() || !GetOwner()->HasAuthority() ||
		OwnSlots.Num() != OwnExpected.Num() || OtherSlots.Num() != OtherExpected.Num())
	{
		return false;
	}
	auto Validate = [](UMT2InventoryComponent& Inventory, const TArray<int32>& Indices,
		const TArray<FMT2ItemSlot>& Expected)
	{
		TSet<int32> Unique;
		for (int32 Index = 0; Index < Indices.Num(); ++Index)
		{
			const int32 SlotIndex = Indices[Index];
			if (!Inventory.Slots.IsValidIndex(SlotIndex) || Unique.Contains(SlotIndex) ||
				Inventory.Slots[SlotIndex].IsEmpty() ||
				Inventory.Slots[SlotIndex].Vnum != Expected[Index].Vnum ||
				Inventory.Slots[SlotIndex].Count != Expected[Index].Count ||
				!MT2ItemUtils::HaveSameInstanceData(Inventory.Slots[SlotIndex], Expected[Index]))
			{
				return false;
			}
			Unique.Add(SlotIndex);
			const UMT2ItemTemplate* Template = Inventory.ResolveTemplate(Expected[Index].Vnum);
			constexpr int32 AntiGive = 1 << 13;
			if (!Template || (Template->AntiFlags & AntiGive) != 0)
			{
				return false;
			}
			if (Inventory.Slots[SlotIndex].bAutoRecoveryActive)
			{
				return false;
			}
		}
		return true;
	};
	if (!Validate(*this, OwnSlots, OwnExpected) || !Validate(Other, OtherSlots, OtherExpected))
	{
		return false;
	}

	const TArray<FMT2ItemSlot> OwnBackup = Slots;
	const TArray<FMT2ItemSlot> OtherBackup = Other.Slots;
	for (const int32 SlotIndex : OwnSlots) Slots[SlotIndex] = FMT2ItemSlot();
	for (const int32 SlotIndex : OtherSlots) Other.Slots[SlotIndex] = FMT2ItemSlot();

	bool bComplete = true;
	for (const FMT2ItemSlot& Item : OtherExpected)
	{
		bComplete &= AddItemSlotPartialInternal(Item, false) == Item.Count;
	}
	for (const FMT2ItemSlot& Item : OwnExpected)
	{
		bComplete &= Other.AddItemSlotPartialInternal(Item, false) == Item.Count;
	}
	if (!bComplete)
	{
		Slots = OwnBackup;
		Other.Slots = OtherBackup;
	}
	else
	{
		for (const FMT2ItemSlot& Item : OtherExpected) NotifyItemReceived(Item, Item.Count);
		for (const FMT2ItemSlot& Item : OwnExpected) Other.NotifyItemReceived(Item, Item.Count);
	}
	OnInventoryChanged.Broadcast();
	Other.OnInventoryChanged.Broadcast();
	return bComplete;
}

bool UMT2InventoryComponent::CanTradeItemAtSlot(int32 InventorySlot) const
{
	if (!Slots.IsValidIndex(InventorySlot) || Slots[InventorySlot].IsEmpty()) return false;
	const UMT2ItemTemplate* Template = ResolveTemplate(Slots[InventorySlot].Vnum);
	constexpr int32 AntiGive = 1 << 13;
	return Template && !Slots[InventorySlot].bAutoRecoveryActive &&
		(Template->AntiFlags & AntiGive) == 0;
}

void UMT2InventoryComponent::OnRep_Slots()
{
	OnInventoryChanged.Broadcast();
}

void UMT2InventoryComponent::OnRep_Equipment()
{
	OnEquipmentChanged.Broadcast();
}
