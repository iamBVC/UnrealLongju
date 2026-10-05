/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Mounts/MT2MountDefinition.h"
#include "MT2MountComponent.generated.h"

class AMT2PlayerCharacter;
class USkeletalMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMT2MountedStateChangedSignature, bool, bMounted);

// Server-authoritative mounted state. The player remains the only possessed movement actor; the
// mount is a visual child, avoiding a second movement simulation and correction stream.
UCLASS(BlueprintType, ClassGroup = "MT2", meta = (BlueprintSpawnableComponent))
class METIN2_API UMT2MountComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMT2MountComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Mount")
	bool IsMounted() const { return bMounted; }

	UFUNCTION(BlueprintPure, Category = "Mount")
	UMT2MountDefinition* GetMountDefinition() const { return MountDefinition.Get(); }

	UFUNCTION(BlueprintPure, Category = "Mount")
	USkeletalMeshComponent* GetMountMeshComponent() const { return MountMeshComponent; }

	UFUNCTION(BlueprintPure, Category = "Mount")
	EMT2MountKind GetMountedKind() const { return MountedKind; }

	// Compatibility reads used by imported legacy quests. A called special mount remains selected
	// while dismounted, matching the equipped special-ride item queried by the old server.
	UFUNCTION(BlueprintPure, Category = "Mount")
	int32 GetCalledSpecialMountVnum() const;

	UFUNCTION(BlueprintPure, Category = "Mount")
	int32 GetCalledSpecialMountRemainingSeconds() const;

	UFUNCTION(BlueprintPure, Category = "Mount|Combat")
	bool CanAttackWhileMounted() const;

	UFUNCTION(BlueprintPure, Category = "Mount|Combat")
	bool CanUseHorseSkills() const;

	UFUNCTION(Server, Reliable, BlueprintCallable, BlueprintAuthorityOnly, Category = "Mount")
	void ServerDismount();

	// Old-client Ctrl+G/Ctrl+H actions carry no asset reference. The server toggles only a mount
	// previously registered by validated gameplay (item use or horse progression).
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Mount")
	void ServerToggleSpecialMount();

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Mount")
	void ServerToggleHorse();

	// Server-only transition used by death, travel and future restricted-map policies.
	void ForceDismount();

	// Server-only validated transition used by inventory items and future horse progression.
	bool Mount(UMT2MountDefinition* Definition, int32 DurationSeconds = 0);
	bool CallSpecialMount(UMT2MountDefinition* Definition, int32 DurationSeconds = 0);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Mount")
	void SetCalledHorse(UMT2MountDefinition* Definition);

	UPROPERTY(BlueprintAssignable, Category = "Mount")
	FMT2MountedStateChangedSignature OnMountedStateChanged;

private:
	UFUNCTION()
	void OnRep_MountedState();

	void ApplyMountedState();
	void EnsureVisualComponent();
	AMT2PlayerCharacter* GetPlayerCharacter() const;

	UPROPERTY(ReplicatedUsing = OnRep_MountedState)
	bool bMounted = false;

	UPROPERTY(ReplicatedUsing = OnRep_MountedState)
	TSoftObjectPtr<UMT2MountDefinition> MountDefinition;

	UPROPERTY(ReplicatedUsing = OnRep_MountedState)
	EMT2MountKind MountedKind = EMT2MountKind::SpecialMount;

	UPROPERTY(EditDefaultsOnly, Category = "Mount|Horse")
	TSoftObjectPtr<UMT2MountDefinition> CalledHorseDefinition;

	UPROPERTY(Transient)
	TSoftObjectPtr<UMT2MountDefinition> CalledSpecialMountDefinition;

	int32 CalledSpecialMountDurationSeconds = 0;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> MountMeshComponent;

	FTransform StandingMeshTransform = FTransform::Identity;
	uint8 StandingNetworkSmoothingMode = 0;
	FTimerHandle MountExpiryTimer;
};
