/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Characters/MT2CharacterAppearanceComponent.h"

#include "Animation/AnimInstance.h"
#include "Characters/MT2CharacterAppearanceSettings.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInstanceDynamic.h"

UMT2CharacterAppearanceComponent::UMT2CharacterAppearanceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UMT2CharacterAppearanceComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!MeshComponent)
	{
		if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
		{
			MeshComponent = Character->GetMesh();
		}
	}
}

void UMT2CharacterAppearanceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	++ActiveRequestId;
	if (LoadHandle.IsValid())
	{
		LoadHandle->CancelHandle();
		LoadHandle.Reset();
	}

	Super::EndPlay(EndPlayReason);
}

void UMT2CharacterAppearanceComponent::SetMeshComponent(USkeletalMeshComponent* InMeshComponent)
{
	MeshComponent = InMeshComponent;
}

bool UMT2CharacterAppearanceComponent::ApplyCharacterAppearance(const FMT2CharacterAppearance& Appearance)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return false;
	}

	const UMT2CharacterAppearanceSettings* Settings = GetDefault<UMT2CharacterAppearanceSettings>();
	const FMT2CharacterAppearanceAsset* Asset = Settings->FindAppearance(Appearance);
	if (!Asset || Asset->Mesh.IsNull() || Asset->DiffuseTexture.IsNull())
	{
		return false;
	}

	TSoftClassPtr<UAnimInstance> AnimInstanceClass;
	if (const FMT2CharacterAnimationProfile* Profile =
		Settings->FindAnimationProfile(Appearance.Race, Appearance.Sex))
	{
		AnimInstanceClass = Profile->AnimInstanceClass;
	}

	ApplyVisualProfile(Asset->Mesh, Asset->DiffuseTexture, AnimInstanceClass, Appearance);
	return true;
}

void UMT2CharacterAppearanceComponent::ApplyVisualProfile(
	TSoftObjectPtr<USkeletalMesh> Mesh,
	TSoftObjectPtr<UTexture2D> DiffuseTexture,
	TSoftClassPtr<UAnimInstance> AnimInstanceClass,
	const FMT2CharacterAppearance& Appearance)
{
	if (!MeshComponent || Mesh.IsNull())
	{
		return;
	}

	if (LoadHandle.IsValid())
	{
		LoadHandle->CancelHandle();
	}

	const int32 RequestId = ++ActiveRequestId;
	const FSoftObjectPath MeshPath = Mesh.ToSoftObjectPath();
	const FSoftObjectPath TexturePath = DiffuseTexture.ToSoftObjectPath();
	const FSoftObjectPath AnimClassPath = AnimInstanceClass.ToSoftObjectPath();
	TArray<FSoftObjectPath> AssetsToLoad{MeshPath};
	if (TexturePath.IsValid())
	{
		AssetsToLoad.Add(TexturePath);
	}
	if (AnimClassPath.IsValid())
	{
		AssetsToLoad.Add(AnimClassPath);
	}

	LoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		AssetsToLoad,
		FStreamableDelegate::CreateUObject(
			this,
			&UMT2CharacterAppearanceComponent::OnVisualAssetsLoaded,
			RequestId,
			Appearance,
			MeshPath,
			TexturePath,
			AnimClassPath));
}

void UMT2CharacterAppearanceComponent::OnVisualAssetsLoaded(
	int32 RequestId,
	FMT2CharacterAppearance Appearance,
	FSoftObjectPath MeshPath,
	FSoftObjectPath TexturePath,
	FSoftObjectPath AnimClassPath)
{
	if (RequestId != ActiveRequestId || !MeshComponent)
	{
		return;
	}

	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(MeshPath.ResolveObject());
	if (!SkeletalMesh)
	{
		return;
	}

	// Dynamic style materials belong to the previous skeletal mesh. UE intentionally preserves
	// OverrideMaterials across SetSkeletalMesh, but material slot indices are not interchangeable
	// between the novice body and armor meshes. Keeping them makes the old diffuse texture override
	// unrelated armor/face sections and looks like corrupted UVs.
	// Destroy the previous race's AnimInstance before changing skeletons. Otherwise SetSkeletalMesh
	// initializes it once against the new mesh and evaluates stale animation nodes until the new
	// AnimBP class is installed (visible with Warrior because its hierarchy/reference pose differs).
	MeshComponent->SetAnimInstanceClass(nullptr);
	MeshComponent->EmptyOverrideMaterials();
	MeshComponent->SetSkeletalMesh(SkeletalMesh, true);
	if (UClass* AnimClass = Cast<UClass>(AnimClassPath.ResolveObject()))
	{
		if (AnimClass->IsChildOf(UAnimInstance::StaticClass()))
		{
			MeshComponent->SetAnimationMode(EAnimationMode::AnimationBlueprint);
			MeshComponent->SetAnimInstanceClass(AnimClass);
		}
	}

	if (UTexture2D* DiffuseTexture = Cast<UTexture2D>(TexturePath.ResolveObject()))
	{
		static const FName DiffuseParameterName(TEXT("Diffuse"));
		for (int32 MaterialIndex = 0; MaterialIndex < MeshComponent->GetNumMaterials(); ++MaterialIndex)
		{
			UMaterialInterface* Material = MeshComponent->GetMaterial(MaterialIndex);
			if (!Material || !Material->GetName().Contains(TEXT("_novice_"), ESearchCase::IgnoreCase))
			{
				continue;
			}

			if (UMaterialInstanceDynamic* DynamicMaterial =
				MeshComponent->CreateDynamicMaterialInstance(MaterialIndex, Material))
			{
				DynamicMaterial->SetTextureParameterValue(DiffuseParameterName, DiffuseTexture);
			}
		}
	}

	AppliedAppearance = Appearance;
	bHasAppliedAppearance = true;
	OnAppearanceApplied.Broadcast(AppliedAppearance);
	LoadHandle.Reset();
}
