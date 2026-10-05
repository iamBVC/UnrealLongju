/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Npcs/MT2NpcTypes.h"
#include "MT2NpcShopComponent.generated.h"

class AMT2PlayerCharacter;
class UMT2ItemTemplate;

// A shop NPC's stock, living in the NPC Blueprint's class defaults (no replication needed - the
// class travels to clients; only transactions are server RPCs). Prices come from the item
// templates' BuyPrice/SellPrice unless a shop_item row overrides them.
UCLASS(ClassGroup = "MT2", meta = (BlueprintSpawnableComponent))
class METIN2_API UMT2NpcShopComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Shop")
	const TArray<FMT2ShopEntry>& GetShopEntries() const { return ShopEntries; }

	// What one purchase of this entry costs: the item's BuyPrice (item_proto "gold") times the
	// entry's stack count, exactly like the old shop.cpp's item.price = dwGold * item.count.
	UFUNCTION(BlueprintPure, Category = "Shop")
	int64 GetEntryPrice(int32 EntryIndex) const;

	// What the shop pays for Count of an item: old shop_manager.cpp does
	// shop_buy_price * count / 5, minus a 3% tax.
	UFUNCTION(BlueprintPure, Category = "Shop")
	static int64 CalculateSellPayout(const UMT2ItemTemplate* Template, int32 Count);

	// Server: purchase one shop entry (Count stacks of it). Validates range/yang/space.
	bool BuyItem(AMT2PlayerCharacter* Player, int32 EntryIndex, int32 Count);

	// Server: sell an inventory stack to this shop at the template SellPrice.
	bool SellItem(AMT2PlayerCharacter* Player, int32 InventorySlot, int32 Count);

	void SetShopEntries(const TArray<FMT2ShopEntry>& NewEntries) { ShopEntries = NewEntries; }

	// Server-side validation before a quest requests the owner's shop window.
	bool CanOpenFor(const AMT2PlayerCharacter* Player) const;

private:
	bool IsPlayerInRange(const AMT2PlayerCharacter* Player) const;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shop", meta = (AllowPrivateAccess = "true"))
	TArray<FMT2ShopEntry> ShopEntries;
};
