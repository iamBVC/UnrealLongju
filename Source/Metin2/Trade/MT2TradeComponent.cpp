/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Trade/MT2TradeComponent.h"

#include "Characters/MT2PlayerCharacter.h"
#include "Items/MT2InventoryComponent.h"
#include "Net/UnrealNetwork.h"
#include "Player/MT2PlayerState.h"

UMT2TradeComponent::UMT2TradeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	OwnOffer.SetNum(OfferSlotCount);
	PartnerOffer.SetNum(OfferSlotCount);
}

void UMT2TradeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetOwner() && GetOwner()->HasAuthority() && bTrading) EndTrade(false);
	Super::EndPlay(EndPlayReason);
}

void UMT2TradeComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UMT2TradeComponent, bTrading, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UMT2TradeComponent, bLocked, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UMT2TradeComponent, bAccepted, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UMT2TradeComponent, bPartnerAccepted, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UMT2TradeComponent, PartnerName, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UMT2TradeComponent, OwnOffer, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UMT2TradeComponent, PartnerOffer, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UMT2TradeComponent, OwnYang, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UMT2TradeComponent, PartnerYang, COND_OwnerOnly);
}

AMT2PlayerState* UMT2TradeComponent::GetPlayerState() const { return Cast<AMT2PlayerState>(GetOwner()); }
AMT2PlayerCharacter* UMT2TradeComponent::GetCharacter() const
{
	return GetPlayerState() ? Cast<AMT2PlayerCharacter>(GetPlayerState()->GetPawn()) : nullptr;
}

void UMT2TradeComponent::ServerRequestTrade_Implementation(AActor* TargetPlayer)
{
	AMT2PlayerCharacter* Target = Cast<AMT2PlayerCharacter>(TargetPlayer);
	AMT2PlayerCharacter* OwnerCharacter = GetCharacter();
	AMT2PlayerState* TargetState = Target ? Target->GetPlayerState<AMT2PlayerState>() : nullptr;
	UMT2TradeComponent* Other = TargetState ? TargetState->GetTradeComponent() : nullptr;
	if (!OwnerCharacter || !Target || !Other || Other == this || bTrading || Other->bTrading ||
		FVector::DistSquared(OwnerCharacter->GetActorLocation(), Target->GetActorLocation()) > FMath::Square(1000.0f))
	{
		return;
	}
	BeginTrade(*Other);
}

void UMT2TradeComponent::ServerRequestTradeWithItem_Implementation(
	AActor* TargetPlayer, int32 InventorySlot)
{
	AMT2PlayerCharacter* Target = Cast<AMT2PlayerCharacter>(TargetPlayer);
	AMT2PlayerCharacter* OwnerCharacter = GetCharacter();
	AMT2PlayerState* TargetState = Target ? Target->GetPlayerState<AMT2PlayerState>() : nullptr;
	UMT2TradeComponent* Other = TargetState ? TargetState->GetTradeComponent() : nullptr;
	UMT2InventoryComponent* Inventory = OwnerCharacter ? OwnerCharacter->GetInventoryComponent() : nullptr;
	if (!OwnerCharacter || !Target || !Other || Other == this || bTrading || Other->bTrading ||
		!Inventory || !Inventory->GetSlots().IsValidIndex(InventorySlot) ||
		Inventory->GetSlots()[InventorySlot].IsEmpty() ||
		FVector::DistSquared(OwnerCharacter->GetActorLocation(), Target->GetActorLocation()) > FMath::Square(1000.0f))
	{
		return;
	}

	BeginTrade(*Other);
	if (!SetOfferSlot(0, InventorySlot))
	{
		EndTrade(false);
	}
}

void UMT2TradeComponent::BeginTrade(UMT2TradeComponent& Other)
{
	Partner = &Other; Other.Partner = this;
	bTrading = Other.bTrading = true;
	PartnerName = Other.GetPlayerState()->GetCharacterName();
	Other.PartnerName = GetPlayerState()->GetCharacterName();
	OwnOffer.SetNum(OfferSlotCount); PartnerOffer.SetNum(OfferSlotCount);
	Other.OwnOffer.SetNum(OfferSlotCount); Other.PartnerOffer.SetNum(OfferSlotCount);
	SyncPartnerView(); Other.SyncPartnerView();
}

void UMT2TradeComponent::ResetAcceptances()
{
	bAccepted = bPartnerAccepted = bLocked = false;
	if (Partner) Partner->bAccepted = Partner->bPartnerAccepted = Partner->bLocked = false;
}

void UMT2TradeComponent::SyncPartnerView()
{
	if (!Partner) return;
	Partner->PartnerOffer = OwnOffer;
	Partner->PartnerYang = OwnYang;
	Partner->bPartnerAccepted = bAccepted;
	Partner->OnRep_TradeState();
	OnRep_TradeState();
	if (AActor* OwnerActor = GetOwner()) OwnerActor->ForceNetUpdate();
	if (AActor* PartnerOwner = Partner->GetOwner()) PartnerOwner->ForceNetUpdate();
}

void UMT2TradeComponent::ServerSetOfferSlot_Implementation(int32 OfferIndex, int32 InventorySlot)
{
	SetOfferSlot(OfferIndex, InventorySlot);
}

bool UMT2TradeComponent::SetOfferSlot(int32 OfferIndex, int32 InventorySlot)
{
	constexpr int32 OfferColumns = 4;
	constexpr int32 OfferRows = 3;
	AMT2PlayerCharacter* Character = GetCharacter();
	UMT2InventoryComponent* Inventory = Character ? Character->GetInventoryComponent() : nullptr;
	if (!bTrading || bLocked || !Partner || !OwnOffer.IsValidIndex(OfferIndex) || !Inventory) return false;
	const TArray<FMT2ItemSlot>& Slots = Inventory->GetSlots();
	if (!Slots.IsValidIndex(InventorySlot) || Slots[InventorySlot].IsEmpty()) return false;
	if (!Inventory->CanTradeItemAtSlot(InventorySlot)) return false;
	for (const FMT2TradeOfferEntry& Entry : OwnOffer)
		if (Entry.InventorySlot == InventorySlot) return false;

	const int32 ItemSize = Inventory->GetItemSize(Slots[InventorySlot].Vnum);
	const int32 OfferRow = OfferIndex / OfferColumns;
	if (!OwnOffer[OfferIndex].Item.IsEmpty() || OfferRow + ItemSize > OfferRows) return false;

	TArray<bool> Occupied;
	Occupied.Init(false, OfferSlotCount);
	for (int32 ExistingIndex = 0; ExistingIndex < OwnOffer.Num(); ++ExistingIndex)
	{
		const FMT2TradeOfferEntry& Existing = OwnOffer[ExistingIndex];
		if (Existing.Item.IsEmpty()) continue;
		const int32 ExistingSize = Inventory->GetItemSize(Existing.Item.Vnum);
		for (int32 CellOffset = 0; CellOffset < ExistingSize; ++CellOffset)
		{
			const int32 Cell = ExistingIndex + CellOffset * OfferColumns;
			if (Occupied.IsValidIndex(Cell)) Occupied[Cell] = true;
		}
	}
	for (int32 CellOffset = 0; CellOffset < ItemSize; ++CellOffset)
	{
		const int32 Cell = OfferIndex + CellOffset * OfferColumns;
		if (!Occupied.IsValidIndex(Cell) || Occupied[Cell]) return false;
	}
	OwnOffer[OfferIndex].InventorySlot = InventorySlot;
	OwnOffer[OfferIndex].Item = Slots[InventorySlot];
	ResetAcceptances(); SyncPartnerView();
	return true;
}

void UMT2TradeComponent::ServerRemoveOfferSlot_Implementation(int32 OfferIndex)
{
	if (!bTrading || bLocked || !OwnOffer.IsValidIndex(OfferIndex)) return;
	OwnOffer[OfferIndex] = FMT2TradeOfferEntry();
	ResetAcceptances(); SyncPartnerView();
}

void UMT2TradeComponent::ServerSetYang_Implementation(int64 Yang)
{
	AMT2PlayerState* State = GetPlayerState();
	if (!bTrading || bLocked || !State) return;
	OwnYang = FMath::Clamp<int64>(Yang, 0, State->GetYang());
	ResetAcceptances(); SyncPartnerView();
}

void UMT2TradeComponent::ServerAccept_Implementation()
{
	if (!bTrading || !Partner || bAccepted) return;
	bAccepted = true;
	bLocked = Partner->bLocked = true;
	SyncPartnerView();
	if (Partner->bAccepted) TryCommit();
}

bool UMT2TradeComponent::TryCommit()
{
	if (!Partner || !bAccepted || !Partner->bAccepted) return false;
	AMT2PlayerState* AState = GetPlayerState(); AMT2PlayerState* BState = Partner->GetPlayerState();
	UMT2InventoryComponent* AInv = GetCharacter() ? GetCharacter()->GetInventoryComponent() : nullptr;
	UMT2InventoryComponent* BInv = Partner->GetCharacter() ? Partner->GetCharacter()->GetInventoryComponent() : nullptr;
	AMT2PlayerCharacter* ACharacter = GetCharacter(); AMT2PlayerCharacter* BCharacter = Partner->GetCharacter();
	if (!AState || !BState || !AInv || !BInv || !ACharacter || !BCharacter ||
		FVector::DistSquared(ACharacter->GetActorLocation(), BCharacter->GetActorLocation()) > FMath::Square(1000.0f) ||
		AState->GetYang() < OwnYang || BState->GetYang() < Partner->OwnYang ||
		(Partner->OwnYang > 0 && AState->GetYang() - OwnYang > MAX_int64 - Partner->OwnYang) ||
		(OwnYang > 0 && BState->GetYang() - Partner->OwnYang > MAX_int64 - OwnYang))
	{
		EndTrade(false); return false;
	}
	TArray<int32> ASlots, BSlots; TArray<FMT2ItemSlot> AItems, BItems;
	for (const FMT2TradeOfferEntry& Entry : OwnOffer) if (!Entry.Item.IsEmpty()) { ASlots.Add(Entry.InventorySlot); AItems.Add(Entry.Item); }
	for (const FMT2TradeOfferEntry& Entry : Partner->OwnOffer) if (!Entry.Item.IsEmpty()) { BSlots.Add(Entry.InventorySlot); BItems.Add(Entry.Item); }
	if (!AInv->ExecuteAtomicTrade(*BInv, ASlots, AItems, BSlots, BItems)) { EndTrade(false); return false; }
	AState->SetYang(AState->GetYang() - OwnYang + Partner->OwnYang);
	BState->SetYang(BState->GetYang() - Partner->OwnYang + OwnYang);
	AState->NotifyYangReceived(Partner->OwnYang);
	BState->NotifyYangReceived(OwnYang);
	EndTrade(true); return true;
}

void UMT2TradeComponent::ServerCancel_Implementation() { EndTrade(false); }

void UMT2TradeComponent::EndTrade(bool)
{
	UMT2TradeComponent* Other = Partner;
	auto Clear = [](UMT2TradeComponent* C)
	{
		if (!C) return;
		C->bTrading = C->bLocked = C->bAccepted = C->bPartnerAccepted = false;
		C->OwnYang = C->PartnerYang = 0; C->PartnerName.Reset(); C->Partner = nullptr;
		C->OwnOffer.Empty(OfferSlotCount); C->OwnOffer.SetNum(OfferSlotCount);
		C->PartnerOffer.Empty(OfferSlotCount); C->PartnerOffer.SetNum(OfferSlotCount);
		C->OnRep_TradeState();
		if (AActor* OwnerActor = C->GetOwner()) OwnerActor->ForceNetUpdate();
	};
	Clear(this); Clear(Other);
}

void UMT2TradeComponent::OnRep_TradeState() { OnTradeChanged.Broadcast(); }
