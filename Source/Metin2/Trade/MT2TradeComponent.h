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
#include "MT2TradeComponent.generated.h"

USTRUCT(BlueprintType)
struct METIN2_API FMT2TradeOfferEntry
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) int32 InventorySlot = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly) FMT2ItemSlot Item;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMT2TradeChangedSignature);

UCLASS(ClassGroup="MT2", meta=(BlueprintSpawnableComponent))
class METIN2_API UMT2TradeComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	static constexpr int32 OfferSlotCount = 12;
	UMT2TradeComponent();
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category="Trade") bool IsTrading() const { return bTrading; }
	UFUNCTION(BlueprintPure, Category="Trade") bool IsLocked() const { return bLocked; }
	UFUNCTION(BlueprintPure, Category="Trade") bool HasAccepted() const { return bAccepted; }
	UFUNCTION(BlueprintPure, Category="Trade") bool HasPartnerAccepted() const { return bPartnerAccepted; }
	UFUNCTION(BlueprintPure, Category="Trade") const FString& GetPartnerName() const { return PartnerName; }
	UFUNCTION(BlueprintPure, Category="Trade") const TArray<FMT2TradeOfferEntry>& GetOwnOffer() const { return OwnOffer; }
	UFUNCTION(BlueprintPure, Category="Trade") const TArray<FMT2TradeOfferEntry>& GetPartnerOffer() const { return PartnerOffer; }
	UFUNCTION(BlueprintPure, Category="Trade") int64 GetOwnYang() const { return OwnYang; }
	UFUNCTION(BlueprintPure, Category="Trade") int64 GetPartnerYang() const { return PartnerYang; }

	UFUNCTION(Server, Reliable) void ServerRequestTrade(AActor* TargetPlayer);
	UFUNCTION(Server, Reliable) void ServerRequestTradeWithItem(AActor* TargetPlayer, int32 InventorySlot);
	UFUNCTION(Server, Reliable) void ServerSetOfferSlot(int32 OfferIndex, int32 InventorySlot);
	UFUNCTION(Server, Reliable) void ServerRemoveOfferSlot(int32 OfferIndex);
	UFUNCTION(Server, Reliable) void ServerSetYang(int64 Yang);
	UFUNCTION(Server, Reliable) void ServerAccept();
	UFUNCTION(Server, Reliable) void ServerCancel();

	UPROPERTY(BlueprintAssignable) FMT2TradeChangedSignature OnTradeChanged;

private:
	UFUNCTION() void OnRep_TradeState();
	void BeginTrade(UMT2TradeComponent& Other);
	bool SetOfferSlot(int32 OfferIndex, int32 InventorySlot);
	void SyncPartnerView();
	void ResetAcceptances();
	void EndTrade(bool bSuccess);
	bool TryCommit();
	class AMT2PlayerState* GetPlayerState() const;
	class AMT2PlayerCharacter* GetCharacter() const;

	UPROPERTY(ReplicatedUsing=OnRep_TradeState) bool bTrading = false;
	UPROPERTY(ReplicatedUsing=OnRep_TradeState) bool bLocked = false;
	UPROPERTY(ReplicatedUsing=OnRep_TradeState) bool bAccepted = false;
	UPROPERTY(ReplicatedUsing=OnRep_TradeState) bool bPartnerAccepted = false;
	UPROPERTY(ReplicatedUsing=OnRep_TradeState) FString PartnerName;
	UPROPERTY(ReplicatedUsing=OnRep_TradeState) TArray<FMT2TradeOfferEntry> OwnOffer;
	UPROPERTY(ReplicatedUsing=OnRep_TradeState) TArray<FMT2TradeOfferEntry> PartnerOffer;
	UPROPERTY(ReplicatedUsing=OnRep_TradeState) int64 OwnYang = 0;
	UPROPERTY(ReplicatedUsing=OnRep_TradeState) int64 PartnerYang = 0;
	UPROPERTY() TObjectPtr<UMT2TradeComponent> Partner;
};
