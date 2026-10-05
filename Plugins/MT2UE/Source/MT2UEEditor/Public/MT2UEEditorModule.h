/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class UBlueprint;
struct FAssetData;

class FMT2UEEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterMenus();
	void RegisterContentBrowserMenus();
	void AddMenuEntry(class FMenuBuilder& MenuBuilder);
	void ReimportSkills();
	void ImportQuests();
	void GeneratePlayerAnimationBlueprints();
	void RebuildVnumRegistry();
	void OptimizeImportedAssets();
	void RepairImportedMapActors();
	void RegisterVnumRegistryTracking();
	void UnregisterVnumRegistryTracking();
	void ScheduleVnumRegistryRebuild();
	void HandleTrackedAssetChanged(const FAssetData& AssetData);
	void HandleTrackedAssetRenamed(const FAssetData& AssetData, const FString& OldObjectPath);
	void HandleBlueprintPreCompile(UBlueprint* Blueprint);
	void HandleBlueprintCompiled();
	bool TickVnumRegistry(float DeltaSeconds);
	bool IsTrackedBlueprint(const UBlueprint* Blueprint) const;
	bool IsTrackedAsset(const FAssetData& AssetData) const;
	TSharedRef<class SDockTab> SpawnImporterTab(const class FSpawnTabArgs& SpawnTabArgs);

private:
	TSharedPtr<class FExtender> MenuExtender;
	FDelegateHandle AssetAddedHandle;
	FDelegateHandle AssetUpdatedHandle;
	FDelegateHandle AssetRemovedHandle;
	FDelegateHandle AssetRenamedHandle;
	FDelegateHandle BlueprintPreCompileHandle;
	FDelegateHandle BlueprintCompiledHandle;
	FTSTicker::FDelegateHandle RegistryTickerHandle;
	double RegistryRebuildAtSeconds = 0.0;
	bool bRegistryRebuildPending = false;
	bool bTrackedBlueprintCompiling = false;
};
