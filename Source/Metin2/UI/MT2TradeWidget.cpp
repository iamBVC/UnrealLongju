/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2TradeWidget.h"

#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Engine/LocalPlayer.h"
#include "Items/MT2ItemUtils.h"
#include "Player/MT2PlayerState.h"
#include "Trade/MT2TradeComponent.h"
#include "UI/MT2CursorCarrySubsystem.h"

TArray<UMT2InventorySlotWidget*> UMT2TradeWidget::OwnSlots() const
{
	return {OwnSlot00,OwnSlot01,OwnSlot02,OwnSlot03,OwnSlot04,OwnSlot05,OwnSlot06,OwnSlot07,OwnSlot08,OwnSlot09,OwnSlot10,OwnSlot11};
}
TArray<UMT2InventorySlotWidget*> UMT2TradeWidget::PartnerSlots() const
{
	return {PartnerSlot00,PartnerSlot01,PartnerSlot02,PartnerSlot03,PartnerSlot04,PartnerSlot05,PartnerSlot06,PartnerSlot07,PartnerSlot08,PartnerSlot09,PartnerSlot10,PartnerSlot11};
}

UMT2TradeComponent* UMT2TradeWidget::ResolveTrade() const
{
	AMT2PlayerState* State = GetOwningPlayer() ? GetOwningPlayer()->GetPlayerState<AMT2PlayerState>() : nullptr;
	return State ? State->GetTradeComponent() : nullptr;
}

int32 UMT2TradeWidget::ResolveItemSize(int32 Vnum) const
{
	return MT2ItemUtils::GetInventorySize(this, Vnum);
}

void UMT2TradeWidget::RefreshOfferGrid(
	const TArray<FMT2TradeOfferEntry>& Offer, const TArray<UMT2InventorySlotWidget*>& Slots)
{
	constexpr int32 Columns = 4;
	TArray<int32> CoveringItem;
	CoveringItem.Init(INDEX_NONE, Slots.Num());

	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slots[Index]->Slot))
		{
			CanvasSlot->SetZOrder(0);
		}
		if (!Offer.IsValidIndex(Index) || Offer[Index].Item.IsEmpty()) continue;
		const int32 Size = ResolveItemSize(Offer[Index].Item.Vnum);
		if (Size > 1)
		{
			if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slots[Index]->Slot))
			{
				CanvasSlot->SetZOrder(1);
			}
		}
		for (int32 CellOffset = 1; CellOffset < Size; ++CellOffset)
		{
			const int32 CoveredIndex = Index + CellOffset * Columns;
			if (CoveringItem.IsValidIndex(CoveredIndex)) CoveringItem[CoveredIndex] = Index;
		}
	}

	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		if (CoveringItem[Index] != INDEX_NONE)
		{
			const int32 OwnerIndex = CoveringItem[Index];
			const FMT2ItemSlot& Item = Offer[OwnerIndex].Item;
			Slots[Index]->SetCoveredByItem(OwnerIndex, Item.Vnum, Item.Count, Item.Bonuses, Item.MetinSockets, Item.SkillVnum);
		}
		else if (Offer.IsValidIndex(Index) && !Offer[Index].Item.IsEmpty())
		{
			const FMT2ItemSlot& Item = Offer[Index].Item;
			Slots[Index]->SetItemByVnum(Item.Vnum, Item.Count, Item.Bonuses,
				Item.MetinSockets, Item.SkillVnum, Item.AutoRecoveryRemainingAmount);
		}
		else
		{
			Slots[Index]->SetItemByVnum(0, 0);
		}
	}
}

void UMT2TradeWidget::NativeConstruct()
{
	Super::NativeConstruct();
	AcceptButton->OnClicked.AddUniqueDynamic(this, &UMT2TradeWidget::HandleAccept);
	CancelButton->OnClicked.AddUniqueDynamic(this, &UMT2TradeWidget::HandleCancel);
	OwnYangInput->OnTextCommitted.AddUniqueDynamic(this, &UMT2TradeWidget::HandleYangCommitted);
	for (int32 Index=0; Index<OwnSlots().Num(); ++Index)
	{
		OwnSlots()[Index]->InitializeSlot(Index, EMT2InventorySlotKind::Item, FText::GetEmpty());
		OwnSlots()[Index]->OnSlotClicked.AddUniqueDynamic(this, &UMT2TradeWidget::HandleOwnSlotClicked);
		OwnSlots()[Index]->OnSlotRightClicked.AddUniqueDynamic(this, &UMT2TradeWidget::HandleOwnSlotRightClicked);
		PartnerSlots()[Index]->InitializeSlot(Index, EMT2InventorySlotKind::Item, FText::GetEmpty());
	}
	BoundTrade = ResolveTrade();
	if (BoundTrade.IsValid()) BoundTrade->OnTradeChanged.AddUniqueDynamic(this, &UMT2TradeWidget::RefreshTrade);
	RefreshTrade();
}

void UMT2TradeWidget::NativeDestruct()
{
	if (BoundTrade.IsValid()) BoundTrade->OnTradeChanged.RemoveDynamic(this, &UMT2TradeWidget::RefreshTrade);
	Super::NativeDestruct();
}

void UMT2TradeWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!BoundTrade.IsValid())
	{
		BoundTrade = ResolveTrade();
		if (BoundTrade.IsValid())
		{
			BoundTrade->OnTradeChanged.AddUniqueDynamic(this, &UMT2TradeWidget::RefreshTrade);
			RefreshTrade();
		}
	}
}

void UMT2TradeWidget::RefreshTrade()
{
	UMT2TradeComponent* Trade = BoundTrade.Get();
	if (!Trade) { BoundTrade = ResolveTrade(); Trade = BoundTrade.Get(); }
	SetVisibility(Trade && Trade->IsTrading() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (!Trade || !Trade->IsTrading()) return;
	AMT2PlayerState* State = GetOwningPlayer()->GetPlayerState<AMT2PlayerState>();
	OwnNameText->SetText(FText::FromString(State ? State->GetCharacterName() : TEXT("You")));
	PartnerNameText->SetText(FText::FromString(Trade->GetPartnerName()));
	OwnYangInput->SetText(FText::AsNumber(Trade->GetOwnYang()));
	OwnYangInput->SetIsReadOnly(Trade->IsLocked());
	PartnerYangText->SetText(FText::Format(NSLOCTEXT("MT2Trade","PartnerYang","{0} Yang"), FText::AsNumber(Trade->GetPartnerYang())));
	StatusText->SetText(Trade->HasAccepted() ? NSLOCTEXT("MT2Trade","Accepted","Accepted - waiting for partner") : FText::GetEmpty());
	AcceptButton->SetIsEnabled(!Trade->HasAccepted());
	const TArray<FMT2TradeOfferEntry>& Own = Trade->GetOwnOffer(); const TArray<FMT2TradeOfferEntry>& Other = Trade->GetPartnerOffer();
	RefreshOfferGrid(Own, OwnSlots());
	RefreshOfferGrid(Other, PartnerSlots());
}

void UMT2TradeWidget::HandleOwnSlotClicked(int32 Index, EMT2InventorySlotKind)
{
	UMT2TradeComponent* Trade = BoundTrade.Get(); ULocalPlayer* LP = GetOwningLocalPlayer();
	UMT2CursorCarrySubsystem* Carry = LP ? LP->GetSubsystem<UMT2CursorCarrySubsystem>() : nullptr;
	if (Trade && Carry && Carry->GetCarryKind() == EMT2CarryKind::InventoryItem)
	{
		Trade->ServerSetOfferSlot(Index, Carry->GetCarryPayload()); Carry->EndCarry();
	}
}
void UMT2TradeWidget::HandleOwnSlotRightClicked(int32 Index, EMT2InventorySlotKind) { if (BoundTrade.IsValid()) BoundTrade->ServerRemoveOfferSlot(Index); }
void UMT2TradeWidget::HandleYangCommitted(const FText& Text, ETextCommit::Type) { if (BoundTrade.IsValid()) BoundTrade->ServerSetYang(FCString::Atoi64(*Text.ToString())); }
void UMT2TradeWidget::HandleAccept() { if (BoundTrade.IsValid()) BoundTrade->ServerAccept(); }
void UMT2TradeWidget::HandleCancel() { if (BoundTrade.IsValid()) BoundTrade->ServerCancel(); }
