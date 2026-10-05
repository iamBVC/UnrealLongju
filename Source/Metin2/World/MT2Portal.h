/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MT2Portal.generated.h"

class UStaticMeshComponent;
class USkeletalMeshComponent;
class USkeletalMesh;
class UAnimInstance;
class USceneComponent;
class UBoxComponent;
class UPrimitiveComponent;
class AMT2PlayerCharacter;

// A teleport portal - a special "warp NPC" whose destination is authored directly on the actor. When a
// player overlaps its mesh, the server asks the coordinator to transfer that player to the destination
// map's server (RequestMapTransferFromServer), and the destination is applied on arrival:
//  - bUseCity: land at the player's empire city (town spawn), ignoring DestinationLocation.
//  - otherwise: land at DestinationLocation on the destination map (ground-snapped).
UCLASS(Blueprintable)
class METIN2_API AMT2Portal : public AActor
{
	GENERATED_BODY()

public:
	AMT2Portal();
	virtual void Tick(float DeltaSeconds) override;

	// Destination map id/name the coordinator routes to (which server hosts it).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal")
	FString MapName;

	// Where on the destination map to place the player (UE world XY). Ignored when bUseCity is true.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal")
	FVector2D DestinationLocation = FVector2D::ZeroVector;

	// When true, teleport to the player's empire city instead of DestinationLocation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal")
	bool bUseCity = false;

	// Label used by the full-map marker. Empty falls back to MapName.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal")
	FString DisplayName;

	UFUNCTION(BlueprintPure, Category = "Portal")
	FString GetPortalDisplayName() const
	{
		return DisplayName.IsEmpty() ? MapName : DisplayName;
	}

	// Used by the map importer to copy the imported warp NPC visual without spawning a full mob.
	void ConfigureSkeletalVisual(
		USkeletalMesh* SkeletalMesh,
		TSubclassOf<UAnimInstance> AnimClass,
		const FTransform& RelativeTransform);

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void HandleEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Portal")
	TObjectPtr<UStaticMeshComponent> PortalMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Portal")
	TObjectPtr<USkeletalMeshComponent> PortalSkeletalMesh;

	// Dedicated trigger independent of the visual mesh's simple-collision asset.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Portal")
	TObjectPtr<UBoxComponent> PortalTrigger;

private:
	void PollPortalVolume();
	void TeleportPlayer(AMT2PlayerCharacter* Player);
	bool TeleportPlayerInPIE(AMT2PlayerCharacter* Player);

	// Players mid-transfer, so a lingering overlap can't fire the transfer repeatedly. Cleared when the
	// player leaves the volume (or when their pawn is gone after a successful transfer).
	TSet<TWeakObjectPtr<AActor>> PlayersInTransit;

	UPROPERTY(VisibleAnywhere, Category = "Portal")
	TObjectPtr<USceneComponent> SceneRoot;
};
