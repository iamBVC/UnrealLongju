/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Npcs/MT2NpcShopComponent.h"

#include "Characters/MT2PlayerCharacter.h"
#include "Items/MT2InventoryComponent.h"
#include "Items/MT2ItemTemplate.h"
#include "Items/MT2ItemUtils.h"
#include "Npcs/MT2Npc.h"
#include "Npcs/MT2NpcInteractionComponent.h"
#include "Player/MT2PlayerState.h"
#include "Trade/MT2TradeComponent.h"

namespace
{
	int64 MultiplyCurrency(int64 Amount, int32 Count)
	{
		if (Amount <= 0 || Count <= 0)
		{
			return 0;
		}
		return Amount > MAX_int64 / Count ? MAX_int64 : Amount * Count;
	}
}

int64 UMT2NpcShopComponent::GetEntryPrice(int32 EntryIndex) const
{
	if (!ShopEntries.IsValidIndex(EntryIndex))
	{
		return 0;
	}
	const FMT2ShopEntry& Entry = ShopEntries[EntryIndex];
	if (Entry.PriceOverride > 0)
	{
		return Entry.PriceOverride;
	}
	// Old shop.cpp: item.price = item_table->dwGold * item.count (BuyPrice is item_proto "gold").
	const UMT2ItemTemplate* Template = MT2ItemUtils::ResolveTemplate(this, Entry.ItemVnum);
	return Template ? MultiplyCurrency(Template->BuyPrice, FMath::Max(Entry.Count, 1)) : 0;
}

int64 UMT2NpcShopComponent::CalculateSellPayout(const UMT2ItemTemplate* Template, int32 Count)
{
	if (!Template)
	{
		return 0;
	}
	// Old shop_manager.cpp: price = shop_buy_price * count, /= 5, then a 3% tax is removed.
	int64 Price = MultiplyCurrency(Template->SellPrice, FMath::Max(Count, 1));
	Price /= 5;
	const int64 Tax = (Price / 100) * 3 + ((Price % 100) * 3) / 100;
	return FMath::Max<int64>(Price - Tax, 0);
}

bool UMT2NpcShopComponent::IsPlayerInRange(const AMT2PlayerCharacter* Player) const
{
	const AMT2Npc* Npc = Cast<AMT2Npc>(GetOwner());
	if (!Npc || !Player)
	{
		return false;
	}
	const float Range = Npc->GetInteractionComponent()
		? Npc->GetInteractionComponent()->GetInteractionRange() + 100.0f : 350.0f;
	return FVector::Dist2D(Npc->GetActorLocation(), Player->GetActorLocation()) <= Range;
}

bool UMT2NpcShopComponent::CanOpenFor(const AMT2PlayerCharacter* Player) const
{
	const AActor* Npc = GetOwner();
	const AMT2PlayerState* State = Player ? Player->GetPlayerState<AMT2PlayerState>() : nullptr;
	return IsValid(Npc) && IsValid(Player) && Npc->HasAuthority() && Player->HasAuthority() &&
		Npc->GetWorld() == Player->GetWorld() && !ShopEntries.IsEmpty() && IsPlayerInRange(Player) &&
		(!State || !State->GetTradeComponent() || !State->GetTradeComponent()->IsTrading());
}

bool UMT2NpcShopComponent::BuyItem(AMT2PlayerCharacter* Player, int32 EntryIndex, int32 Count)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !ShopEntries.IsValidIndex(EntryIndex) ||
		Count <= 0 || !IsPlayerInRange(Player))
	{
		return false;
	}
	const FMT2ShopEntry& Entry = ShopEntries[EntryIndex];
	// GetEntryPrice already covers the entry's stack count; Count is how many of those purchases.
	const int64 TotalPrice = MultiplyCurrency(GetEntryPrice(EntryIndex), Count);
	AMT2PlayerState* State = Player->GetPlayerState<AMT2PlayerState>();
	UMT2InventoryComponent* Inventory = Player->GetInventoryComponent();
	if (!State || !Inventory || State->GetYang() < TotalPrice)
	{
		return false;
	}

	const int32 EntryCount = FMath::Max(Entry.Count, 1);
	if (Count > MAX_int32 / EntryCount)
	{
		return false;
	}
	const int32 Requested = EntryCount * Count;
	const int32 Added = Inventory->AddItemByVnumPartial(Entry.ItemVnum, Requested);
	if (Added <= 0)
	{
		return false;
	}
	// Charge only for what actually fit (the inventory can fill up mid-purchase).
	const int64 RequestedCount = FMath::Max(Requested, 1);
	const int64 Paid = (TotalPrice / RequestedCount) * Added +
		((TotalPrice % RequestedCount) * Added) / RequestedCount;
	State->SetYang(State->GetYang() - FMath::Clamp<int64>(Paid, 0, State->GetYang()));
	return true;
}

bool UMT2NpcShopComponent::SellItem(AMT2PlayerCharacter* Player, int32 InventorySlot, int32 Count)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Count <= 0 || !IsPlayerInRange(Player))
	{
		return false;
	}
	AMT2PlayerState* State = Player->GetPlayerState<AMT2PlayerState>();
	UMT2InventoryComponent* Inventory = Player->GetInventoryComponent();
	if (!State || !Inventory)
	{
		return false;
	}

	FMT2ItemSlot Extracted;
	if (!Inventory->ExtractItemForDrop(InventorySlot, Count, Extracted) || Extracted.IsEmpty())
	{
		return false;
	}
	const UMT2ItemTemplate* Template = MT2ItemUtils::ResolveTemplate(this, Extracted.Vnum);
	State->AddYang(CalculateSellPayout(Template, Extracted.Count));
	return true;
}
