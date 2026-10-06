/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Importers/MT2SkeletalMeshImporter.h"
#include "Config/MT2PathSettings.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Importers/MT2GrannyMeshConverter.h"
#include "Importers/MT2MeshUsageClassifier.h"
#include "Importers/MT2StaticMeshImporter.h"
#include "Importers/MT2TextureImporter.h"
#include "InterchangeGenericAssetsPipeline.h"
#include "InterchangeGenericAssetsPipelineSharedSettings.h"
#include "InterchangeGenericMeshPipeline.h"
#include "InterchangeManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "MT2AssetOptimizer.h"
#include "UObject/Package.h"

namespace
{
	bool IsGeneratedPerMeshSkeleton(const USkeletalMesh* SkeletalMesh, const USkeleton* Skeleton)
	{
		return SkeletalMesh && Skeleton &&
			Skeleton->GetName().Equals(SkeletalMesh->GetName() + TEXT("_Skeleton"));
	}

	bool EnsureSkeletonBoneTree(USkeletalMesh* SkeletalMesh, bool bAllowGeneratedSkeletonRebuild)
	{
		if (!SkeletalMesh)
		{
			return false;
		}

		if (USkeleton* Skeleton = SkeletalMesh->GetSkeleton())
		{
			Skeleton->Modify();
			if (!Skeleton->IsCompatibleMesh(SkeletalMesh) &&
				(!bAllowGeneratedSkeletonRebuild ||
				 !IsGeneratedPerMeshSkeleton(SkeletalMesh, Skeleton) ||
				 !Skeleton->RecreateBoneTree(SkeletalMesh)))
			{
				return false;
			}
			if (!Skeleton->MergeAllBonesToBoneTree(SkeletalMesh, false))
			{
				return false;
			}
			Skeleton->RebuildLinkup(SkeletalMesh);
			Skeleton->PostEditChange();
			return true;
		}

		return false;
	}

	FString BuildImporterSkeletalMeshObjectPath(const FMT2ImportContext& Context, const FMT2AssetRecord& Record)
	{
		const FString PackagePath = FMT2AssetScanner::BuildContentPackagePath(Context.DestinationRoot, Record);
		FString AssetName = FMT2AssetScanner::SanitizePackagePathSegment(FPaths::GetBaseFilename(Record.ContentPath));
		if (!AssetName.StartsWith(TEXT("SK_"), ESearchCase::IgnoreCase))
		{
			AssetName = TEXT("SK_") + AssetName;
		}
		return PackagePath / AssetName + TEXT(".") + AssetName;
	}

	TArray<FString> BuildImportedSkeletalMeshObjectPathCandidates(const FMT2ImportContext& Context, const FMT2AssetRecord& Record)
	{
		const FString BasePackagePath = FMT2AssetScanner::BuildContentPackagePath(Context.DestinationRoot, Record);
		FString BaseName = FMT2AssetScanner::SanitizePackagePathSegment(FPaths::GetBaseFilename(Record.ContentPath));
		FString AssetName = BaseName;
		if (!AssetName.StartsWith(TEXT("SK_"), ESearchCase::IgnoreCase))
		{
			AssetName = TEXT("SK_") + AssetName;
		}

		TArray<FString> CandidateObjectPaths;
		CandidateObjectPaths.Add(BasePackagePath / AssetName + TEXT(".") + AssetName);
		CandidateObjectPaths.Add(BasePackagePath / UMT2PathSettings::Path(TEXT("Part_SkeletalMeshes")) / AssetName + TEXT(".") + AssetName);
		CandidateObjectPaths.Add(BasePackagePath / BaseName / UMT2PathSettings::Path(TEXT("Part_SkeletalMeshes")) / AssetName + TEXT(".") + AssetName);
		return CandidateObjectPaths;
	}

	USkeletalMesh* LoadImportedSkeletalMesh(const FMT2ImportContext& Context, const FMT2AssetRecord& Record, FString& OutObjectPath)
	{
		const FString BasePackagePath = FMT2AssetScanner::BuildContentPackagePath(Context.DestinationRoot, Record);
		FString AssetName = FMT2AssetScanner::SanitizePackagePathSegment(FPaths::GetBaseFilename(Record.ContentPath));
		if (!AssetName.StartsWith(TEXT("SK_"), ESearchCase::IgnoreCase))
		{
			AssetName = TEXT("SK_") + AssetName;
		}
		const FAssetRegistryModule& AssetRegistryModule =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

		for (const FString& CandidateObjectPath : BuildImportedSkeletalMeshObjectPathCandidates(Context, Record))
		{
			const FAssetData CandidateAsset = AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(CandidateObjectPath));
			if (!CandidateAsset.IsValid())
			{
				continue;
			}
			if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(CandidateAsset.GetAsset()))
			{
				OutObjectPath = CandidateObjectPath;
				return SkeletalMesh;
			}
		}

		TArray<FAssetData> Assets;
		AssetRegistryModule.Get().GetAssetsByPath(FName(*BasePackagePath), Assets, true);
		for (const FAssetData& Asset : Assets)
		{
			UObject* LoadedAsset = Asset.GetAsset();
			if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(LoadedAsset))
			{
				if (SkeletalMesh->GetName().Equals(AssetName, ESearchCase::IgnoreCase) || Assets.Num() == 1)
				{
					OutObjectPath = Asset.GetSoftObjectPath().ToString();
					return SkeletalMesh;
				}
			}
		}

		return nullptr;
	}

	FString BuildConvertedGltfPath(const FMT2ImportContext& Context, const FMT2AssetRecord& Record)
	{
		TArray<FString> Parts;
		Record.ContentPath.ParseIntoArray(Parts, TEXT("/"), true);

		FString Result = FMT2StaticMeshImporter::GetWorkingRoot(Context) / UMT2PathSettings::Path(TEXT("Part_ConvertedSkeletal")) / FMT2AssetScanner::SanitizePackagePathSegment(Record.PackName);
		for (int32 Index = 0; Index < Parts.Num(); ++Index)
		{
			if (Index == Parts.Num() - 1)
			{
				Result /= FMT2AssetScanner::SanitizePackagePathSegment(FPaths::GetBaseFilename(Parts[Index])) + TEXT(".gltf");
			}
			else
			{
				Result /= FMT2AssetScanner::SanitizePackagePathSegment(Parts[Index]);
			}
		}
		return Result;
	}

	void CreateAndAssignSkeletalMaterials(const FMT2ImportContext& Context, const FMT2AssetRecord& Record, const TArray<FMT2GrannyMaterialSlot>& MaterialSlots, FMT2ImportResult& OutResult)
	{
		if (MaterialSlots.Num() == 0)
		{
			OutResult.AddWarning(FString::Printf(TEXT("No Granny material slots found for skeletal mesh: %s."), *Record.VirtualPath), Record.AbsolutePath);
			return;
		}

		FString LoadedSkeletalMeshObjectPath;
		USkeletalMesh* SkeletalMesh = LoadImportedSkeletalMesh(Context, Record, LoadedSkeletalMeshObjectPath);
		if (!SkeletalMesh)
		{
			OutResult.AddWarning(FString::Printf(TEXT("Imported skeletal mesh could not be loaded for material assignment under: %s"), *FMT2AssetScanner::BuildContentPackagePath(Context.DestinationRoot, Record)), Record.AbsolutePath);
			return;
		}
		if (Context.bEnableDebugLogs)
		{
			OutResult.AddInfo(FString::Printf(TEXT("[Debug][SkeletalMaterial] Loaded skeletal mesh for assignment: %s"), *LoadedSkeletalMeshObjectPath), Record.AbsolutePath);
		}

		TArray<UMaterialInterface*> CreatedMaterials;
		CreatedMaterials.Reserve(MaterialSlots.Num());

		for (int32 SlotIndex = 0; SlotIndex < MaterialSlots.Num(); ++SlotIndex)
		{
			FString MaterialObjectPath;
			UMaterial* Material = FMT2StaticMeshImporter::CreateOrUpdateSharedMaterial(
				Context,
				Record,
				MaterialSlots[SlotIndex],
				SlotIndex,
				OutResult,
				&MaterialObjectPath);
			if (Material)
			{
				Material->Modify();
				Material->bUsedWithSkeletalMesh = true;
				Material->PostEditChange();
				Material->MarkPackageDirty();
				if (Context.bEnableDebugLogs)
				{
					OutResult.AddInfo(FString::Printf(TEXT("[Debug][SkeletalMaterial] Slot=%s Material=%s"),
						*MaterialSlots[SlotIndex].SlotName,
						*MaterialObjectPath),
						Record.AbsolutePath);
				}
			}
			CreatedMaterials.Add(Material);
		}
		TArray<FSkeletalMaterial>& SkeletalMaterials = SkeletalMesh->GetMaterials();
		for (int32 SlotIndex = 0; SlotIndex < CreatedMaterials.Num(); ++SlotIndex)
		{
			UMaterialInterface* Material = CreatedMaterials[SlotIndex];
			if (!Material)
			{
				continue;
			}

			const FName GrannySlotName(*MaterialSlots[SlotIndex].SlotName);
			const FName ImportedSlotName(*(MaterialSlots[SlotIndex].ImportedSlotName.IsEmpty() ? MaterialSlots[SlotIndex].SlotName : MaterialSlots[SlotIndex].ImportedSlotName));
			int32 SkeletalMaterialIndex = INDEX_NONE;
			for (int32 Index = 0; Index < SkeletalMaterials.Num(); ++Index)
			{
				if (SkeletalMaterials[Index].MaterialSlotName == GrannySlotName ||
					SkeletalMaterials[Index].ImportedMaterialSlotName == GrannySlotName ||
					SkeletalMaterials[Index].MaterialSlotName == ImportedSlotName ||
					SkeletalMaterials[Index].ImportedMaterialSlotName == ImportedSlotName)
				{
					SkeletalMaterialIndex = Index;
					break;
				}
			}

			if (SkeletalMaterialIndex == INDEX_NONE && SkeletalMaterials.IsValidIndex(SlotIndex))
			{
				SkeletalMaterialIndex = SlotIndex;
			}

			if (SkeletalMaterialIndex != INDEX_NONE)
			{
				SkeletalMaterials[SkeletalMaterialIndex].MaterialInterface = Material;
				SkeletalMaterials[SkeletalMaterialIndex].MaterialSlotName = GrannySlotName;
				SkeletalMaterials[SkeletalMaterialIndex].ImportedMaterialSlotName = ImportedSlotName;
			}
			else
			{
				SkeletalMaterials.Add(FSkeletalMaterial(Material, GrannySlotName, ImportedSlotName));
			}
		}

		SkeletalMesh->PostEditChange();
		SkeletalMesh->MarkPackageDirty();
		OutResult.AddInfo(FString::Printf(TEXT("Created/assigned %d skeletal material(s) for %s."), CreatedMaterials.Num(), *Record.VirtualPath), Record.AbsolutePath);
	}

	struct FPendingSkeletalMaterialAssignment
	{
		FMT2AssetRecord Record;
		TArray<FMT2GrannyMaterialSlot> MaterialSlots;
	};
}

USkeletalMesh* FMT2SkeletalMeshImporter::LoadImportedMesh(const FMT2ImportContext& Context, const FMT2AssetRecord& Record, FString* OutObjectPath)
{
	FString LoadedObjectPath;
	USkeletalMesh* SkeletalMesh = LoadImportedSkeletalMesh(Context, Record, LoadedObjectPath);
	if (OutObjectPath)
	{
		*OutObjectPath = MoveTemp(LoadedObjectPath);
	}
	return SkeletalMesh;
}

FMT2SkeletalMeshImporter::FMT2SkeletalMeshImporter()
	: FMT2ImporterBase(EMT2ImportDomain::SkeletalMeshes, TEXT("SkeletalMeshImporter"))
{
}

bool FMT2SkeletalMeshImporter::Discover(const FMT2ImportContext& Context, const FMT2AssetScanResult& ScanResult, FMT2ImportDiscovery& OutDiscovery) const
{
	OutDiscovery.Domain = GetDomain();
	const FMT2MeshUsageClassifier UsageClassifier(&ScanResult);
	for (const FString& Warning : UsageClassifier.GetWarnings())
	{
		FMT2ImportMessage& Message = OutDiscovery.Messages.AddDefaulted_GetRef();
		Message.Severity = EMT2ImportSeverity::Warning;
		Message.Text = Warning;
	}
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	for (const FMT2AssetRecord& Record : ScanResult.GetRecordsByKind(EMT2AssetKind::Granny))
	{
		bool bAlreadyImported = false;
		if (!Context.bReplaceExisting)
		{
			for (const FString& CandidateObjectPath : BuildImportedSkeletalMeshObjectPathCandidates(Context, Record))
			{
				if (AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(CandidateObjectPath)).IsValid())
				{
					bAlreadyImported = true;
					break;
				}
			}
		}
		if (bAlreadyImported)
		{
			continue;
		}

		FMT2GrannyFileInspection Inspection;
		FString Error;
		if (FMT2GrannyMeshConverter::InspectGrannyFile(Record.AbsolutePath, Inspection, Error) &&
			Inspection.Type == EMT2GrannyFileType::SkeletalMesh &&
			UsageClassifier.RequiresSkeletalMesh(Record, Inspection))
		{
			OutDiscovery.AssetRecords.Add(Record);
		}
	}

	OutDiscovery.ItemsDiscovered = OutDiscovery.AssetRecords.Num();
	return true;
}

bool FMT2SkeletalMeshImporter::Import(const FMT2ImportRequest& Request, FMT2ImportResult& OutResult)
{
	if (!CanImport(Request, OutResult))
	{
		return false;
	}

	if (Request.Selection.AssetRecords.Num() == 0)
	{
		OutResult.AddWarning(TEXT("No skeletal mesh records were selected."));
		OutResult.bSucceeded = true;
		return true;
	}
	const FMT2MeshUsageClassifier UsageClassifier(Request.Context.ScanResult);
	if (!UsageClassifier.IsReady())
	{
		for (const FString& Warning : UsageClassifier.GetWarnings())
		{
			OutResult.AddWarning(Warning);
		}
	}

	TArray<FMT2AssetRecord> OrderedRecords = Request.Selection.AssetRecords;
	OrderedRecords.StableSort([&UsageClassifier](const FMT2AssetRecord& Left, const FMT2AssetRecord& Right)
	{
		const FString LeftCanonical = UsageClassifier.GetCanonicalModelPath(Left);
		const FString RightCanonical = UsageClassifier.GetCanonicalModelPath(Right);
		const bool bLeftIsCanonical = !LeftCanonical.IsEmpty() &&
			FPaths::IsSamePath(Left.AbsolutePath, LeftCanonical);
		const bool bRightIsCanonical = !RightCanonical.IsEmpty() &&
			FPaths::IsSamePath(Right.AbsolutePath, RightCanonical);
		return bLeftIsCanonical && !bRightIsCanonical;
	});

	const int32 MaxItems = Request.MaxItems > 0 ? Request.MaxItems : Request.Selection.AssetRecords.Num();
	int32 ConsideredCount = 0;
	int32 ImportedTaskCount = 0;
	FScopedSlowTask MeshProgress(static_cast<float>(MaxItems), NSLOCTEXT("FMT2SkeletalMeshImporter", "ImportSkeletalMeshesProgress", "Importing skeletal meshes..."));

	for (const FMT2AssetRecord& Record : OrderedRecords)
	{
		if (ConsideredCount >= MaxItems)
		{
			break;
		}

		MeshProgress.EnterProgressFrame(1.0f, FText::Format(
			NSLOCTEXT("FMT2SkeletalMeshImporter", "ImportSkeletalMeshProgressFormat", "Importing skeletal mesh {0} of {1}: {2}"),
			FText::AsNumber(ConsideredCount + 1),
			FText::AsNumber(MaxItems),
			FText::FromString(Record.VirtualPath)));
		if (Request.Context.IsStopRequested() || MeshProgress.ShouldCancel())
		{
			OutResult.AddWarning(TEXT("Skeletal mesh import stopped by user."));
			break;
		}

		ConsideredCount++;
		OutResult.ItemsDiscovered++;

		FMT2GrannyFileInspection Inspection;
		FString InspectionError;
		if (Record.Kind != EMT2AssetKind::Granny ||
			!FMT2GrannyMeshConverter::InspectGrannyFile(Record.AbsolutePath, Inspection, InspectionError) ||
			Inspection.Type != EMT2GrannyFileType::SkeletalMesh ||
			!UsageClassifier.RequiresSkeletalMesh(Record, Inspection))
		{
			OutResult.ItemsSkipped++;
			OutResult.AddWarning(FString::Printf(TEXT("Skipped non-skeletal Granny mesh: %s"), *Record.VirtualPath), Record.AbsolutePath);
			continue;
		}

		const FString GltfPath = BuildConvertedGltfPath(Request.Context, Record);
		const FString ObjectPath = BuildImporterSkeletalMeshObjectPath(Request.Context, Record);
		OutResult.CreatedFiles.Add(GltfPath);
		OutResult.CreatedPackages.Add(ObjectPath);

		FString ExistingObjectPath;
		USkeletalMesh* ExistingMesh = LoadImportedMesh(Request.Context, Record, &ExistingObjectPath);
		if (ExistingMesh && !Request.Context.bReplaceExisting)
		{
			if (!Request.Context.bDryRun)
			{
				EnsureSkeletonBoneTree(ExistingMesh, true);
				TArray<FMT2GrannyMaterialSlot> ExistingMaterialSlots;
				FString MaterialError;
				if (FMT2GrannyMeshConverter::ExtractMaterialSlots(Record.AbsolutePath, ExistingMaterialSlots, MaterialError))
				{
					if (Request.Context.bImportStaticObjectTextures)
					{
						FMT2StaticMeshImporter::ImportMaterialTexturesForSlots(Request.Context, Record, ExistingMaterialSlots, OutResult);
					}
					CreateAndAssignSkeletalMaterials(Request.Context, Record, ExistingMaterialSlots, OutResult);
					OutResult.AddInfo(FString::Printf(TEXT("Refreshed materials without reimporting skeletal mesh: %s"), *ExistingObjectPath), Record.AbsolutePath);
				}
				else
				{
					OutResult.AddWarning(FString::Printf(TEXT("Could not refresh Granny materials for %s: %s"), *Record.VirtualPath, *MaterialError), Record.AbsolutePath);
				}
			}
			OutResult.ItemsSkipped++;
			OutResult.AddInfo(FString::Printf(TEXT("Skipped already imported skeletal mesh: %s"), *ExistingObjectPath), Record.AbsolutePath);
			continue;
		}

		if (Request.Context.bDryRun)
		{
			OutResult.ItemsSkipped++;
			continue;
		}

		TArray<FMT2GrannyMaterialSlot> MaterialSlots;
		FString Error;
		const FString CanonicalModelPath = UsageClassifier.GetCanonicalModelPath(Record);
		const bool bUsesOwnSkeleton = CanonicalModelPath.IsEmpty() ||
			FPaths::IsSamePath(Record.AbsolutePath, CanonicalModelPath);
		if (ExistingMesh && bUsesOwnSkeleton &&
			!EnsureSkeletonBoneTree(ExistingMesh, true))
		{
			OutResult.AddError(FString::Printf(
				TEXT("Existing generated skeleton is incompatible with skeletal mesh %s and could not be repaired."),
				*Record.VirtualPath), Record.AbsolutePath);
			OutResult.ItemsSkipped++;
			continue;
		}
		if (!FMT2GrannyMeshConverter::ConvertGrannyToGltf(
			Record.AbsolutePath,
			GltfPath,
			Error,
			&MaterialSlots,
			Request.Context.MeshUVTransform,
			CanonicalModelPath))
		{
			OutResult.ItemsSkipped++;
			OutResult.AddError(FString::Printf(TEXT("Granny skeletal import failed for %s: %s"), *Record.VirtualPath, *Error), Record.AbsolutePath);
			continue;
		}

		if (Request.Context.bEnableDebugLogs)
		{
			OutResult.AddInfo(FString::Printf(TEXT("[Debug][SkeletalImport] %s converted to %s with %d material slot(s)."), *Record.VirtualPath, *GltfPath, MaterialSlots.Num()), Record.AbsolutePath);
			for (int32 SlotIndex = 0; SlotIndex < MaterialSlots.Num(); ++SlotIndex)
			{
				const FMT2GrannyMaterialSlot& Slot = MaterialSlots[SlotIndex];
				OutResult.AddInfo(FString::Printf(TEXT("[Debug][SkeletalImport] Slot[%d] Name='%s' Diffuse='%s' Opacity='%s' TwoSided=%s"),
					SlotIndex,
					*Slot.SlotName,
					*Slot.DiffuseTextureReference,
					*Slot.OpacityTextureReference,
					Slot.bTwoSided ? TEXT("true") : TEXT("false")),
					Record.AbsolutePath);
			}
		}

		if (Request.Context.bImportStaticObjectTextures)
		{
			FMT2StaticMeshImporter::ImportMaterialTexturesForSlots(Request.Context, Record, MaterialSlots, OutResult);
		}

		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->AddToRoot();
		Task->Filename = GltfPath;
		Task->DestinationPath = ExistingMesh
			? FPackageName::GetLongPackagePath(FSoftObjectPath(ExistingObjectPath).GetLongPackageName())
			: FMT2AssetScanner::BuildContentPackagePath(Request.Context.DestinationRoot, Record);
		Task->DestinationName = ExistingMesh
			? ExistingMesh->GetName()
			: FPackageName::ObjectPathToObjectName(ObjectPath);
		Task->bAutomated = true;
		Task->bSave = false;
		Task->bReplaceExisting = Request.Context.bReplaceExisting;
		Task->Factory = nullptr;

		UInterchangeGenericAssetsPipeline* Pipeline = NewObject<UInterchangeGenericAssetsPipeline>(Task);
		// The converter now bakes the correct orientation into the glTF (source-X mirror, same
		// convention as the map import). No import-time rotation: any offset on the root would
		// also land in the skeleton's root bone, but animation tracks replace that local during
		// playback and would wipe the offset for as long as a clip plays.
		Pipeline->ImportOffsetRotation = FRotator::ZeroRotator;
		Pipeline->bUseSourceNameForAsset = false;
		Pipeline->AssetName = Task->DestinationName;
		if (Pipeline->MeshPipeline)
		{
			Pipeline->MeshPipeline->bCreatePhysicsAsset = false;
		}

		bool bCanonicalSkeletonResolved = true;
		if (!CanonicalModelPath.IsEmpty() &&
			!FPaths::IsSamePath(Record.AbsolutePath, CanonicalModelPath) &&
			Request.Context.ScanResult && Pipeline->CommonSkeletalMeshesAndAnimationsProperties)
		{
			const FMT2AssetRecord* CanonicalRecord = Request.Context.ScanResult->Records.FindByPredicate(
				[&CanonicalModelPath](const FMT2AssetRecord& Candidate)
				{
					return Candidate.Kind == EMT2AssetKind::Granny &&
						FPaths::IsSamePath(Candidate.AbsolutePath, CanonicalModelPath);
				});
			USkeletalMesh* CanonicalMesh = CanonicalRecord
				? LoadImportedMesh(Request.Context, *CanonicalRecord)
				: nullptr;
			if (CanonicalMesh && CanonicalMesh->GetSkeleton())
			{
				Pipeline->CommonSkeletalMeshesAndAnimationsProperties->Skeleton = CanonicalMesh->GetSkeleton();
			}
			else
			{
				bCanonicalSkeletonResolved = false;
				OutResult.AddError(FString::Printf(
					TEXT("Canonical skeleton was not available before importing variant %s (%s)."),
					*Record.VirtualPath, *CanonicalModelPath), Record.AbsolutePath);
			}
		}
		if (!bCanonicalSkeletonResolved)
		{
			Task->RemoveFromRoot();
			OutResult.ItemsSkipped++;
			continue;
		}

		UInterchangePipelineStackOverride* PipelineOverride = NewObject<UInterchangePipelineStackOverride>(Task);
		PipelineOverride->AddPipeline(Pipeline);
		Task->Options = PipelineOverride;
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
		TArray<UAssetImportTask*> SingleTask;
		SingleTask.Add(Task);
		AssetToolsModule.Get().ImportAssetTasks(SingleTask);

		Task->RemoveFromRoot();

		CreateAndAssignSkeletalMaterials(Request.Context, Record, MaterialSlots, OutResult);
		if (USkeletalMesh* ImportedMesh = LoadImportedMesh(Request.Context, Record))
		{
			if (!EnsureSkeletonBoneTree(ImportedMesh, bUsesOwnSkeleton))
			{
				OutResult.AddError(FString::Printf(
					TEXT("Imported skeletal mesh %s is incompatible with its assigned skeleton."),
					*Record.VirtualPath), Record.AbsolutePath);
			}
			FMT2AssetOptimizer::ConfigureSkeletalMesh(ImportedMesh);
		}
		ImportedTaskCount++;
	}

	OutResult.ItemsImported = ImportedTaskCount;
	OutResult.AddInfo(FString::Printf(TEXT("Imported %d skeletal mesh glTF task(s) serially."), ImportedTaskCount));
	OutResult.bSucceeded = !OutResult.HasErrors();
	return OutResult.bSucceeded;
}
