/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2CharacterPreviewActor.h"

#include "Animation/AnimInstance.h"
#include "Characters/MT2CharacterAppearanceSettings.h"
#include "Config/MT2GameplaySettings.h"
#include "Core/MT2VnumRegistrySubsystem.h"
#include "Components/SceneComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Items/MT2ItemTemplate.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Particles/ParticleSystemComponent.h"
#include "Server/MT2ServerRuntimeTypes.h"

AMT2CharacterPreviewActor::AMT2CharacterPreviewActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	SetCanBeDamaged(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	BodyMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(SceneRoot);
	BodyMesh->SetRelativeRotation(FRotator(0.0f, 90.0f + 20.0f, 0.0f));
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyMesh->SetGenerateOverlapEvents(false);
	BodyMesh->SetReceivesDecals(false);
	BodyMesh->SetVisibleInSceneCaptureOnly(true);
	BodyMesh->bForceMipStreaming = true;

	HairMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("HairMesh"));
	HairMesh->SetupAttachment(BodyMesh);
	HairMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HairMesh->SetGenerateOverlapEvents(false);
	HairMesh->SetReceivesDecals(false);
	HairMesh->SetVisibleInSceneCaptureOnly(true);
	HairMesh->bForceMipStreaming = true;

	WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	WeaponMesh->SetupAttachment(SceneRoot);
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponMesh->SetGenerateOverlapEvents(false);
	WeaponMesh->SetReceivesDecals(false);
	WeaponMesh->SetVisibleInSceneCaptureOnly(true);
	WeaponMesh->bForceMipStreaming = true;
	WeaponMesh->SetVisibility(false, true);

	WeaponGlitter = CreateDefaultSubobject<UParticleSystemComponent>(TEXT("WeaponGlitter"));
	WeaponGlitter->SetupAttachment(WeaponMesh);
	WeaponGlitter->SetAutoActivate(false);
	WeaponGlitter->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponGlitter->SetVisibleInSceneCaptureOnly(true);
	WeaponGlitter->SetVisibility(false, true);

	ArmorGlitter = CreateDefaultSubobject<UParticleSystemComponent>(TEXT("ArmorGlitter"));
	ArmorGlitter->SetupAttachment(BodyMesh);
	ArmorGlitter->SetAutoActivate(false);
	ArmorGlitter->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ArmorGlitter->SetVisibleInSceneCaptureOnly(true);
	ArmorGlitter->SetVisibility(false, true);

	SceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("SceneCapture"));
	SceneCapture->SetupAttachment(SceneRoot);
	SceneCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorHDR;
	SceneCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	SceneCapture->bCaptureEveryFrame = true;
	SceneCapture->bCaptureOnMovement = true;
	SceneCapture->bAlwaysPersistRenderingState = true;
	SceneCapture->FOVAngle = 30.0f;
	SceneCapture->ShowFlags.SetMotionBlur(false);
	SceneCapture->ShowFlags.SetTemporalAA(false);
	SceneCapture->ShowFlags.SetAntiAliasing(false);

	// A local light avoids affecting the gateway world. It follows the capture camera and points
	// toward the character, so the face remains lit from the front.
	FrontLight = CreateDefaultSubobject<USpotLightComponent>(TEXT("FrontLight"));
	FrontLight->SetupAttachment(SceneRoot);
	FrontLight->SetIntensity(16000.0f);
	FrontLight->SetLightColor(FLinearColor::White);
	FrontLight->SetCastShadows(false);
	FrontLight->SetAttenuationRadius(1200.0f);
	FrontLight->SetInnerConeAngle(35.0f);
	FrontLight->SetOuterConeAngle(55.0f);
}

void AMT2CharacterPreviewActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	RefreshCaptureComponentList();
}

UTextureRenderTarget2D* AMT2CharacterPreviewActor::InitializePreview(const FIntPoint& Resolution)
{
	const int32 Width = FMath::Clamp(Resolution.X, 64, 2048);
	const int32 Height = FMath::Clamp(Resolution.Y, 64, 2048);

	RenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("CharacterPreviewRenderTarget"));
	RenderTarget->ClearColor = FLinearColor::Transparent;
	RenderTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA16f;
	RenderTarget->InitAutoFormat(Width, Height);
	RenderTarget->UpdateResourceImmediate(true);

	SceneCapture->TextureTarget = RenderTarget;
	RefreshCaptureComponentList();
	return RenderTarget;
}

void AMT2CharacterPreviewActor::ApplyAppearance(const FMT2CharacterAppearance& Appearance)
{
	const UMT2CharacterAppearanceSettings* Settings =
		GetDefault<UMT2CharacterAppearanceSettings>();
	const FMT2CharacterAppearanceAsset* Asset = Settings->FindAppearance(Appearance);
	if (!Asset)
	{
		BodyMesh->SetSkeletalMesh(nullptr);
		HairMesh->SetSkeletalMesh(nullptr);
		return;
	}

	BodyMesh->SetAnimInstanceClass(nullptr);
	BodyMesh->EmptyOverrideMaterials();
	BodyMesh->SetSkeletalMesh(Asset->Mesh.LoadSynchronous(), true);
	if (const FMT2CharacterAnimationProfile* AnimationProfile =
		Settings->FindAnimationProfile(Appearance.Race, Appearance.Sex))
	{
		if (UClass* AnimClass = AnimationProfile->AnimInstanceClass.LoadSynchronous())
		{
			BodyMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
			BodyMesh->SetAnimInstanceClass(AnimClass);
		}
	}

	if (UTexture2D* DiffuseTexture = Asset->DiffuseTexture.LoadSynchronous())
	{
		static const FName DiffuseParameterName(TEXT("Diffuse"));
		for (int32 MaterialIndex = 0; MaterialIndex < BodyMesh->GetNumMaterials(); ++MaterialIndex)
		{
			UMaterialInterface* Material = BodyMesh->GetMaterial(MaterialIndex);
			if (!Material ||
				!Material->GetName().Contains(TEXT("_novice_"), ESearchCase::IgnoreCase))
			{
				continue;
			}
			if (UMaterialInstanceDynamic* DynamicMaterial =
				BodyMesh->CreateDynamicMaterialInstance(MaterialIndex, Material))
			{
				DynamicMaterial->SetTextureParameterValue(DiffuseParameterName, DiffuseTexture);
			}
		}
	}

	HairMesh->EmptyOverrideMaterials();
	HairMesh->SetSkeletalMesh(Asset->DefaultHairMesh.LoadSynchronous(), true);
	HairMesh->SetLeaderPoseComponent(BodyMesh);
	HairMesh->SetVisibility(HairMesh->GetSkeletalMeshAsset() != nullptr, true);
	if (UTexture2D* HairTexture = Asset->DefaultHairTexture.LoadSynchronous())
	{
		static const FName DiffuseParameterName(TEXT("Diffuse"));
		for (int32 MaterialIndex = 0; MaterialIndex < HairMesh->GetNumMaterials(); ++MaterialIndex)
		{
			if (UMaterialInterface* Material = HairMesh->GetMaterial(MaterialIndex))
			{
				if (UMaterialInstanceDynamic* DynamicMaterial =
					HairMesh->CreateDynamicMaterialInstance(MaterialIndex, Material))
				{
					DynamicMaterial->SetTextureParameterValue(DiffuseParameterName, HairTexture);
				}
			}
		}
	}

	BodyMesh->UpdateBounds();
	HairMesh->UpdateBounds();
	BodyMesh->PrestreamTextures(30.0f, true);
	HairMesh->PrestreamTextures(30.0f, true);
	FrameCapture();
}

void AMT2CharacterPreviewActor::ApplyCharacterSummary(const FMT2CharacterSummary& Character)
{
	ApplyAppearance(Character.Appearance);

	UGameInstance* GameInstance = GetGameInstance();
	UMT2VnumRegistrySubsystem* Registry = GameInstance
		? GameInstance->GetSubsystem<UMT2VnumRegistrySubsystem>() : nullptr;
	if (!Registry)
	{
		return;
	}

	WeaponGlitter->DeactivateSystem();
	WeaponGlitter->SetVisibility(false, true);
	WeaponGlitter->SetTemplate(nullptr);
	ArmorGlitter->DeactivateSystem();
	ArmorGlitter->SetVisibility(false, true);
	ArmorGlitter->SetTemplate(nullptr);

	const UMT2ItemWeaponTemplate* Weapon = Cast<UMT2ItemWeaponTemplate>(
		Registry->ResolveItemTemplateClass(Character.EquippedWeaponVnum).GetDefaultObject());
	UStaticMesh* LoadedWeaponMesh = Weapon ? Weapon->WorldMesh.LoadSynchronous() : nullptr;
	WeaponMesh->SetStaticMesh(LoadedWeaponMesh);
	WeaponMesh->SetVisibility(false, true);
	if (Weapon && LoadedWeaponMesh)
	{
		FName AttachBone = Character.Appearance.Race == EMT2CharacterRace::Warrior
			? FName(TEXT("equip_right_hand")) : FName(TEXT("equip_right"));
		if (!BodyMesh->DoesSocketExist(AttachBone))
		{
			AttachBone = BodyMesh->DoesSocketExist(TEXT("equip_right_hand"))
				? FName(TEXT("equip_right_hand")) : FName(TEXT("equip_right"));
		}
		if (BodyMesh->DoesSocketExist(AttachBone))
		{
			WeaponMesh->AttachToComponent(
				BodyMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, AttachBone);
			WeaponMesh->SetVisibility(true, true);
		}

		const FMT2RefinementGlitterSet* GlitterSet =
			UMT2GameplaySettings::Get().RefinementGlitterEffects.FindByPredicate(
				[Weapon](const FMT2RefinementGlitterSet& Candidate)
				{
					return Candidate.RefinementLevel == Weapon->RefinementLevel;
				});
		if (GlitterSet && WeaponMesh->IsVisible())
		{
			TSoftObjectPtr<UParticleSystem> GlitterAsset;
			switch (Weapon->WeaponSubType)
			{
			case 1:
			case 4:
			case 6:
				GlitterAsset = GlitterSet->SmallWeapon;
				break;
			case 2:
				GlitterAsset = GlitterSet->Bow;
				break;
			case 5:
				GlitterAsset = GlitterSet->FanBell;
				break;
			default:
				GlitterAsset = GlitterSet->Sword;
				break;
			}
			UParticleSystem* Glitter = GlitterAsset.LoadSynchronous();
			if (Glitter)
			{
				FVector RelativeLocation = FVector::ZeroVector;
				FVector RelativeScale = FVector::OneVector;
				if (Weapon->WeaponSubType == 0 || Weapon->WeaponSubType == 3)
				{
					const UMT2GameplaySettings& Settings = UMT2GameplaySettings::Get();
					const FBoxSphereBounds Bounds = LoadedWeaponMesh->GetBounds();
					const float LengthScale = FMath::Clamp(
						(Bounds.BoxExtent.Z * 2.0f) /
							FMath::Max(Settings.SwordGlitterAuthoredLength, 1.0f),
						FMath::Min(Settings.SwordGlitterMinimumLengthScale,
							Settings.SwordGlitterMaximumLengthScale),
						FMath::Max(Settings.SwordGlitterMinimumLengthScale,
							Settings.SwordGlitterMaximumLengthScale));
					RelativeScale.Z = LengthScale;
					RelativeLocation.Z =
						Bounds.Origin.Z - Settings.SwordGlitterAuthoredCenter * LengthScale;
				}
				WeaponGlitter->SetTemplate(Glitter);
				WeaponGlitter->AttachToComponent(
					WeaponMesh, FAttachmentTransformRules::KeepRelativeTransform);
				WeaponGlitter->SetRelativeTransform(FTransform(
					FRotator::ZeroRotator, RelativeLocation, RelativeScale));
				WeaponGlitter->SetVisibility(true, true);
				WeaponGlitter->ActivateSystem(true);
			}
		}
	}

	const FMT2CharacterAppearanceAsset* DefaultAppearance =
		GetDefault<UMT2CharacterAppearanceSettings>()->FindAppearance(Character.Appearance);
	const UMT2ItemArmorTemplate* Armor = Cast<UMT2ItemArmorTemplate>(
		Registry->ResolveItemTemplateClass(Character.EquippedArmorVnum).GetDefaultObject());
	if (Armor)
	{
		const FMT2ArmorMeshVariant* Variant = Armor->ResolveArmorVariant(Character.Appearance);
		const TSoftObjectPtr<USkeletalMesh> ArmorMesh =
			Variant ? Variant->Mesh : Armor->ArmorMesh;
		if (USkeletalMesh* LoadedArmorMesh = ArmorMesh.LoadSynchronous())
		{
			UClass* AnimClass = BodyMesh->GetAnimClass();
			BodyMesh->SetAnimInstanceClass(nullptr);
			BodyMesh->EmptyOverrideMaterials();
			BodyMesh->SetSkeletalMesh(LoadedArmorMesh, true);
			if (Variant)
			{
				for (const FMT2ArmorMaterialOverride& Override : Variant->MaterialOverrides)
				{
					if (Override.MaterialSlotIndex >= 0 &&
						Override.MaterialSlotIndex < BodyMesh->GetNumMaterials())
					{
						BodyMesh->SetMaterial(
							Override.MaterialSlotIndex, Override.Material.LoadSynchronous());
					}
				}
			}
			if (AnimClass)
			{
				BodyMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
				BodyMesh->SetAnimInstanceClass(AnimClass);
			}
			HairMesh->SetLeaderPoseComponent(BodyMesh, true, true);

			const FMT2RefinementGlitterSet* GlitterSet =
				UMT2GameplaySettings::Get().RefinementGlitterEffects.FindByPredicate(
					[Armor](const FMT2RefinementGlitterSet& Candidate)
					{
						return Candidate.RefinementLevel == Armor->RefinementLevel;
					});
			if (GlitterSet)
			{
				UParticleSystem* Glitter = GlitterSet->BodyArmor.LoadSynchronous();
				if (Glitter)
				{
					const FName RootBone =
						BodyMesh->DoesSocketExist(TEXT("Bip01")) ? FName(TEXT("Bip01")) : NAME_None;
					ArmorGlitter->SetTemplate(Glitter);
					ArmorGlitter->AttachToComponent(
						BodyMesh, FAttachmentTransformRules::KeepRelativeTransform, RootBone);
					ArmorGlitter->SetRelativeTransform(FTransform::Identity);
					ArmorGlitter->SetVisibility(true, true);
					ArmorGlitter->ActivateSystem(true);
				}
			}
		}
	}
	else if (DefaultAppearance)
	{
		BodyMesh->SetSkeletalMesh(DefaultAppearance->Mesh.LoadSynchronous(), true);
	}

	const UMT2ItemCostumeTemplate* HairCostume = Cast<UMT2ItemCostumeTemplate>(
		Registry->ResolveItemTemplateClass(Character.EquippedHairVnum).GetDefaultObject());
	const FMT2HairMeshVariant* HairVariant =
		HairCostume ? HairCostume->ResolveHairVariant(Character.Appearance) : nullptr;
	if (HairVariant && !HairVariant->Mesh.IsNull())
	{
		HairMesh->EmptyOverrideMaterials();
		HairMesh->SetSkeletalMesh(HairVariant->Mesh.LoadSynchronous(), true);
		HairMesh->SetLeaderPoseComponent(BodyMesh);
		HairMesh->SetVisibility(HairMesh->GetSkeletalMeshAsset() != nullptr, true);
		if (UTexture2D* HairTexture = HairVariant->DiffuseTexture.LoadSynchronous())
		{
			static const FName DiffuseParameterName(TEXT("Diffuse"));
			for (int32 MaterialIndex = 0; MaterialIndex < HairMesh->GetNumMaterials(); ++MaterialIndex)
			{
				if (UMaterialInterface* Material = HairMesh->GetMaterial(MaterialIndex))
				{
					if (UMaterialInstanceDynamic* DynamicMaterial =
						HairMesh->CreateDynamicMaterialInstance(MaterialIndex, Material))
					{
						DynamicMaterial->SetTextureParameterValue(
							DiffuseParameterName, HairTexture);
					}
				}
			}
		}
	}
	BodyMesh->PrestreamTextures(30.0f, true);
	HairMesh->PrestreamTextures(30.0f, true);
	WeaponMesh->PrestreamTextures(30.0f, true);
	FrameCapture();
}

void AMT2CharacterPreviewActor::SetPreviewActive(bool bActive)
{
	SetActorTickEnabled(bActive);
	if (SceneCapture)
	{
		SceneCapture->bCaptureEveryFrame = bActive;
		SceneCapture->bCaptureOnMovement = bActive;
		SceneCapture->SetComponentTickEnabled(bActive);
	}
}

void AMT2CharacterPreviewActor::RefreshCaptureComponentList()
{
	if (!SceneCapture)
	{
		return;
	}

	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(this);
	uint32 NewHash = GetTypeHash(PrimitiveComponents.Num());
	for (UPrimitiveComponent* Component : PrimitiveComponents)
	{
		NewHash = HashCombineFast(NewHash, PointerHash(Component));
	}
	if (NewHash == CaptureComponentHash)
	{
		return;
	}

	CaptureComponentHash = NewHash;
	SceneCapture->ClearShowOnlyComponents();
	for (UPrimitiveComponent* Component : PrimitiveComponents)
	{
		if (!Component)
		{
			continue;
		}
		Component->SetVisibleInSceneCaptureOnly(true);
		SceneCapture->ShowOnlyComponent(Component);
	}
}

void AMT2CharacterPreviewActor::FrameCapture()
{
	const FVector Center = GetActorLocation() + FVector(0.0f, 0.0f, 90.0f);
	const float CameraDistance = 400.0f;
	const FVector CameraLocation(Center.X + CameraDistance, Center.Y, Center.Z);
	const FRotator CameraRotation(0.0f, 180.0f, 0.0f);

	SceneCapture->SetWorldLocationAndRotation(CameraLocation, CameraRotation);
	FrontLight->SetWorldLocationAndRotation(
		CameraLocation + FVector(0.0f, 0.0f, 30.0f),
		(CameraLocation - Center).Rotation() + FRotator(0.0f, 180.0f, 0.0f));
}
