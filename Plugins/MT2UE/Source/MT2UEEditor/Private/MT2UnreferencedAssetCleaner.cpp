/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "MT2UnreferencedAssetCleaner.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/AssetManager.h"
#include "Engine/World.h"
#include "Misc/MessageDialog.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"

#define LOCTEXT_NAMESPACE "FMT2UnreferencedAssetCleaner"

void FMT2UnreferencedAssetCleaner::DeleteFromFolders(const TArray<FString>& FolderPaths)
{
	if (FolderPaths.IsEmpty())
	{
		return;
	}

	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	AssetRegistry.WaitForCompletion();

	TMap<FName, FAssetData> AssetsByObjectPath;
	for (const FString& FolderPath : FolderPaths)
	{
		TArray<FAssetData> FolderAssets;
		AssetRegistry.GetAssetsByPath(FName(*FolderPath), FolderAssets, true, true);
		for (const FAssetData& Asset : FolderAssets)
		{
			AssetsByObjectPath.FindOrAdd(FName(*Asset.GetSoftObjectPath().ToString())) = Asset;
		}
	}

	if (AssetsByObjectPath.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("NoAssetsInFolder", "The selected folder contains no assets."));
		return;
	}

	TArray<FAssetData> AssetsToDelete;
	AssetsToDelete.Reserve(AssetsByObjectPath.Num());
	FScopedSlowTask Progress(
		static_cast<float>(AssetsByObjectPath.Num()),
		LOCTEXT("ScanningAssets", "Scanning assets and references..."));
	Progress.MakeDialog(true);

	for (const TPair<FName, FAssetData>& Pair : AssetsByObjectPath)
	{
		if (Progress.ShouldCancel())
		{
			return;
		}
		Progress.EnterProgressFrame(1.0f, FText::FromName(Pair.Value.AssetName));

		const FAssetData& Asset = Pair.Value;
		// Maps are editor entry points and may intentionally have no package referencer.
		if (Asset.AssetClassPath == UWorld::StaticClass()->GetClassPathName())
		{
			continue;
		}
		// Primary assets are runtime roots selected by Asset Manager rules rather than package references.
		if (UAssetManager::IsInitialized() &&
			UAssetManager::Get().GetPrimaryAssetIdForData(Asset).IsValid())
		{
			continue;
		}

		TArray<FName> Referencers;
		AssetRegistry.GetReferencers(
			Asset.PackageName, Referencers, UE::AssetRegistry::EDependencyCategory::Package);
		const bool bHasOtherPackageReferencer = Referencers.ContainsByPredicate(
			[PackageName = Asset.PackageName](FName Referencer)
			{
				return Referencer != PackageName;
			});
		if (!bHasOtherPackageReferencer)
		{
			AssetsToDelete.Add(Asset);
		}
	}

	if (AssetsToDelete.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("NoUnreferencedAssets", "No safely deletable unreferenced assets were found."));
		return;
	}

	// Keep Unreal's standard confirmation and memory-reference validation as the final safety gate.
	const int32 DeletedCount = ObjectTools::DeleteAssets(AssetsToDelete, true);
	if (DeletedCount > 0)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
			LOCTEXT("DeletedAssets", "Deleted {0} unreferenced asset(s)."),
			FText::AsNumber(DeletedCount)));
	}
}

#undef LOCTEXT_NAMESPACE
