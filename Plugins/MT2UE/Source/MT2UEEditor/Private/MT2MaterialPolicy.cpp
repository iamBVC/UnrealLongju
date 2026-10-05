/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "MT2MaterialPolicy.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Materials/Material.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	bool SaveMaterialPackage(UMaterial* Material)
	{
		UPackage* Package = Material ? Material->GetOutermost() : nullptr;
		if (!Package) return false;

		const FString Filename = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		return UPackage::SavePackage(Package, Material, *Filename, Args);
	}
}

bool FMT2MaterialPolicy::IsLandscapeMaterial(const UMaterial* Material)
{
	if (!Material) return false;

	// MT2 landscape materials are generated as M_<MapName>_Landscape. Restrict the exception to
	// /Game materials so unrelated engine/plugin assets with a similar name are never modified.
	return Material->GetOutermost()->GetName().StartsWith(TEXT("/Game/")) &&
		Material->GetName().EndsWith(TEXT("_Landscape"), ESearchCase::IgnoreCase);
}

bool FMT2MaterialPolicy::Apply(UMaterial* Material)
{
	if (!Material || IsLandscapeMaterial(Material) || Material->TwoSided) return false;
	Material->Modify();
	Material->TwoSided = true;
	Material->PostEditChange();
	Material->MarkPackageDirty();
	return true;
}

bool FMT2MaterialPolicy::ApplyToProject(FMT2MaterialPolicyResult& OutResult, bool bSavePackages)
{
	OutResult = {};
	FAssetRegistryModule& AssetRegistry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	AssetRegistry.Get().SearchAllAssets(true);

	TArray<FAssetData> Assets;
	AssetRegistry.Get().GetAssetsByClass(UMaterial::StaticClass()->GetClassPathName(), Assets, true);
	for (const FAssetData& AssetData : Assets)
	{
		if (!AssetData.PackageName.ToString().StartsWith(TEXT("/Game/"))) continue;
		UMaterial* Material = Cast<UMaterial>(AssetData.GetAsset());
		if (!Material) continue;
		if (IsLandscapeMaterial(Material))
		{
			OutResult.LandscapeMaterialsSkipped++;
			continue;
		}
		if (!Apply(Material))
		{
			OutResult.MaterialsAlreadyCompliant++;
			continue;
		}
		OutResult.MaterialsChanged++;
		if (bSavePackages && !SaveMaterialPackage(Material))
		{
			OutResult.SaveFailures++;
		}
	}
	return OutResult.SaveFailures == 0;
}
