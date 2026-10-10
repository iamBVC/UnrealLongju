/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Items/MT2ItemTypes.h"
#include "MT2InventoryComponent.generated.h"

class UMT2ItemTemplate;
class UMT2ItemAutoRecoveryTemplate;
class UMT2Item;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMT2InventoryChangedSignature);

USTRUCT()
struct METIN2_API FMT2EquipmentAppearance
{
	GENERATED_BODY()
	UPROPERTY() int32 BodyVnum = 0;
	UPROPERTY() int32 WeaponVnum = 0;
};

// Slot-based, server-authoritative item grid replicated to the owning client, matching the old game's
// inventory model. Inventory slots (four 45-cell pages) and equipment slots (indexed by EWearPositions
// from the old client's enums.h: BODY=0, HEAD=1, FOOTS=2, WRIST=3, WEAPON=4, NECK=5, EAR=6, SHIELD=10, ...)
// live in this one component; the character reacts to OnEquipmentChanged to apply meshes and stat bonuses.
UCLASS(BlueprintType, ClassGroup = "MT2", meta = (BlueprintSpawnableComponent))
class METIN2_API UMT2InventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	static constexpr int32 PageSlotCount = 45;
	static constexpr int32 PageCount = 4;
	static constexpr int32 SlotCount = PageSlotCount * PageCount;
	// Indexed by EWearPositions; sized to cover the highest wear slot the equipment panel uses (glove=28).
	static constexpr int32 EquipmentCount = 32;

	UMT2InventoryComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// Server-only. Adds Count of Vnum into the first free run of cells big enough for the item's size,
	// stacking onto matching slots first. Returns true if anything was placed.
	bool AddItemByVnum(int32 Vnum, int32 Count);

	// Server-only. Adds as much as fits and returns the exact quantity inserted.
	int32 AddItemByVnumPartial(int32 Vnum, int32 Count);

	int32 AddItemInstancePartial(const UMT2Item* Item);
	int32 AddItemSlotPartial(const FMT2ItemSlot& Item);

	EMT2LootCrateOpenResult OpenLootCrate(int32 CrateSlot, int32 KeySlot = INDEX_NONE);

	// Server-only. Moves/swaps an item between two inventory slots.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory")
	bool MoveItem(int32 FromSlot, int32 ToSlot);

	// Server-only. Applies one of the configured bonus consumables at SourceSlot to the equipment
	// item at TargetSlot. The source is consumed only when the request is valid.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory|Bonuses")
	EMT2ItemBonusApplyResult ApplyBonusConsumable(
		int32 SourceSlot, int32 TargetSlot, bool bTargetEquipped = false);

	// Server-only. Consumes one Metin stone and attempts to install it into the first compatible
	// open socket. The target may be in inventory or already equipped.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory|Sockets")
	EMT2MetinSocketApplyResult ApplyMetinStone(
		int32 SourceSlot, int32 TargetSlot, bool bTargetEquipped = false);

	// Server-only atomic refinement. ScrollSlot is INDEX_NONE for blacksmith refinement.
	EMT2RefinementResult RefineItem(int32 TargetSlot, int32 ScrollSlot = INDEX_NONE);

	// Read-only eligibility check shared by client UI and server validation. A maxed item, missing
	// result template, or item without an imported refine_proto recipe returns false.
	UFUNCTION(BlueprintPure, Category = "Inventory|Refinement")
	bool CanRefineItem(int32 TargetSlot) const;

	UFUNCTION(BlueprintPure, Category = "Inventory")
	int32 CountItemByVnum(int32 Vnum) const;
	int64 CountItemsInVnumRange(int32 FirstVnum, int32 LastVnum) const;
	// Server-only, all-or-nothing; consumes ascending vnums, then inventory slot order.
	bool RemoveItemsInVnumRange(int32 Count, int32 FirstVnum, int32 LastVnum);

	// Server-only. Removes up to Count of Vnum across stacks (old pc.remove_item). Returns how many
	// were actually removed; removes nothing and returns 0 when the player doesn't have enough.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory")
	int32 RemoveItemByVnum(int32 Vnum, int32 Count);

	// Server-only. Writes the scripted numeric value of a socket on an inventory item (old
	// item.set_socket). Grows the socket array if the item has fewer sockets than the index asks for.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory|Sockets")
	bool SetItemSocketValue(int32 InventorySlot, int32 SocketIndex, int32 Value);

	// Server-only old item.set_value: writes one intrinsic attribute slot on the item that raised a
	// quest event. Slots 0-4 are normal attributes and 5-6 are rare attributes.
	bool SetItemBonusValue(int32 InventorySlot, int32 BonusIndex, int32 ApplyType, int32 Value);

	// Quest item-copy transaction. Replaces one equipment item in place and consumes the complete
	// material batch atomically. ExpectedSource is a server-owned snapshot from before the dialog.
	bool CopyAndReplaceQuestItem(int32 InventorySlot, const FMT2ItemSlot& ExpectedSource,
		int32 ResultVnum, const TMap<int32, int32>& Materials);

	// Server-only. Equips the item at InventorySlot into its wear position (removing it from the grid),
	// swapping any currently-equipped item back into that inventory slot. Returns true on success.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory")
	bool EquipItemFromSlot(int32 InventorySlot);

	// Server-only. Moves an equipped item back into the first free inventory slot.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory")
	bool UnequipItem(int32 WearPosition);

	// Server-only. Atomically moves every equipped item into free inventory cells. Nothing changes
	// when the complete equipment set cannot fit.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory")
	bool UnequipAllItems();
	bool ExtractRandomEquippedItem(FMT2ItemSlot& OutItem);

	// Server-only. Uses one consumable item. Basic HP/MP potions are supported now; other use-item
	// subtypes remain available for their dedicated gameplay implementations.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory")
	bool UseItem(int32 InventorySlot);
	bool ApplyFishingBait(int32 InventorySlot);
	bool FinishFishingAttempt(bool bPractice);
	int32 RefineFishingRod(int32 InventorySlot);

	// Owning-client acknowledgement sent when the new item's tooltip is first displayed.
	UFUNCTION(Server, Reliable)
	void ServerMarkItemSeen(int32 InventorySlot);

	// Server-only. Removes one item from a specific slot (e.g. consuming the skill book that was
	// right-clicked). Returns true if the slot held an item.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory")
	bool ConsumeItemAtSlot(int32 InventorySlot);

	// Resolves the item template at a slot (for reading the item's data before acting on it).
	const UMT2ItemTemplate* GetTemplateAtSlot(int32 InventorySlot) const;

	// Server-only. Removes up to Count from an inventory stack for a world drop. Anti-drop and
	// anti-give items are rejected, matching the original server's DropItem validation.
	bool ExtractItemForDrop(int32 InventorySlot, int32 Count, FMT2ItemSlot& OutDroppedItem);

	// Server-only all-or-nothing exchange. Offered slots are revalidated against the expected item
	// snapshots immediately before commit. Both inventories are restored byte-for-byte on failure.
	bool ExecuteAtomicTrade(
		UMT2InventoryComponent& Other,
		const TArray<int32>& OwnSlots, const TArray<FMT2ItemSlot>& OwnExpected,
		const TArray<int32>& OtherSlots, const TArray<FMT2ItemSlot>& OtherExpected);

	UFUNCTION(BlueprintPure, Category = "Inventory")
	const TArray<FMT2ItemSlot>& GetSlots() const { return Slots; }

	UFUNCTION(BlueprintPure, Category = "Inventory")
	// Server/owner: complete instances. Observers: body/weapon identifiers only.
	const TArray<FMT2ItemSlot>& GetEquipment() const { return Equipment; }

	// Server-only restore used after the player's normalized item rows are loaded.
	void RestoreItems(const TArray<FMT2ItemSlot>& InSlots, const TArray<FMT2ItemSlot>& InEquipment);

	// Maps original item_proto wearable flags and item subtypes to the matching equipment position.
	int32 GetWearPosition(int32 Vnum) const;

	// Item height in inventory cells (item_proto Size, clamped 1-3).
	int32 GetItemSize(int32 Vnum) const;
	// Legacy enough_inventory requires empty grid cells, not room in an existing stack.
	bool HasEmptySpaceForItem(int32 Vnum) const;
	bool CanTradeItemAtSlot(int32 InventorySlot) const;

	UPROPERTY(BlueprintAssignable, Category = "Inventory")
	FMT2InventoryChangedSignature OnInventoryChanged;

	UPROPERTY(BlueprintAssignable, Category = "Inventory")
	FMT2InventoryChangedSignature OnEquipmentChanged;

protected:
	virtual void BeginPlay() override;

private:
	friend class FMT2ObserverReplicationTest;
	UFUNCTION() void OnRep_PublicAppearance();
	void RefreshPublicAppearance();
	friend class FMT2QuestItemCopyTest;
	bool CopyAndReplaceQuestItemWithTemplates(int32 InventorySlot, const FMT2ItemSlot& ExpectedSource,
		int32 ResultVnum, const UMT2ItemTemplate* SourceTemplate, const UMT2ItemTemplate* ResultTemplate,
		const TMap<int32, int32>& Materials);
	UFUNCTION()
	void OnRep_Slots();

	UFUNCTION()
	void OnRep_Equipment();

	const UMT2ItemTemplate* ResolveTemplate(int32 Vnum) const;
	void NotifyItemReceived(const FMT2ItemSlot& Item, int32 AddedCount) const;
	int32 AddItemSlotPartialInternal(const FMT2ItemSlot& Item, bool bGenerateIntrinsicBonuses);
	bool CanEquipTemplate(const UMT2ItemTemplate* Template) const;
	bool IsCellOccupied(int32 Cell, int32 IgnoreSlotA = INDEX_NONE, int32 IgnoreSlotB = INDEX_NONE) const;
	// True if all cells the item (of the given size) would occupy starting at TopSlot are free/in-bounds.
	bool CanPlaceAt(
		int32 TopSlot, int32 Size, int32 IgnoreSlotA = INDEX_NONE, int32 IgnoreSlotB = INDEX_NONE) const;
	bool ToggleAutoRecoveryItem(int32 InventorySlot, const UMT2ItemAutoRecoveryTemplate& Template);
	void ProcessAutoRecoveryItems();
	void RefreshAutoRecoveryEffect(const UMT2ItemAutoRecoveryTemplate& Template, int32 RemainingAmount);
	void StartAutoRecoveryTimerIfNeeded();
	void StopAutoRecoveryEffectForSlot(const FMT2ItemSlot& Slot);

	UPROPERTY(ReplicatedUsing = OnRep_Slots)
	TArray<FMT2ItemSlot> Slots;

	UPROPERTY(ReplicatedUsing = OnRep_Equipment)
	TArray<FMT2ItemSlot> Equipment;
	UPROPERTY(ReplicatedUsing = OnRep_PublicAppearance)
	FMT2EquipmentAppearance PublicAppearance;

	FTimerHandle AutoRecoveryTimer;
};
