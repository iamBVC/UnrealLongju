/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Items/MT2ItemTypes.h"
#include "MT2WorldItem.generated.h"

class AMT2PlayerCharacter;
class AMT2PlayerState;
class UMT2NameplateComponent;
class USceneComponent;
class USphereComponent;
class UStaticMeshComponent;
class UWorld;

UCLASS(Blueprintable)
class METIN2_API AMT2WorldItem : public AActor
{
	GENERATED_BODY()

public:
	AMT2WorldItem();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	static AMT2WorldItem* SpawnWorldItem(
		UWorld* World, const FVector& GroundLocation, int32 InVnum, int32 InCount,
		AActor* SourceActor = nullptr, const FString& InOwnerCharacterId = FString(),
		const FString& InOwnerDisplayName = FString(), float OwnershipDuration = 0.0f);
	static AMT2WorldItem* SpawnWorldYang(
		UWorld* World, const FVector& GroundLocation, int64 Amount,
		AActor* SourceActor = nullptr, const FString& InOwnerCharacterId = FString(),
		const FString& InOwnerDisplayName = FString(), float OwnershipDuration = 0.0f);
	static AMT2WorldItem* SpawnWorldItemInstance(
		UWorld* World, const FVector& GroundLocation, const FMT2ItemSlot& Item,
		AActor* SourceActor = nullptr, const FString& InOwnerCharacterId = FString(),
		const FString& InOwnerDisplayName = FString(), float OwnershipDuration = 0.0f);

	void InitializeItem(
		int32 InVnum, int32 InCount, const FString& InOwnerCharacterId = FString(),
		const FString& InOwnerDisplayName = FString(), float OwnershipDuration = 0.0f);
	void InitializeItemInstance(
		const FMT2ItemSlot& Item, const FString& InOwnerCharacterId = FString(),
		const FString& InOwnerDisplayName = FString(), float OwnershipDuration = 0.0f);
	int64 TryPickup(
		AMT2PlayerCharacter* RequestingCharacter,
		AMT2PlayerCharacter** OutReceivingCharacter = nullptr);

	UFUNCTION(BlueprintPure, Category = "Item") int32 GetItemVnum() const { return ItemInstance.Vnum; }
	UFUNCTION(BlueprintPure, Category = "Item") int32 GetItemCount() const { return ItemInstance.Count; }
	UFUNCTION(BlueprintPure, Category = "Item") int64 GetYangAmount() const { return YangAmount; }
	UFUNCTION(BlueprintPure, Category = "Item") FString GetItemDisplayName() const;
	UFUNCTION(BlueprintPure, Category = "Item") const FString& GetOwnershipDisplayName() const { return OwnerDisplayName; }
	const FString& GetOwnershipCharacterId() const { return OwnerCharacterId; }

	static FString ResolvePlayerIdentity(const AMT2PlayerState* PlayerState);

private:
	UFUNCTION() void OnRep_ItemData();
	UFUNCTION(NetMulticast, Reliable) void MulticastPlayDropSound(int32 DroppedVnum);
	UFUNCTION() void ClearOwnership();
	void BroadcastDropSound();
	void WakeForReplication();
	void RefreshVisuals();
	bool CanBePickedUpBy(const AMT2PlayerCharacter* Character) const;
	AMT2PlayerCharacter* ResolvePickupRecipient(AMT2PlayerCharacter* RequestingCharacter) const;

	UPROPERTY(VisibleAnywhere, Category = "Item") TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY(VisibleAnywhere, Category = "Item") TObjectPtr<USphereComponent> InteractionSphere;
	UPROPERTY(VisibleAnywhere, Category = "Item") TObjectPtr<UStaticMeshComponent> MeshComponent;
	UPROPERTY(VisibleAnywhere, Category = "Item") TObjectPtr<UMT2NameplateComponent> NameplateComponent;

	UPROPERTY(ReplicatedUsing = OnRep_ItemData)
	FMT2ItemSlot ItemInstance;
	UPROPERTY(ReplicatedUsing = OnRep_ItemData)
	int64 YangAmount = 0;
	UPROPERTY(ReplicatedUsing = OnRep_ItemData) FString OwnerCharacterId;
	UPROPERTY(ReplicatedUsing = OnRep_ItemData) FString OwnerDisplayName;

	FTimerHandle OwnershipTimer;

	// Cosmetic drop animation: the mesh starts above (at the dropping pawn's head) with a random
	// 3D rotation and falls/spins into its final resting transform. Purely local - the actor itself
	// already sits at the final ground location.
	void StartFallAnimation();
	FVector FallStartRelativeLocation = FVector::ZeroVector;
	FQuat FallStartRelativeQuat = FQuat::Identity;
	FQuat FallEndRelativeQuat = FQuat::Identity;
	float FallElapsed = 0.0f;
	bool bFalling = false;

	static constexpr float FallDuration = 0.45f;
};
