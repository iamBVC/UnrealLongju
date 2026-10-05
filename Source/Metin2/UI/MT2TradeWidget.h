/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "UI/MT2DraggableWindowWidget.h"
#include "UI/MT2InventorySlotWidget.h"
#include "MT2TradeWidget.generated.h"

class UButton; class UEditableTextBox; class UTextBlock; class UMT2TradeComponent;
struct FMT2TradeOfferEntry;

UCLASS()
class METIN2_API UMT2TradeWidget : public UMT2DraggableWindowWidget
{
	GENERATED_BODY()
protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
private:
	UFUNCTION() void RefreshTrade();
	UFUNCTION() void HandleOwnSlotClicked(int32 Index, EMT2InventorySlotKind Kind);
	UFUNCTION() void HandleOwnSlotRightClicked(int32 Index, EMT2InventorySlotKind Kind);
	UFUNCTION() void HandleYangCommitted(const FText& Text, ETextCommit::Type Method);
	UFUNCTION() void HandleAccept();
	UFUNCTION() void HandleCancel();
	UMT2TradeComponent* ResolveTrade() const;
	int32 ResolveItemSize(int32 Vnum) const;
	void RefreshOfferGrid(const TArray<FMT2TradeOfferEntry>& Offer, const TArray<UMT2InventorySlotWidget*>& Slots);
	TArray<UMT2InventorySlotWidget*> OwnSlots() const;
	TArray<UMT2InventorySlotWidget*> PartnerSlots() const;

	UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> OwnNameText;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> PartnerNameText;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UEditableTextBox> OwnYangInput;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> PartnerYangText;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> StatusText;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UButton> AcceptButton;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UButton> CancelButton;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UMT2InventorySlotWidget> OwnSlot00;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UMT2InventorySlotWidget> OwnSlot01;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UMT2InventorySlotWidget> OwnSlot02;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UMT2InventorySlotWidget> OwnSlot03;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UMT2InventorySlotWidget> OwnSlot04;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UMT2InventorySlotWidget> OwnSlot05;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UMT2InventorySlotWidget> OwnSlot06;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UMT2InventorySlotWidget> OwnSlot07;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UMT2InventorySlotWidget> OwnSlot08;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UMT2InventorySlotWidget> OwnSlot09;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UMT2InventorySlotWidget> OwnSlot10;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UMT2InventorySlotWidget> OwnSlot11;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UMT2InventorySlotWidget> PartnerSlot00;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UMT2InventorySlotWidget> PartnerSlot01;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UMT2InventorySlotWidget> PartnerSlot02;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UMT2InventorySlotWidget> PartnerSlot03;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UMT2InventorySlotWidget> PartnerSlot04;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UMT2InventorySlotWidget> PartnerSlot05;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UMT2InventorySlotWidget> PartnerSlot06;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UMT2InventorySlotWidget> PartnerSlot07;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UMT2InventorySlotWidget> PartnerSlot08;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UMT2InventorySlotWidget> PartnerSlot09;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UMT2InventorySlotWidget> PartnerSlot10;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UMT2InventorySlotWidget> PartnerSlot11;
	UPROPERTY(Transient) TWeakObjectPtr<UMT2TradeComponent> BoundTrade;
};
