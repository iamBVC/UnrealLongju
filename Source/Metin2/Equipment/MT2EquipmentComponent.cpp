/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Equipment/MT2EquipmentComponent.h"

#include "Characters/MT2CharacterAppearanceSettings.h"
#include "Config/MT2GameplaySettings.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"

UMT2EquipmentComponent::UMT2EquipmentComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UMT2EquipmentComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	++HairRequestId;
	++WeaponRequestId;
	++ArmorRequestId;
	ClearWeaponGlitter();
	ClearArmorGlitter();
	if (HairLoadHandle.IsValid())
	{
		HairLoadHandle->CancelHandle();
	}
	if (WeaponLoadHandle.IsValid())
	{
		WeaponLoadHandle->CancelHandle();
	}
	if (ArmorLoadHandle.IsValid())
	{
		ArmorLoadHandle->CancelHandle();
	}
	Super::EndPlay(EndPlayReason);
}

void UMT2EquipmentComponent::SetVisualComponents(
	USkeletalMeshComponent* InBodyMesh,
	USkeletalMeshComponent* InHairMesh,
	UStaticMeshComponent* InWeaponMesh,
	UStaticMeshComponent* InLeftWeaponMesh)
{
	BodyMeshComponent = InBodyMesh;
	HairMeshComponent = InHairMesh;
	WeaponMeshComponent = InWeaponMesh;
	LeftWeaponMeshComponent = InLeftWeaponMesh;
}

void UMT2EquipmentComponent::ApplyDefaultHair(const FMT2CharacterAppearance& Appearance)
{
	if (GetNetMode() == NM_DedicatedServer || !HairMeshComponent)
	{
		return;
	}

	const FMT2CharacterAppearanceAsset* Asset =
		GetDefault<UMT2CharacterAppearanceSettings>()->FindAppearance(Appearance);
	if (!Asset || Asset->DefaultHairMesh.IsNull())
	{
		HairMeshComponent->EmptyOverrideMaterials();
		HairMeshComponent->SetSkeletalMesh(nullptr);
		HairMeshComponent->SetVisibility(false, true);
		HairMeshComponent->SetComponentTickEnabled(false);
		return;
	}

	EquipHair(Asset->DefaultHairMesh, Asset->DefaultHairTexture);
}

void UMT2EquipmentComponent::EquipHair(
	TSoftObjectPtr<USkeletalMesh> Hair,
	TSoftObjectPtr<UTexture2D> DiffuseTexture)
{
	if (GetNetMode() == NM_DedicatedServer || !HairMeshComponent || Hair.IsNull())
	{
		return;
	}
	if (HairLoadHandle.IsValid())
	{
		HairLoadHandle->CancelHandle();
	}
	const int32 RequestId = ++HairRequestId;
	const FSoftObjectPath MeshPath = Hair.ToSoftObjectPath();
	const FSoftObjectPath TexturePath = DiffuseTexture.ToSoftObjectPath();
	TArray<FSoftObjectPath> Paths{MeshPath};
	if (TexturePath.IsValid())
	{
		Paths.Add(TexturePath);
	}
	HairLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		Paths,
		FStreamableDelegate::CreateUObject(
			this, &UMT2EquipmentComponent::OnHairLoaded, RequestId, MeshPath, TexturePath));
}

void UMT2EquipmentComponent::EquipWeapon(
	TSoftObjectPtr<UStaticMesh> Weapon, FName AttachBone,
	int32 WeaponSubType, int32 RefinementLevel)
{
	WeaponAttachBone = AttachBone.IsNone() ? FName(TEXT("equip_right")) : AttachBone;
	EquippedWeaponSubType = WeaponSubType;
	EquippedWeaponRefinementLevel = RefinementLevel;
	ClearWeaponGlitter();

	if (WeaponLoadHandle.IsValid())
	{
		WeaponLoadHandle->CancelHandle();
		WeaponLoadHandle.Reset();
	}
	const int32 RequestId = ++WeaponRequestId;
	if (GetNetMode() == NM_DedicatedServer || !WeaponMeshComponent || Weapon.IsNull())
	{
		// Weapon subtype is gameplay animation state and must remain available on dedicated servers.
		// Only the mesh load is presentation-only.
		if (WeaponMeshComponent)
		{
			WeaponMeshComponent->SetStaticMesh(nullptr);
			WeaponMeshComponent->SetVisibility(false, true);
		}
		if (LeftWeaponMeshComponent)
		{
			LeftWeaponMeshComponent->SetStaticMesh(nullptr);
			LeftWeaponMeshComponent->SetVisibility(false, true);
		}
		return;
	}
	const FSoftObjectPath MeshPath = Weapon.ToSoftObjectPath();
	WeaponLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		MeshPath,
		FStreamableDelegate::CreateUObject(
			this, &UMT2EquipmentComponent::OnWeaponLoaded, RequestId, MeshPath));
}

void UMT2EquipmentComponent::UnequipWeapon()
{
	EquippedWeaponSubType = INDEX_NONE;
	EquippedWeaponRefinementLevel = 0;
	ClearWeaponGlitter();
	++WeaponRequestId;
	if (WeaponLoadHandle.IsValid())
	{
		WeaponLoadHandle->CancelHandle();
		WeaponLoadHandle.Reset();
	}
	if (WeaponMeshComponent)
	{
		WeaponMeshComponent->SetStaticMesh(nullptr);
		WeaponMeshComponent->SetVisibility(false, true);
	}
	if (LeftWeaponMeshComponent)
	{
		LeftWeaponMeshComponent->SetStaticMesh(nullptr);
		LeftWeaponMeshComponent->SetVisibility(false, true);
	}
}

bool UMT2EquipmentComponent::HasWeaponEquipped() const
{
	return WeaponMeshComponent && WeaponMeshComponent->GetStaticMesh() != nullptr;
}

void UMT2EquipmentComponent::EquipArmor(
	TSoftObjectPtr<USkeletalMesh> Armor,
	const TArray<FMT2ArmorMaterialOverride>& MaterialOverrides,
	int32 RefinementLevel)
{
	if (GetNetMode() == NM_DedicatedServer || !BodyMeshComponent || Armor.IsNull())
	{
		return;
	}
	bArmorEquipped = true;
	EquippedArmorRefinementLevel = RefinementLevel;
	ClearArmorGlitter();
	PendingArmorMaterialOverrides = MaterialOverrides;

	if (ArmorLoadHandle.IsValid())
	{
		ArmorLoadHandle->CancelHandle();
	}
	const int32 RequestId = ++ArmorRequestId;
	const FSoftObjectPath MeshPath = Armor.ToSoftObjectPath();
	TArray<FSoftObjectPath> Paths{MeshPath};
	for (const FMT2ArmorMaterialOverride& Override : PendingArmorMaterialOverrides)
	{
		if (!Override.Material.IsNull())
		{
			Paths.AddUnique(Override.Material.ToSoftObjectPath());
		}
	}
	ArmorLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		Paths,
		FStreamableDelegate::CreateUObject(
			this, &UMT2EquipmentComponent::OnArmorLoaded, RequestId, MeshPath));
}

void UMT2EquipmentComponent::UnequipArmor(TSoftObjectPtr<USkeletalMesh> DefaultBodyMesh)
{
	bArmorEquipped = false;
	EquippedArmorRefinementLevel = 0;
	ClearArmorGlitter();
	PendingArmorMaterialOverrides.Reset();
	if (GetNetMode() == NM_DedicatedServer || !BodyMeshComponent || DefaultBodyMesh.IsNull())
	{
		return;
	}

	if (ArmorLoadHandle.IsValid())
	{
		ArmorLoadHandle->CancelHandle();
	}
	const int32 RequestId = ++ArmorRequestId;
	const FSoftObjectPath MeshPath = DefaultBodyMesh.ToSoftObjectPath();
	ArmorLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		MeshPath,
		FStreamableDelegate::CreateUObject(
			this, &UMT2EquipmentComponent::OnArmorLoaded, RequestId, MeshPath));
}

void UMT2EquipmentComponent::OnHairLoaded(
	int32 RequestId, FSoftObjectPath MeshPath, FSoftObjectPath TexturePath)
{
	if (RequestId != HairRequestId || !HairMeshComponent || !BodyMeshComponent)
	{
		return;
	}

	USkeletalMesh* HairMesh = Cast<USkeletalMesh>(MeshPath.ResolveObject());
	if (!HairMesh)
	{
		return;
	}
	HairMeshComponent->EmptyOverrideMaterials();
	HairMeshComponent->SetSkeletalMesh(HairMesh, true);
	HairMeshComponent->SetLeaderPoseComponent(BodyMeshComponent);
	HairMeshComponent->SetVisibility(true, true);
	HairMeshComponent->SetComponentTickEnabled(true);

	if (UTexture2D* HairTexture = Cast<UTexture2D>(TexturePath.ResolveObject()))
	{
		static const FName DiffuseParameterName(TEXT("Diffuse"));
		for (int32 Index = 0; Index < HairMeshComponent->GetNumMaterials(); ++Index)
		{
			if (UMaterialInterface* Material = HairMeshComponent->GetMaterial(Index))
			{
				if (UMaterialInstanceDynamic* DynamicMaterial =
					HairMeshComponent->CreateDynamicMaterialInstance(Index, Material))
				{
					DynamicMaterial->SetTextureParameterValue(DiffuseParameterName, HairTexture);
				}
			}
		}
	}
	HairLoadHandle.Reset();
}

void UMT2EquipmentComponent::OnWeaponLoaded(int32 RequestId, FSoftObjectPath MeshPath)
{
	if (RequestId != WeaponRequestId || !WeaponMeshComponent)
	{
		return;
	}
	WeaponMeshComponent->SetStaticMesh(Cast<UStaticMesh>(MeshPath.ResolveObject()));
	WeaponMeshComponent->SetVisibility(
		WeaponMeshComponent->GetStaticMesh() != nullptr && AttachWeaponMeshToBody(), true);
	if (LeftWeaponMeshComponent)
	{
		LeftWeaponMeshComponent->SetStaticMesh(
			UsesPairedWeaponVisual() ? WeaponMeshComponent->GetStaticMesh() : nullptr);
		LeftWeaponMeshComponent->SetVisibility(
			LeftWeaponMeshComponent->GetStaticMesh() != nullptr && AttachLeftWeaponMeshToBody(), true);
	}
	RefreshWeaponGlitter();
	WeaponLoadHandle.Reset();
}

void UMT2EquipmentComponent::OnArmorLoaded(int32 RequestId, FSoftObjectPath MeshPath)
{
	if (RequestId != ArmorRequestId || !BodyMeshComponent)
	{
		return;
	}
	if (USkeletalMesh* ArmorMesh = Cast<USkeletalMesh>(MeshPath.ResolveObject()))
	{
		// SetSkeletalMesh preserves dynamic material overrides. Those overrides were created for the
		// previous novice/armor mesh and its slot layout, so carrying them into this mesh corrupts the
		// face and armor textures by assigning them to unrelated sections.
		// Recreate the AnimInstance around the mesh swap as well. Keeping the live instance preserves
		// cached compact-pose bone indices from the previous mesh, which can distort Warrior poses.
		UClass* AnimInstanceClass = BodyMeshComponent->GetAnimClass();
		const bool bMeshChanged = BodyMeshComponent->GetSkeletalMeshAsset() != ArmorMesh;
		// Socket/proficiency replication reapplies equipment without changing the body mesh.
		// Preserve its live AnimInstance, including finishing fishing montages, in that case.
		if (bMeshChanged) { BodyMeshComponent->SetAnimInstanceClass(nullptr); }
		BodyMeshComponent->EmptyOverrideMaterials();
		if (bMeshChanged) { BodyMeshComponent->SetSkeletalMesh(ArmorMesh, true); }
		for (const FMT2ArmorMaterialOverride& Override : PendingArmorMaterialOverrides)
		{
			if (Override.MaterialSlotIndex >= 0 &&
				Override.MaterialSlotIndex < BodyMeshComponent->GetNumMaterials())
			{
				if (UMaterialInterface* Material = Override.Material.Get())
				{
					BodyMeshComponent->SetMaterial(Override.MaterialSlotIndex, Material);
				}
			}
		}
		if (bMeshChanged && AnimInstanceClass)
		{
			BodyMeshComponent->SetAnimationMode(EAnimationMode::AnimationBlueprint);
			BodyMeshComponent->SetAnimInstanceClass(AnimInstanceClass);
		}
		if (HairMeshComponent)
		{
			HairMeshComponent->SetLeaderPoseComponent(BodyMeshComponent, true, true);
		}
		if (WeaponMeshComponent && WeaponMeshComponent->GetStaticMesh())
		{
			WeaponMeshComponent->SetVisibility(AttachWeaponMeshToBody(), true);
		}
		if (LeftWeaponMeshComponent && LeftWeaponMeshComponent->GetStaticMesh())
		{
			LeftWeaponMeshComponent->SetVisibility(AttachLeftWeaponMeshToBody(), true);
		}
		RefreshArmorGlitter();
	}
	ArmorLoadHandle.Reset();
}

TArray<TSoftObjectPtr<UParticleSystem>> UMT2EquipmentComponent::ResolveWeaponGlitters() const
{
	TArray<TSoftObjectPtr<UParticleSystem>> Effects;
	for (int32 Level = 7; Level <= EquippedWeaponRefinementLevel; ++Level)
	{
		const FMT2RefinementGlitterSet* Set =
			UMT2GameplaySettings::Get().RefinementGlitterEffects.FindByPredicate(
				[Level](const FMT2RefinementGlitterSet& Candidate)
				{
					return Candidate.RefinementLevel == Level;
				});
		if (!Set)
		{
			continue;
		}

		// Old EWeaponSubTypes: sword=0, dagger=1, bow=2, two-hand=3, bell=4,
		// fan=5, arrow=6.
		TSoftObjectPtr<UParticleSystem> Effect;
		switch (EquippedWeaponSubType)
		{
		case 1:
		case 4:
		case 6:
			Effect = Set->SmallWeapon;
			break;
		case 2:
			Effect = Set->Bow;
			break;
		case 5:
			Effect = Set->FanBell;
			break;
		default:
			Effect = Set->Sword;
			break;
		}
		if (!Effect.IsNull())
		{
			Effects.Add(Effect);
		}
	}
	return Effects;
}

TArray<TSoftObjectPtr<UParticleSystem>> UMT2EquipmentComponent::ResolveArmorGlitters() const
{
	TArray<TSoftObjectPtr<UParticleSystem>> Effects;
	for (int32 Level = 7; Level <= EquippedArmorRefinementLevel; ++Level)
	{
		const FMT2RefinementGlitterSet* Set =
			UMT2GameplaySettings::Get().RefinementGlitterEffects.FindByPredicate(
				[Level](const FMT2RefinementGlitterSet& Candidate)
				{
					return Candidate.RefinementLevel == Level;
				});
		if (Set && !Set->BodyArmor.IsNull())
		{
			Effects.Add(Set->BodyArmor);
		}
	}
	return Effects;
}

void UMT2EquipmentComponent::GetWeaponGlitterRelativeTransform(
	FVector& OutLocation, FVector& OutScale) const
{
	OutLocation = FVector::ZeroVector;
	OutScale = FVector::OneVector;

	// The old client uses the same sword effect for one-hand and two-hand swords. Fit its authored
	// local-Z emitter volume to the actual imported weapon length so both categories remain covered.
	const bool bUsesSwordEffect = EquippedWeaponSubType == 0 || EquippedWeaponSubType == 3;
	const UStaticMesh* WeaponMesh =
		WeaponMeshComponent ? WeaponMeshComponent->GetStaticMesh() : nullptr;
	if (!bUsesSwordEffect || !WeaponMesh)
	{
		return;
	}

	const UMT2GameplaySettings& Settings = UMT2GameplaySettings::Get();
	const float AuthoredLength = FMath::Max(Settings.SwordGlitterAuthoredLength, 1.0f);
	const FBoxSphereBounds MeshBounds = WeaponMesh->GetBounds();
	const float MeshLength = MeshBounds.BoxExtent.Z * 2.0f;
	const float MinimumScale = FMath::Min(
		Settings.SwordGlitterMinimumLengthScale,
		Settings.SwordGlitterMaximumLengthScale);
	const float MaximumScale = FMath::Max(
		Settings.SwordGlitterMinimumLengthScale,
		Settings.SwordGlitterMaximumLengthScale);
	const float LengthScale = FMath::Clamp(
		MeshLength / AuthoredLength, MinimumScale, MaximumScale);

	OutScale.Z = LengthScale;
	OutLocation.Z =
		MeshBounds.Origin.Z - Settings.SwordGlitterAuthoredCenter * LengthScale;
}

void UMT2EquipmentComponent::RefreshWeaponGlitter()
{
	ClearWeaponGlitter();
	if (GetNetMode() == NM_DedicatedServer || !WeaponMeshComponent ||
		!WeaponMeshComponent->GetStaticMesh() || !WeaponMeshComponent->IsVisible())
	{
		return;
	}

	const TArray<TSoftObjectPtr<UParticleSystem>> Effects = ResolveWeaponGlitters();
	if (Effects.IsEmpty())
	{
		return;
	}
	const int32 RequestId = ++WeaponGlitterRequestId;
	TArray<FSoftObjectPath> EffectPaths;
	EffectPaths.Reserve(Effects.Num());
	bool bAllLoaded = true;
	for (const TSoftObjectPtr<UParticleSystem>& Effect : Effects)
	{
		EffectPaths.Add(Effect.ToSoftObjectPath());
		bAllLoaded &= Effect.IsValid();
	}
	if (bAllLoaded)
	{
		OnWeaponGlittersLoaded(RequestId, MoveTemp(EffectPaths));
		return;
	}
	WeaponGlitterLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		EffectPaths,
		FStreamableDelegate::CreateUObject(
			this, &UMT2EquipmentComponent::OnWeaponGlittersLoaded, RequestId, EffectPaths));
}

void UMT2EquipmentComponent::RefreshArmorGlitter()
{
	ClearArmorGlitter();
	if (GetNetMode() == NM_DedicatedServer || !bArmorEquipped || !BodyMeshComponent ||
		!BodyMeshComponent->GetSkeletalMeshAsset())
	{
		return;
	}

	const TArray<TSoftObjectPtr<UParticleSystem>> Effects = ResolveArmorGlitters();
	if (Effects.IsEmpty())
	{
		return;
	}
	const int32 RequestId = ++ArmorGlitterRequestId;
	TArray<FSoftObjectPath> EffectPaths;
	EffectPaths.Reserve(Effects.Num());
	bool bAllLoaded = true;
	for (const TSoftObjectPtr<UParticleSystem>& Effect : Effects)
	{
		EffectPaths.Add(Effect.ToSoftObjectPath());
		bAllLoaded &= Effect.IsValid();
	}
	if (bAllLoaded)
	{
		OnArmorGlittersLoaded(RequestId, MoveTemp(EffectPaths));
		return;
	}
	ArmorGlitterLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		EffectPaths,
		FStreamableDelegate::CreateUObject(
			this, &UMT2EquipmentComponent::OnArmorGlittersLoaded, RequestId, EffectPaths));
}

void UMT2EquipmentComponent::OnWeaponGlittersLoaded(
	int32 RequestId, TArray<FSoftObjectPath> EffectPaths)
{
	if (RequestId != WeaponGlitterRequestId || !BodyMeshComponent || !WeaponMeshComponent ||
		!WeaponMeshComponent->GetStaticMesh() || !WeaponMeshComponent->IsVisible())
	{
		return;
	}
	FVector RelativeLocation;
	FVector RelativeScale;
	GetWeaponGlitterRelativeTransform(RelativeLocation, RelativeScale);
	for (const FSoftObjectPath& EffectPath : EffectPaths)
	{
		if (UParticleSystem* Effect = Cast<UParticleSystem>(EffectPath.ResolveObject()))
		{
			// The converted weapon component carries the source-basis correction. Parenting every
			// refinement layer to it keeps the cumulative effects aligned to the blade.
			if (UParticleSystemComponent* Component = UGameplayStatics::SpawnEmitterAttached(
				Effect, WeaponMeshComponent, NAME_None, RelativeLocation, FRotator::ZeroRotator,
				RelativeScale, EAttachLocation::KeepRelativeOffset,
				false, EPSCPoolMethod::None, true))
			{
				WeaponGlitterComponents.Add(Component);
			}
		}
	}
	WeaponGlitterLoadHandle.Reset();
}

void UMT2EquipmentComponent::OnArmorGlittersLoaded(
	int32 RequestId, TArray<FSoftObjectPath> EffectPaths)
{
	if (RequestId != ArmorGlitterRequestId || !bArmorEquipped || !BodyMeshComponent)
	{
		return;
	}
	static const FName RootBone(TEXT("Bip01"));
	const FName AttachBone = BodyMeshComponent->DoesSocketExist(RootBone) ? RootBone : NAME_None;
	for (const FSoftObjectPath& EffectPath : EffectPaths)
	{
		if (UParticleSystem* Effect = Cast<UParticleSystem>(EffectPath.ResolveObject()))
		{
			if (UParticleSystemComponent* Component = UGameplayStatics::SpawnEmitterAttached(
				Effect, BodyMeshComponent, AttachBone, FVector::ZeroVector, FRotator::ZeroRotator,
				FVector::OneVector, EAttachLocation::KeepRelativeOffset,
				false, EPSCPoolMethod::None, true))
			{
				ArmorGlitterComponents.Add(Component);
			}
		}
	}
	ArmorGlitterLoadHandle.Reset();
}

void UMT2EquipmentComponent::ClearWeaponGlitter()
{
	++WeaponGlitterRequestId;
	if (WeaponGlitterLoadHandle.IsValid())
	{
		WeaponGlitterLoadHandle->CancelHandle();
		WeaponGlitterLoadHandle.Reset();
	}
	for (UParticleSystemComponent* Component : WeaponGlitterComponents)
	{
		if (Component)
		{
			Component->DeactivateSystem();
			Component->DestroyComponent();
		}
	}
	WeaponGlitterComponents.Reset();
}

void UMT2EquipmentComponent::ClearArmorGlitter()
{
	++ArmorGlitterRequestId;
	if (ArmorGlitterLoadHandle.IsValid())
	{
		ArmorGlitterLoadHandle->CancelHandle();
		ArmorGlitterLoadHandle.Reset();
	}
	for (UParticleSystemComponent* Component : ArmorGlitterComponents)
	{
		if (Component)
		{
			Component->DeactivateSystem();
			Component->DestroyComponent();
		}
	}
	ArmorGlitterComponents.Reset();
}

bool UMT2EquipmentComponent::AttachWeaponMeshToBody()
{
	if (!BodyMeshComponent || !WeaponMeshComponent)
	{
		return false;
	}

	FName ResolvedBone = WeaponAttachBone;
	if (!BodyMeshComponent->DoesSocketExist(ResolvedBone))
	{
		static const FName FallbackBones[] = {TEXT("equip_right_hand"), TEXT("equip_right")};
		ResolvedBone = NAME_None;
		for (const FName Candidate : FallbackBones)
		{
			if (BodyMeshComponent->DoesSocketExist(Candidate))
			{
				ResolvedBone = Candidate;
				break;
			}
		}
	}

	if (ResolvedBone.IsNone())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[MT2Equipment] Character mesh %s has no weapon attachment bone (%s)."),
			*GetNameSafe(BodyMeshComponent->GetSkeletalMeshAsset()), *WeaponAttachBone.ToString());
		return false;
	}

	WeaponAttachBone = ResolvedBone;
	WeaponMeshComponent->AttachToComponent(
		BodyMeshComponent, FAttachmentTransformRules::SnapToTargetNotIncludingScale, ResolvedBone);
	return true;
}

bool UMT2EquipmentComponent::UsesPairedWeaponVisual() const
{
	// Original weapon subtypes: 1 = dagger/knife, 5 = fan. Both use a second copy in the left hand.
	return EquippedWeaponSubType == 1 || EquippedWeaponSubType == 5;
}

bool UMT2EquipmentComponent::AttachLeftWeaponMeshToBody()
{
	if (!BodyMeshComponent || !LeftWeaponMeshComponent || !UsesPairedWeaponVisual())
	{
		return false;
	}

	static const FName LeftBones[] = {TEXT("equip_left_hand"), TEXT("equip_left")};
	FName ResolvedBone = NAME_None;
	for (const FName Candidate : LeftBones)
	{
		if (BodyMeshComponent->DoesSocketExist(Candidate))
		{
			ResolvedBone = Candidate;
			break;
		}
	}
	if (ResolvedBone.IsNone())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[MT2Equipment] Character mesh %s has no left-hand weapon attachment bone."),
			*GetNameSafe(BodyMeshComponent->GetSkeletalMeshAsset()));
		return false;
	}

	LeftWeaponMeshComponent->AttachToComponent(
		BodyMeshComponent, FAttachmentTransformRules::SnapToTargetNotIncludingScale, ResolvedBone);
	return true;
}
