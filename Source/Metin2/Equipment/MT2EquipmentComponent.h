/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Items/MT2ItemTemplate.h"
#include "Player/MT2PlayerTypes.h"
#include "MT2EquipmentComponent.generated.h"

class USkeletalMesh;
class USkeletalMeshComponent;
class UParticleSystem;
class UParticleSystemComponent;
class UStaticMesh;
class UStaticMeshComponent;
struct FStreamableHandle;

UCLASS(BlueprintType, ClassGroup = "MT2", meta = (BlueprintSpawnableComponent))
class METIN2_API UMT2EquipmentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMT2EquipmentComponent();
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void SetVisualComponents(
		USkeletalMeshComponent* InBodyMesh,
		USkeletalMeshComponent* InHairMesh,
		UStaticMeshComponent* InWeaponMesh,
		UStaticMeshComponent* InLeftWeaponMesh);

	UFUNCTION(BlueprintCallable, Category = "Equipment")
	void ApplyDefaultHair(const FMT2CharacterAppearance& Appearance);

	UFUNCTION(BlueprintCallable, Category = "Equipment")
	void EquipHair(
		TSoftObjectPtr<USkeletalMesh> Hair,
		TSoftObjectPtr<UTexture2D> DiffuseTexture);

	UFUNCTION(BlueprintCallable, Category = "Equipment")
	void EquipWeapon(
		TSoftObjectPtr<UStaticMesh> Weapon, FName AttachBone = NAME_None,
		int32 WeaponSubType = 0, int32 RefinementLevel = 0);

	UFUNCTION(BlueprintCallable, Category = "Equipment")
	void UnequipWeapon();

	UFUNCTION(BlueprintPure, Category = "Equipment")
	bool HasWeaponEquipped() const;

	UFUNCTION(BlueprintPure, Category = "Equipment")
	UStaticMeshComponent* GetWeaponMeshComponent() const { return WeaponMeshComponent; }

	UFUNCTION(BlueprintPure, Category = "Equipment")
	int32 GetEquippedWeaponSubType() const { return EquippedWeaponSubType; }

	// Body armor replaces the character's whole skeletal mesh rather than attaching a separate mesh.
	// UnequipArmor needs the base (unequipped) body mesh to revert to, since there's nothing else on
	// this component that knows what that is - the caller (which does, via character appearance) passes it.
	UFUNCTION(BlueprintCallable, Category = "Equipment")
	void EquipArmor(
		TSoftObjectPtr<USkeletalMesh> Armor,
		const TArray<FMT2ArmorMaterialOverride>& MaterialOverrides,
		int32 RefinementLevel = 0);

	UFUNCTION(BlueprintCallable, Category = "Equipment")
	void UnequipArmor(TSoftObjectPtr<USkeletalMesh> DefaultBodyMesh);

	UFUNCTION(BlueprintPure, Category = "Equipment")
	bool HasArmorEquipped() const { return bArmorEquipped; }

private:
	friend class FMT2FishingPresentationTest;
	bool AttachWeaponMeshToBody();
	bool AttachLeftWeaponMeshToBody();
	bool UsesPairedWeaponVisual() const;
	void OnHairLoaded(int32 RequestId, FSoftObjectPath MeshPath, FSoftObjectPath TexturePath);
	void OnWeaponLoaded(int32 RequestId, FSoftObjectPath MeshPath);
	void OnArmorLoaded(int32 RequestId, FSoftObjectPath MeshPath);
	void RefreshWeaponGlitter();
	void RefreshArmorGlitter();
	void OnWeaponGlittersLoaded(int32 RequestId, TArray<FSoftObjectPath> EffectPaths);
	void OnArmorGlittersLoaded(int32 RequestId, TArray<FSoftObjectPath> EffectPaths);
	void ClearWeaponGlitter();
	void ClearArmorGlitter();
	TArray<TSoftObjectPtr<UParticleSystem>> ResolveWeaponGlitters() const;
	TArray<TSoftObjectPtr<UParticleSystem>> ResolveArmorGlitters() const;
	void GetWeaponGlitterRelativeTransform(FVector& OutLocation, FVector& OutScale) const;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> BodyMeshComponent;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> HairMeshComponent;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> WeaponMeshComponent;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> LeftWeaponMeshComponent;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UParticleSystemComponent>> WeaponGlitterComponents;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UParticleSystemComponent>> ArmorGlitterComponents;

	TSharedPtr<FStreamableHandle> HairLoadHandle;
	TSharedPtr<FStreamableHandle> WeaponLoadHandle;
	TSharedPtr<FStreamableHandle> ArmorLoadHandle;
	TSharedPtr<FStreamableHandle> WeaponGlitterLoadHandle;
	TSharedPtr<FStreamableHandle> ArmorGlitterLoadHandle;
	TArray<FMT2ArmorMaterialOverride> PendingArmorMaterialOverrides;
	int32 HairRequestId = 0;
	int32 WeaponRequestId = 0;
	int32 ArmorRequestId = 0;
	int32 WeaponGlitterRequestId = 0;
	int32 ArmorGlitterRequestId = 0;
	int32 EquippedWeaponSubType = INDEX_NONE;
	int32 EquippedWeaponRefinementLevel = 0;
	int32 EquippedArmorRefinementLevel = 0;
	bool bArmorEquipped = false;
	FName WeaponAttachBone = TEXT("equip_right");
};
