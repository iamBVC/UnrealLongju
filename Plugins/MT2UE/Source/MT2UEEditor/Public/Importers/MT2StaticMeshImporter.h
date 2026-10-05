/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "API/MT2ImporterInterface.h"
#include "Importers/MT2GrannyMeshConverter.h"

class UMaterial;

class FMT2StaticMeshImporter : public FMT2ImporterBase
{
public:
	FMT2StaticMeshImporter();

	virtual bool Discover(const FMT2ImportContext& Context, const FMT2AssetScanResult& ScanResult, FMT2ImportDiscovery& OutDiscovery) const override;
	virtual bool Import(const FMT2ImportRequest& Request, FMT2ImportResult& OutResult) override;

	static FString BuildDestinationPath(const FMT2ImportContext& Context, const FMT2AssetRecord& Record);
	static FString BuildObjectPath(const FMT2ImportContext& Context, const FMT2AssetRecord& Record);
	static FString BuildSharedMaterialFolder(const FMT2ImportContext& Context);
	static FString BuildSharedMaterialObjectPath(const FMT2ImportContext& Context, const FMT2GrannyMaterialSlot& Slot, int32 SlotIndex, const FString& DiffuseTextureObjectPath, const FString& OpacityTextureObjectPath);
	static FString BuildConvertedMeshPath(const FMT2ImportContext& Context, const FMT2AssetRecord& Record);
	static FString GetWorkingRoot(const FMT2ImportContext& Context);
	static bool ConvertGrannyToMeshSource(const FMT2ImportContext& Context, const FMT2AssetRecord& Record, FString& OutMeshPath, FMT2ImportResult& OutResult);
	static void ImportMaterialTexturesForConvertedMesh(const FMT2ImportContext& Context, const FString& MeshPath, FMT2ImportResult& OutResult);
	static void ImportMaterialTexturesForSlots(const FMT2ImportContext& Context, const FMT2AssetRecord& Record, const TArray<FMT2GrannyMaterialSlot>& MaterialSlots, FMT2ImportResult& OutResult);
	static TArray<FString> ReadMaterialTextureReferences(const FString& MeshPath, FMT2ImportResult& OutResult);
	static UMaterial* CreateOrUpdateSharedMaterial(const FMT2ImportContext& Context, const FMT2AssetRecord& Record, const FMT2GrannyMaterialSlot& Slot, int32 SlotIndex, FMT2ImportResult& OutResult, FString* OutMaterialObjectPath = nullptr);
	static void CreateAndAssignMaterials(const FMT2ImportContext& Context, const FMT2AssetRecord& Record, const TArray<FMT2GrannyMaterialSlot>& MaterialSlots, FMT2ImportResult& OutResult);
	static TSet<FString> CollectMaterialObjectPathsInFolder(const FString& PackageFolder);
	static void DeleteUnreferencedNewMaterials(const FMT2ImportContext& Context, const FMT2AssetRecord& Record, const TSet<FString>& PreImportMaterialObjectPaths, FMT2ImportResult& OutResult);
};
