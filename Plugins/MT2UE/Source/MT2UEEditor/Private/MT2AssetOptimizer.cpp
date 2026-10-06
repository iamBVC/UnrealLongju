/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "MT2AssetOptimizer.h"
#include "Config/MT2PathSettings.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/PrimitiveComponent.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshLODSettings.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "LODUtilities.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Package.h"

namespace
{
	bool IsImportedAsset(const FAssetData& Asset)
	{
		const FString Name = Asset.AssetName.ToString();
		return Name.StartsWith(TEXT("SM_"), ESearchCase::IgnoreCase)
			|| Name.StartsWith(TEXT("SK_"), ESearchCase::IgnoreCase)
			|| Name.StartsWith(TEXT("T_"), ESearchCase::IgnoreCase);
	}

	bool IsUITexture(const UTexture2D* Texture)
	{
		return Texture && Texture->GetPathName().Contains(TEXT("/ui/"), ESearchCase::IgnoreCase);
	}
}

FString FMT2AssetOptimizationResult::BuildSummary() const
{
	return FString::Printf(
		TEXT("Optimization complete.\n\nStatic meshes: %d\nSkeletal meshes: %d\nTextures: %d\nWorld components: %d\nWarnings: %d"),
		StaticMeshes, SkeletalMeshes, Textures, WorldComponents, Warnings.Num());
}

bool FMT2AssetOptimizer::ConfigureStaticMesh(UStaticMesh* Mesh)
{
	if (!Mesh)
	{
		return false;
	}

	Mesh->Modify();
	if (Mesh->GetNumSourceModels() <= 1)
	{
		Mesh->SetAutoComputeLODScreenSize(true);
		Mesh->SetLODGroup(TEXT("MT2World"), true);
	}
	Mesh->MarkPackageDirty();
	return true;
}

bool FMT2AssetOptimizer::ConfigureSkeletalMesh(USkeletalMesh* Mesh)
{
	if (!Mesh)
	{
		return false;
	}

	USkeletalMeshLODSettings* CharacterLODSettings = LoadObject<USkeletalMeshLODSettings>(
		nullptr, UMT2PathSettings::Path(TEXT("Optimizations_CharactersLODs")));
	if (!CharacterLODSettings)
	{
		return false;
	}

	Mesh->Modify();
	Mesh->SetLODSettings(CharacterLODSettings);
	const int32 DesiredLODCount = FMath::Max(1, CharacterLODSettings->GetNumberOfSettings());
	if (!FLODUtilities::RegenerateLOD(
		Mesh,
		GetTargetPlatformManagerRef().GetRunningTargetPlatform(),
		DesiredLODCount,
		false,
		false))
	{
		return false;
	}
	Mesh->PostEditChange();
	Mesh->MarkPackageDirty();
	return true;
}

bool FMT2AssetOptimizer::ConfigureTexture(UTexture2D* Texture)
{
	if (!Texture)
	{
		return false;
	}

	Texture->Modify();
	if (IsUITexture(Texture))
	{
		Texture->LODGroup = TEXTUREGROUP_UI;
		Texture->MipGenSettings = TMGS_NoMipmaps;
		Texture->NeverStream = true;
	}
	else
	{
		const FString Path = Texture->GetPathName();
		Texture->LODGroup = Path.Contains(TEXT("character"), ESearchCase::IgnoreCase)
			|| Path.Contains(TEXT("/pc/"), ESearchCase::IgnoreCase)
			|| Path.Contains(TEXT("/pc2/"), ESearchCase::IgnoreCase)
			? TEXTUREGROUP_Character
			: TEXTUREGROUP_World;
		Texture->MipGenSettings = TMGS_FromTextureGroup;
		Texture->NeverStream = false;
	}
	Texture->PostEditChange();
	Texture->UpdateResource();
	Texture->MarkPackageDirty();
	return true;
}

float FMT2AssetOptimizer::ComputeMapObjectCullDistance(const FBoxSphereBounds& Bounds)
{
	const float Radius = Bounds.SphereRadius;
	if (Radius <= 150.0f) return 15000.0f;
	if (Radius <= 500.0f) return 30000.0f;
	if (Radius <= 1500.0f) return 60000.0f;
	return 100000.0f;
}

bool FMT2AssetOptimizer::OptimizeImportedAssets(FMT2AssetOptimizationResult& OutResult)
{
	FAssetRegistryModule& RegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> Assets;
	RegistryModule.Get().GetAssetsByPath(UMT2PathSettings::Path(TEXT("ImportDestinationRoot")), Assets, true);

	TArray<FAssetData> ImportedAssets;
	for (const FAssetData& Asset : Assets)
	{
		if (IsImportedAsset(Asset))
		{
			ImportedAssets.Add(Asset);
		}
	}

	FScopedSlowTask Progress(static_cast<float>(FMath::Max(ImportedAssets.Num(), 1)), NSLOCTEXT("MT2AssetOptimizer", "Progress", "Optimizing imported MT2 assets..."));
	Progress.MakeDialog(true);
	TArray<UPackage*> DirtyPackages;
	for (const FAssetData& Asset : ImportedAssets)
	{
		Progress.EnterProgressFrame(1.0f, FText::FromName(Asset.AssetName));
		if (Progress.ShouldCancel())
		{
			OutResult.Warnings.Add(TEXT("Optimization stopped by user."));
			break;
		}

		UObject* Object = Asset.GetAsset();
		bool bChanged = false;
		if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Object))
		{
			bChanged = ConfigureStaticMesh(StaticMesh);
			OutResult.StaticMeshes += bChanged ? 1 : 0;
		}
		else if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Object))
		{
			bChanged = ConfigureSkeletalMesh(SkeletalMesh);
			OutResult.SkeletalMeshes += bChanged ? 1 : 0;
			if (!bChanged)
			{
				OutResult.Warnings.Add(FString::Printf(TEXT("Could not generate skeletal LODs: %s"), *Asset.GetObjectPathString()));
			}
		}
		else if (UTexture2D* Texture = Cast<UTexture2D>(Object))
		{
			bChanged = ConfigureTexture(Texture);
			OutResult.Textures += bChanged ? 1 : 0;
		}

		if (bChanged && Object->GetOutermost())
		{
			DirtyPackages.AddUnique(Object->GetOutermost());
		}
	}

	if (GEditor)
	{
		if (UWorld* World = GEditor->GetEditorWorldContext().World())
		{
			for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
			{
				UPrimitiveComponent* Component = *It;
				if (!Component || Component->GetWorld() != World || !Component->GetOwner())
				{
					continue;
				}
				bool bIsMapObject = Component->GetOwner()->ActorHasTag(TEXT("MT2UE_MapObject"));
				for (const FName Tag : Component->GetOwner()->Tags)
				{
					bIsMapObject |= Tag.ToString().StartsWith(TEXT("MT2UE_MapObject:"));
				}
				if (!bIsMapObject)
				{
					continue;
				}
				Component->Modify();
				Component->SetCullDistance(ComputeMapObjectCullDistance(Component->Bounds));
				OutResult.WorldComponents++;
			}
			World->MarkPackageDirty();
			DirtyPackages.AddUnique(World->GetOutermost());
		}
	}

	if (DirtyPackages.Num() > 0)
	{
		FEditorFileUtils::PromptForCheckoutAndSave(DirtyPackages, false, false);
	}
	return true;
}
