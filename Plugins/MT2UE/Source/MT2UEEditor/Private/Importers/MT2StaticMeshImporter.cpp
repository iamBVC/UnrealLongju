/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Importers/MT2StaticMeshImporter.h"
#include "Config/MT2PathSettings.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Factories/FbxImportUI.h"
#include "Factories/FbxStaticMeshImportData.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
#include "Importers/MT2GrannyMeshConverter.h"
#include "Importers/MT2MeshUsageClassifier.h"
#include "Importers/MT2SkeletalMeshImporter.h"
#include "Importers/MT2SpeedTreeConverter.h"
#include "Importers/MT2TextureImporter.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialInstance.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "ObjectTools.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "MT2AssetOptimizer.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"

namespace
{
	constexpr const TCHAR* StaticMeshConversionVersion =
		TEXT("MT2UE_STATIC_CONVERSION_V4_SKELETAL_ATTACHMENT_BASIS");
	constexpr const TCHAR* StaticMeshConversionVersionKey =
		TEXT("MT2UEStaticMeshConversionVersion");

	UTexture2D* ResolveMaterialTexture(const FMT2ImportContext& Context, const FMT2AssetRecord& Record, const FString& Reference, FString& OutObjectPath)
	{
		OutObjectPath.Reset();
		if (!Context.ScanResult || Reference.IsEmpty())
		{
			return nullptr;
		}

		if (const FMT2AssetRecord* TextureRecord = FMT2TextureImporter::FindRecordForReference(*Context.ScanResult, Reference, Record))
		{
			OutObjectPath = FMT2TextureImporter::BuildObjectPath(Context, *TextureRecord);
			return LoadObject<UTexture2D>(nullptr, *OutObjectPath);
		}
		return nullptr;
	}

	UMaterialExpressionTextureSampleParameter2D* AddTextureSample(UMaterial* Material, UMaterialEditorOnlyData* EditorOnlyData, UTexture2D* Texture, const FName ParameterName, int32 X, int32 Y, EMaterialSamplerType SamplerType = SAMPLERTYPE_Color)
	{
		if (!Material || !EditorOnlyData || !Texture)
		{
			return nullptr;
		}

		UMaterialExpressionTextureSampleParameter2D* Sample = NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
		Sample->Material = Material;
		Sample->ParameterName = ParameterName;
		Sample->Texture = Texture;
		Sample->SamplerType = SamplerType;
		Sample->MaterialExpressionEditorX = X;
		Sample->MaterialExpressionEditorY = Y;
		EditorOnlyData->ExpressionCollection.AddExpression(Sample);
		return Sample;
	}

	UMaterialExpressionConstant* AddScalar(UMaterial* Material, UMaterialEditorOnlyData* EditorOnlyData, float Value, int32 X, int32 Y)
	{
		UMaterialExpressionConstant* Constant = NewObject<UMaterialExpressionConstant>(Material);
		Constant->Material = Material;
		Constant->R = Value;
		Constant->MaterialExpressionEditorX = X;
		Constant->MaterialExpressionEditorY = Y;
		EditorOnlyData->ExpressionCollection.AddExpression(Constant);
		return Constant;
	}

	UMaterialExpressionConstant3Vector* AddColor(UMaterial* Material, UMaterialEditorOnlyData* EditorOnlyData, const FLinearColor& Value, int32 X, int32 Y)
	{
		UMaterialExpressionConstant3Vector* Constant = NewObject<UMaterialExpressionConstant3Vector>(Material);
		Constant->Material = Material;
		Constant->Constant = Value;
		Constant->MaterialExpressionEditorX = X;
		Constant->MaterialExpressionEditorY = Y;
		EditorOnlyData->ExpressionCollection.AddExpression(Constant);
		return Constant;
	}

	UMaterialExpressionMultiply* AddMultiply(UMaterial* Material, UMaterialEditorOnlyData* EditorOnlyData, UMaterialExpression* A, UMaterialExpression* B, int32 X, int32 Y)
	{
		UMaterialExpressionMultiply* Multiply = NewObject<UMaterialExpressionMultiply>(Material);
		Multiply->Material = Material;
		Multiply->A.Expression = A;
		Multiply->B.Expression = B;
		Multiply->MaterialExpressionEditorX = X;
		Multiply->MaterialExpressionEditorY = Y;
		EditorOnlyData->ExpressionCollection.AddExpression(Multiply);
		return Multiply;
	}

	float NormalizeLegacyPercent(float Value)
	{
		return FMath::Clamp(Value > 1.0f ? Value / 100.0f : Value, 0.0f, 1.0f);
	}

	float ComputeSpecular(const FMT2GrannyMaterialSlot& Slot)
	{
		const float ColorLuminance = FMath::Clamp(
			Slot.SpecularColor.R * 0.2126f + Slot.SpecularColor.G * 0.7152f + Slot.SpecularColor.B * 0.0722f,
			0.0f,
			1.0f);
		float Strength = NormalizeLegacyPercent(Slot.ShininessStrength);
		if ((!Slot.SpecularTextureReference.IsEmpty() || !Slot.ReflectionTextureReference.IsEmpty()) && Strength <= UE_SMALL_NUMBER)
		{
			Strength = 0.0f;
		}
		return FMath::Clamp(Strength * (Slot.bHasSpecularColor ? ColorLuminance : 1.0f), 0.0f, 1.0f);
	}

	float ComputeRoughness(const FMT2GrannyMaterialSlot& Slot, float Specular)
	{
		if (Specular <= UE_SMALL_NUMBER)
		{
			return 1.0f;
		}

		const float NormalizedShininess = NormalizeLegacyPercent(Slot.Shininess);
		const float PhongPower = NormalizedShininess * 128.0f;
		return FMath::Clamp(FMath::Sqrt(2.0f / (PhongPower + 2.0f)), 0.08f, 1.0f);
	}

	bool IsNormalTextureReference(const FString& Reference)
	{
		const FString BaseName = FPaths::GetBaseFilename(Reference).ToLower();
		return BaseName.Contains(TEXT("normal")) || BaseName.EndsWith(TEXT("_n")) || BaseName.EndsWith(TEXT("_nm"));
	}

	bool IsStaticGrannyMeshRecord(
		const FMT2MeshUsageClassifier& UsageClassifier,
		const FMT2AssetRecord& Record)
	{
		if (Record.Kind != EMT2AssetKind::Granny)
		{
			return false;
		}

		FMT2GrannyFileInspection Inspection;
		FString Error;
		if (!FMT2GrannyMeshConverter::InspectGrannyFile(Record.AbsolutePath, Inspection, Error))
		{
			return false;
		}

		return Inspection.Type == EMT2GrannyFileType::StaticMesh ||
			(Inspection.Type == EMT2GrannyFileType::SkeletalMesh &&
				UsageClassifier.IsReady() && !UsageClassifier.RequiresSkeletalMesh(Record, Inspection));
	}

	bool IsStaticMeshRecord(
		const FMT2MeshUsageClassifier& UsageClassifier,
		const FMT2AssetRecord& Record)
	{
		return Record.Kind == EMT2AssetKind::Tree ||
			IsStaticGrannyMeshRecord(UsageClassifier, Record);
	}

	bool IsConvertedLegacySkeletalRecord(
		const FMT2MeshUsageClassifier& UsageClassifier,
		const FMT2AssetRecord& Record)
	{
		if (Record.Kind != EMT2AssetKind::Granny || !UsageClassifier.IsReady())
		{
			return false;
		}

		FMT2GrannyFileInspection Inspection;
		FString Error;
		return FMT2GrannyMeshConverter::InspectGrannyFile(Record.AbsolutePath, Inspection, Error) &&
			Inspection.Type == EMT2GrannyFileType::SkeletalMesh &&
			!UsageClassifier.RequiresSkeletalMesh(Record, Inspection);
	}

	void DeleteLegacySkeletalAssets(
		const FMT2ImportContext& Context,
		const FMT2MeshUsageClassifier& UsageClassifier,
		const FMT2AssetRecord& Record,
		FMT2ImportResult& OutResult)
	{
		if (!IsConvertedLegacySkeletalRecord(UsageClassifier, Record) ||
			!LoadObject<UStaticMesh>(nullptr, *FMT2StaticMeshImporter::BuildObjectPath(Context, Record)))
		{
			return;
		}

		USkeletalMesh* SkeletalMesh = FMT2SkeletalMeshImporter::LoadImportedMesh(Context, Record);
		if (!SkeletalMesh)
		{
			return;
		}

		UPhysicsAsset* PhysicsAsset = SkeletalMesh->GetPhysicsAsset();
		USkeleton* Skeleton = SkeletalMesh->GetSkeleton();
		const FAssetRegistryModule& RegistryModule =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& Registry = RegistryModule.Get();

		TSet<FName> GeneratedPackages;
		GeneratedPackages.Add(SkeletalMesh->GetOutermost()->GetFName());
		if (PhysicsAsset)
		{
			GeneratedPackages.Add(PhysicsAsset->GetOutermost()->GetFName());
		}
		if (Skeleton)
		{
			GeneratedPackages.Add(Skeleton->GetOutermost()->GetFName());
		}

		auto HasOutsideReferencer = [&Registry, &GeneratedPackages](const UObject* Asset)
		{
			TArray<FName> Referencers;
			Registry.GetReferencers(
				Asset->GetOutermost()->GetFName(), Referencers,
				UE::AssetRegistry::EDependencyCategory::Package);
			return Referencers.ContainsByPredicate(
				[&GeneratedPackages](FName Referencer) { return !GeneratedPackages.Contains(Referencer); });
		};

		TArray<FAssetData> AssetsToDelete;
		const FAssetData SkeletalMeshData = Registry.GetAssetByObjectPath(
			FSoftObjectPath(SkeletalMesh->GetPathName()));
		if (SkeletalMeshData.IsValid())
		{
			AssetsToDelete.Add(SkeletalMeshData);
		}
		if (PhysicsAsset && !HasOutsideReferencer(PhysicsAsset))
		{
			const FAssetData PhysicsData = Registry.GetAssetByObjectPath(
				FSoftObjectPath(PhysicsAsset->GetPathName()));
			if (PhysicsData.IsValid())
			{
				AssetsToDelete.Add(PhysicsData);
			}
		}
		if (Skeleton && !HasOutsideReferencer(Skeleton))
		{
			const FAssetData SkeletonData = Registry.GetAssetByObjectPath(
				FSoftObjectPath(Skeleton->GetPathName()));
			if (SkeletonData.IsValid())
			{
				AssetsToDelete.Add(SkeletonData);
			}
		}

		const int32 DeletedCount = ObjectTools::DeleteAssets(AssetsToDelete, false);
		if (DeletedCount > 0)
		{
			OutResult.AddInfo(FString::Printf(
				TEXT("Converted %s to a static mesh and deleted %d obsolete skeletal asset(s)."),
				*Record.VirtualPath, DeletedCount), Record.AbsolutePath);
		}
		if (DeletedCount < AssetsToDelete.Num())
		{
			OutResult.AddWarning(FString::Printf(
				TEXT("Static conversion succeeded, but Unreal kept %d referenced legacy skeletal asset(s) for %s."),
				AssetsToDelete.Num() - DeletedCount, *Record.VirtualPath), Record.AbsolutePath);
		}
	}

	bool IsWeaponMeshRecord(const FMT2AssetRecord& Record)
	{
		FString Path = Record.VirtualPath.IsEmpty() ? Record.ContentPath : Record.VirtualPath;
		Path.ReplaceInline(TEXT("\\"), TEXT("/"));
		return Path.Contains(TEXT("/item/weapon/"), ESearchCase::IgnoreCase);
	}

	bool NeedsWeaponPlacementReimport(const FMT2ImportContext& Context, const FMT2AssetRecord& Record)
	{
		if (!IsWeaponMeshRecord(Record))
		{
			return false;
		}

		UStaticMesh* ExistingMesh = LoadObject<UStaticMesh>(
			nullptr, *FMT2StaticMeshImporter::BuildObjectPath(Context, Record));
		if (!ExistingMesh)
		{
			return false;
		}

		FMetaData& MetaData = ExistingMesh->GetOutermost()->GetMetaData();
		return !MetaData.GetValue(
			ExistingMesh, StaticMeshConversionVersionKey).Equals(
				StaticMeshConversionVersion, ESearchCase::CaseSensitive);
	}

	void MarkWeaponPlacementCurrent(UStaticMesh* StaticMesh, const FMT2AssetRecord& Record)
	{
		if (!StaticMesh || !IsWeaponMeshRecord(Record))
		{
			return;
		}

		FMetaData& MetaData = StaticMesh->GetOutermost()->GetMetaData();
		MetaData.SetValue(
			StaticMesh, StaticMeshConversionVersionKey, StaticMeshConversionVersion);
		StaticMesh->MarkPackageDirty();
	}
}

FMT2StaticMeshImporter::FMT2StaticMeshImporter()
	: FMT2ImporterBase(EMT2ImportDomain::StaticMeshes, TEXT("StaticMeshImporter"))
{
}

bool FMT2StaticMeshImporter::Discover(const FMT2ImportContext& Context, const FMT2AssetScanResult& ScanResult, FMT2ImportDiscovery& OutDiscovery) const
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
	const auto IsAlreadyImported = [&Context, &AssetRegistryModule, &UsageClassifier](const FMT2AssetRecord& Record)
	{
		if (Context.bReplaceExisting)
		{
			return false;
		}
		const FString ObjectPath = FMT2StaticMeshImporter::BuildObjectPath(Context, Record);
		const bool bStaticMeshExists =
			AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(ObjectPath)).IsValid() &&
			!NeedsWeaponPlacementReimport(Context, Record);
		if (!bStaticMeshExists)
		{
			return false;
		}
		if (IsConvertedLegacySkeletalRecord(UsageClassifier, Record) &&
			FMT2SkeletalMeshImporter::LoadImportedMesh(Context, Record))
		{
			return false;
		}
		return true;
	};

	FMT2ResolvedWorldResult ResolvedWorld;
	FString Error;
	FMT2PropertyResolver Resolver;
	TMap<FString, int32> PlacementCountsByAssetPath;
	if (Resolver.Resolve(ScanResult, ResolvedWorld, Error))
	{
		TMap<FString, const FMT2AssetRecord*> UniqueStaticMeshesByPath;
		for (const FMT2MapObjectPlacement& Placement : ResolvedWorld.MapObjects)
		{
			if (Placement.ReferencedAsset &&
				!IsAlreadyImported(*Placement.ReferencedAsset) &&
				IsStaticMeshRecord(UsageClassifier, *Placement.ReferencedAsset))
			{
				UniqueStaticMeshesByPath.FindOrAdd(Placement.ReferencedAsset->AbsolutePath, Placement.ReferencedAsset);
				PlacementCountsByAssetPath.FindOrAdd(Placement.ReferencedAsset->AbsolutePath)++;
			}
		}

		for (const TPair<FString, const FMT2AssetRecord*>& Pair : UniqueStaticMeshesByPath)
		{
			if (Pair.Value)
			{
				OutDiscovery.AssetRecords.Add(*Pair.Value);
			}
		}
	}

	if (Context.bReplaceExisting)
	{
		TSet<FString> ListedSourcePaths;
		for (const FMT2AssetRecord& Record : OutDiscovery.AssetRecords)
		{
			ListedSourcePaths.Add(Record.AbsolutePath);
		}

		TArray<FMT2AssetRecord> CandidateRecords = ScanResult.GetRecordsByKind(EMT2AssetKind::Granny);
		CandidateRecords.Append(ScanResult.GetRecordsByKind(EMT2AssetKind::Tree));
		for (const FMT2AssetRecord& Record : CandidateRecords)
		{
			if (ListedSourcePaths.Contains(Record.AbsolutePath))
			{
				continue;
			}

			const FAssetData ExistingAsset = AssetRegistryModule.Get().GetAssetByObjectPath(
				FSoftObjectPath(BuildObjectPath(Context, Record)));
			const bool bIsImportedStaticMesh =
				ExistingAsset.IsValid() &&
				ExistingAsset.AssetClassPath == UStaticMesh::StaticClass()->GetClassPathName();
			if (bIsImportedStaticMesh || IsStaticMeshRecord(UsageClassifier, Record))
			{
				OutDiscovery.AssetRecords.Add(Record);
				ListedSourcePaths.Add(Record.AbsolutePath);
			}
		}
	}
	else
	{
		TSet<FString> ListedSourcePaths;
		for (const FMT2AssetRecord& Record : OutDiscovery.AssetRecords)
		{
			ListedSourcePaths.Add(Record.AbsolutePath);
		}

		TArray<FMT2AssetRecord> CandidateRecords = ScanResult.GetRecordsByKind(EMT2AssetKind::Granny);
		CandidateRecords.Append(ScanResult.GetRecordsByKind(EMT2AssetKind::Tree));
		for (const FMT2AssetRecord& Record : CandidateRecords)
		{
			if (!ListedSourcePaths.Contains(Record.AbsolutePath) &&
				!IsAlreadyImported(Record) && IsStaticMeshRecord(UsageClassifier, Record))
			{
				OutDiscovery.AssetRecords.Add(Record);
				ListedSourcePaths.Add(Record.AbsolutePath);
			}
		}
		if (!Error.IsEmpty())
		{
			FMT2ImportMessage Message;
			Message.Severity = EMT2ImportSeverity::Warning;
			Message.Text = FString::Printf(TEXT("Static placement resolution failed; showing all static mesh sources instead. %s"), *Error);
			OutDiscovery.Messages.Add(MoveTemp(Message));
		}
	}

	OutDiscovery.ItemsDiscovered = OutDiscovery.AssetRecords.Num();
	for (const FMT2AssetRecord& Record : OutDiscovery.AssetRecords)
	{
		OutDiscovery.EntryNames.Add(Record.VirtualPath.IsEmpty() ? Record.ContentPath : Record.VirtualPath);
		const int32 PlacementCount = PlacementCountsByAssetPath.FindRef(Record.AbsolutePath);
		if (PlacementCount > 0)
		{
			OutDiscovery.EntryCountsByName.Add(Record.AbsolutePath, PlacementCount);
		}
	}
	return true;
}

bool FMT2StaticMeshImporter::Import(const FMT2ImportRequest& Request, FMT2ImportResult& OutResult)
{
	if (!CanImport(Request, OutResult))
	{
		return false;
	}

	if (Request.Selection.AssetRecords.Num() == 0)
	{
		OutResult.AddWarning(TEXT("No static mesh records were selected."));
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

	struct FPendingMaterialAssignment
	{
		FMT2AssetRecord Record;
		TArray<FMT2GrannyMaterialSlot> MaterialSlots;
		TSet<FString> PreImportMaterialObjectPaths;
	};

	TArray<UAssetImportTask*> Tasks;
	TArray<FPendingMaterialAssignment> PendingMaterialAssignments;
	const int32 MaxItems = Request.MaxItems > 0 ? Request.MaxItems : Request.Selection.AssetRecords.Num();
	int32 ConsideredCount = 0;
	FScopedSlowTask MeshProgress(static_cast<float>(MaxItems + 1), NSLOCTEXT("FMT2StaticMeshImporter", "ImportStaticMeshesProgress", "Importing static meshes..."));

	for (const FMT2AssetRecord& Record : Request.Selection.AssetRecords)
	{
		if (ConsideredCount >= MaxItems)
		{
			break;
		}

		MeshProgress.EnterProgressFrame(1.0f, FText::Format(
			NSLOCTEXT("FMT2StaticMeshImporter", "ImportStaticMeshProgressFormat", "Preparing static mesh {0} of {1}: {2}"),
			FText::AsNumber(ConsideredCount + 1),
			FText::AsNumber(MaxItems),
			FText::FromString(Record.VirtualPath)));
		if (Request.Context.IsStopRequested() || MeshProgress.ShouldCancel())
		{
			OutResult.AddWarning(TEXT("Static mesh import stopped by user."));
			break;
		}

		ConsideredCount++;
		OutResult.ItemsDiscovered++;

		if (Record.Kind != EMT2AssetKind::Granny && Record.Kind != EMT2AssetKind::Tree)
		{
			OutResult.ItemsSkipped++;
			OutResult.AddWarning(FString::Printf(TEXT("Skipped unsupported static mesh record: %s"), *Record.VirtualPath), Record.AbsolutePath);
			continue;
		}

		if (Record.Kind == EMT2AssetKind::Granny)
		{
			FMT2GrannyFileInspection Inspection;
			FString InspectionError;
			if (!FMT2GrannyMeshConverter::InspectGrannyFile(Record.AbsolutePath, Inspection, InspectionError) ||
				!IsStaticMeshRecord(UsageClassifier, Record))
			{
				OutResult.ItemsSkipped++;
				OutResult.AddWarning(FString::Printf(TEXT("Skipped non-static Granny mesh: %s"), *Record.VirtualPath), Record.AbsolutePath);
				continue;
			}
		}

		const FString ObjectPath = BuildObjectPath(Request.Context, Record);
		const bool bNeedsPlacementReimport = NeedsWeaponPlacementReimport(Request.Context, Record);
		if (!Request.Context.bReplaceExisting && !bNeedsPlacementReimport && LoadObject<UStaticMesh>(nullptr, *ObjectPath))
		{
			if (!Request.Context.bDryRun && Record.Kind == EMT2AssetKind::Granny)
			{
				TArray<FMT2GrannyMaterialSlot> ExistingMaterialSlots;
				FString MaterialError;
				if (FMT2GrannyMeshConverter::ExtractMaterialSlots(Record.AbsolutePath, ExistingMaterialSlots, MaterialError))
				{
					if (Request.Context.bImportStaticObjectTextures)
					{
						ImportMaterialTexturesForSlots(Request.Context, Record, ExistingMaterialSlots, OutResult);
					}
					CreateAndAssignMaterials(Request.Context, Record, ExistingMaterialSlots, OutResult);
					OutResult.AddInfo(FString::Printf(TEXT("Refreshed materials without reimporting static mesh: %s"), *ObjectPath), Record.AbsolutePath);
				}
				else
				{
					OutResult.AddWarning(FString::Printf(TEXT("Could not refresh Granny materials for %s: %s"), *Record.VirtualPath, *MaterialError), Record.AbsolutePath);
				}
			}
			OutResult.ItemsSkipped++;
			OutResult.CreatedPackages.Add(ObjectPath);
			OutResult.AddInfo(FString::Printf(TEXT("Skipped already imported static mesh: %s"), *ObjectPath), Record.AbsolutePath);
			DeleteLegacySkeletalAssets(Request.Context, UsageClassifier, Record, OutResult);
			continue;
		}

		FString MeshPath = BuildConvertedMeshPath(Request.Context, Record);
		if (Request.Context.bDryRun)
		{
			OutResult.ItemsSkipped++;
			OutResult.CreatedFiles.Add(MeshPath);
			OutResult.CreatedPackages.Add(ObjectPath);
			continue;
		}

		TArray<FMT2GrannyMaterialSlot> MaterialSlots;
		FString MaterialError;
		bool bConverted = false;
		if (Record.Kind == EMT2AssetKind::Tree)
		{
			bConverted = FMT2SpeedTreeConverter::ConvertToObj(
				Record.AbsolutePath, MeshPath, MaterialSlots, MaterialError);
			if (!bConverted)
			{
				OutResult.AddError(FString::Printf(TEXT("SpeedTree import failed for %s: %s"),
					*Record.VirtualPath, *MaterialError), Record.AbsolutePath);
			}
		}
		else
		{
			if (!FMT2GrannyMeshConverter::ExtractMaterialSlots(Record.AbsolutePath, MaterialSlots, MaterialError))
			{
				OutResult.AddWarning(FString::Printf(TEXT("Could not read Granny material data for %s: %s"), *Record.VirtualPath, *MaterialError), Record.AbsolutePath);
			}
			bConverted = ConvertGrannyToMeshSource(Request.Context, Record, MeshPath, OutResult);
		}

		if (!bConverted)
		{
			OutResult.ItemsSkipped++;
			continue;
		}

		const TSet<FString> PreImportMaterialObjectPaths = CollectMaterialObjectPathsInFolder(BuildDestinationPath(Request.Context, Record));

		if (Request.Context.bImportStaticObjectTextures)
		{
			ImportMaterialTexturesForSlots(Request.Context, Record, MaterialSlots, OutResult);
		}

		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->AddToRoot();
		Task->Filename = MeshPath;
		Task->DestinationPath = BuildDestinationPath(Request.Context, Record);
		Task->DestinationName = FPackageName::ObjectPathToObjectName(ObjectPath);
		Task->bAutomated = true;
		Task->bSave = false;
		Task->bReplaceExisting = Request.Context.bReplaceExisting || bNeedsPlacementReimport;
		Task->Factory = nullptr;

		UFbxImportUI* ImportOptions = NewObject<UFbxImportUI>(Task);
		ImportOptions->bIsObjImport = true;
		ImportOptions->OriginalImportType = FBXIT_StaticMesh;
		ImportOptions->MeshTypeToImport = FBXIT_StaticMesh;
		ImportOptions->bImportAsSkeletal = false;
		ImportOptions->bImportMesh = true;
		ImportOptions->bImportMaterials = false;
		ImportOptions->bImportTextures = false;
		ImportOptions->bAutomatedImportShouldDetectType = false;
		ImportOptions->StaticMeshImportData->bCombineMeshes = true;
		Task->Options = ImportOptions;

		Tasks.Add(Task);
		PendingMaterialAssignments.Add({ Record, MoveTemp(MaterialSlots), PreImportMaterialObjectPaths });

		OutResult.CreatedFiles.Add(MeshPath);
		OutResult.CreatedPackages.Add(ObjectPath);
	}

	if (Tasks.Num() > 0)
	{
		MeshProgress.EnterProgressFrame(1.0f, FText::Format(
			NSLOCTEXT("FMT2StaticMeshImporter", "CreateStaticMeshAssetsProgressFormat", "Creating {0} static mesh asset(s)..."),
			FText::AsNumber(Tasks.Num())));
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
		AssetToolsModule.Get().ImportAssetTasks(Tasks);

		for (UAssetImportTask* Task : Tasks)
		{
			Task->RemoveFromRoot();
		}

		for (const FPendingMaterialAssignment& Assignment : PendingMaterialAssignments)
		{
			CreateAndAssignMaterials(Request.Context, Assignment.Record, Assignment.MaterialSlots, OutResult);
			DeleteUnreferencedNewMaterials(Request.Context, Assignment.Record, Assignment.PreImportMaterialObjectPaths, OutResult);
			if (UStaticMesh* ImportedMesh = LoadObject<UStaticMesh>(nullptr, *BuildObjectPath(Request.Context, Assignment.Record)))
			{
				FMT2AssetOptimizer::ConfigureStaticMesh(ImportedMesh);
				MarkWeaponPlacementCurrent(ImportedMesh, Assignment.Record);
			}
			DeleteLegacySkeletalAssets(Request.Context, UsageClassifier, Assignment.Record, OutResult);
		}
	}

	OutResult.ItemsImported = Tasks.Num();
	if (Request.Context.bDryRun)
	{
		OutResult.AddInfo(FString::Printf(TEXT("Dry run: prepared %d static mesh conversion/import task(s)."), OutResult.CreatedPackages.Num()));
	}
	else
	{
		OutResult.AddInfo(FString::Printf(TEXT("Submitted %d static mesh import task(s)."), Tasks.Num()));
	}

	OutResult.bSucceeded = !OutResult.HasErrors();
	return OutResult.bSucceeded;
}

FString FMT2StaticMeshImporter::BuildDestinationPath(const FMT2ImportContext& Context, const FMT2AssetRecord& Record)
{
	return FMT2AssetScanner::BuildContentPackagePath(Context.DestinationRoot, Record);
}

FString FMT2StaticMeshImporter::BuildObjectPath(const FMT2ImportContext& Context, const FMT2AssetRecord& Record)
{
	return FMT2AssetScanner::BuildContentObjectPath(Context.DestinationRoot, Record);
}

FString FMT2StaticMeshImporter::BuildSharedMaterialFolder(const FMT2ImportContext& Context)
{
	FString Root = Context.DestinationRoot;
	Root.RemoveFromEnd(TEXT("/"));
	return Root / UMT2PathSettings::Path(TEXT("Part_Materials"));
}

FString FMT2StaticMeshImporter::BuildSharedMaterialObjectPath(const FMT2ImportContext& Context, const FMT2GrannyMaterialSlot& Slot, int32 SlotIndex, const FString& DiffuseTextureObjectPath, const FString& OpacityTextureObjectPath)
{
	const FString SlotName = Slot.SlotName.IsEmpty() ? FString::Printf(TEXT("material_%d"), SlotIndex) : Slot.SlotName;
	const FString DiffuseKey = DiffuseTextureObjectPath.IsEmpty() ? FMT2TextureImporter::NormalizeReferencePath(Slot.DiffuseTextureReference).ToLower() : DiffuseTextureObjectPath.ToLower();
	const FString OpacityKey = OpacityTextureObjectPath.IsEmpty() ? FMT2TextureImporter::NormalizeReferencePath(Slot.OpacityTextureReference).ToLower() : OpacityTextureObjectPath.ToLower();
	const FString AmbientKey = FMT2TextureImporter::NormalizeReferencePath(Slot.AmbientTextureReference).ToLower();
	const bool bHasTextureIdentity = !DiffuseKey.IsEmpty() || !OpacityKey.IsEmpty() || !AmbientKey.IsEmpty() ||
		!Slot.SpecularTextureReference.IsEmpty() || !Slot.BumpTextureReference.IsEmpty() ||
		!Slot.ReflectionTextureReference.IsEmpty();
	const FString NameKey = bHasTextureIdentity ? FString() : SlotName.ToLower();
	const FString MaterialKey = FString::Printf(
		TEXT("%s|%s|%s|%s|%s|%s|%d|%.6f|%.6f|%.6f|%.6f|%.6f|%.6f|%.6f|%.6f|%.6f"),
		*DiffuseKey,
		*OpacityKey,
		*FMT2TextureImporter::NormalizeReferencePath(Slot.SpecularTextureReference).ToLower(),
		*FMT2TextureImporter::NormalizeReferencePath(Slot.BumpTextureReference).ToLower(),
		*FMT2TextureImporter::NormalizeReferencePath(Slot.ReflectionTextureReference).ToLower(),
		*NameKey,
		1,
		Slot.DiffuseColor.R,
		Slot.DiffuseColor.G,
		Slot.DiffuseColor.B,
		Slot.SpecularColor.R,
		Slot.SpecularColor.G,
		Slot.SpecularColor.B,
		Slot.Opacity,
		Slot.Shininess,
		Slot.ShininessStrength) +
		FString::Printf(TEXT("|%s|%.6f|%.6f"), *AmbientKey, Slot.ReflectionLevel, Slot.IndexOfRefraction);
	const uint32 MaterialHash = FCrc::StrCrc32(*MaterialKey);

	FString BaseName = DiffuseTextureObjectPath.IsEmpty() ? SlotName : FPackageName::ObjectPathToObjectName(DiffuseTextureObjectPath);
	BaseName.RemoveFromStart(TEXT("T_"), ESearchCase::IgnoreCase);
	if (!OpacityTextureObjectPath.IsEmpty())
	{
		FString OpacityName = FPackageName::ObjectPathToObjectName(OpacityTextureObjectPath);
		OpacityName.RemoveFromStart(TEXT("T_"), ESearchCase::IgnoreCase);
		BaseName += TEXT("_") + OpacityName;
	}

	const FString MaterialAssetName = FMT2AssetScanner::SanitizePackagePathSegment(FString::Printf(TEXT("M_%s_%08x"), *BaseName, MaterialHash));
	return BuildSharedMaterialFolder(Context) / MaterialAssetName + TEXT(".") + MaterialAssetName;
}

FString FMT2StaticMeshImporter::BuildConvertedMeshPath(const FMT2ImportContext& Context, const FMT2AssetRecord& Record)
{
	TArray<FString> Parts;
	Record.ContentPath.ParseIntoArray(Parts, TEXT("/"), true);

	FString Result = GetWorkingRoot(Context) / UMT2PathSettings::Path(TEXT("Part_Converted")) / FMT2AssetScanner::SanitizePackagePathSegment(Record.PackName);
	for (int32 Index = 0; Index < Parts.Num(); ++Index)
	{
		if (Index == Parts.Num() - 1)
		{
			Result /= FMT2AssetScanner::SanitizePackagePathSegment(FPaths::GetBaseFilename(Parts[Index])) + TEXT(".obj");
		}
		else
		{
			Result /= FMT2AssetScanner::SanitizePackagePathSegment(Parts[Index]);
		}
	}
	return Result;
}

FString FMT2StaticMeshImporter::GetWorkingRoot(const FMT2ImportContext& Context)
{
	FString WorkingRoot = Context.WorkingDirectory;
	if (WorkingRoot.IsEmpty())
	{
		WorkingRoot = UMT2PathSettings::Path(TEXT("ImporterWorkingDirectory"));
	}
	FPaths::NormalizeFilename(WorkingRoot);
	return WorkingRoot;
}

bool FMT2StaticMeshImporter::ConvertGrannyToMeshSource(const FMT2ImportContext& Context, const FMT2AssetRecord& Record, FString& OutMeshPath, FMT2ImportResult& OutResult)
{
	OutMeshPath = BuildConvertedMeshPath(Context, Record);
	FPaths::NormalizeFilename(OutMeshPath);

	if (IFileManager::Get().FileExists(*OutMeshPath))
	{
		FString ExistingObjText;
		if (FFileHelper::LoadFileToString(ExistingObjText, *OutMeshPath) &&
			ExistingObjText.Contains(StaticMeshConversionVersion) &&
			!ExistingObjText.Contains(TEXT("mtllib"), ESearchCase::IgnoreCase))
		{
			IFileManager::Get().Delete(*FPaths::ChangeExtension(OutMeshPath, TEXT(".mtl")), false, true);
			OutResult.AddInfo(FString::Printf(TEXT("Using cached converted mesh: %s"), *OutMeshPath), Record.AbsolutePath);
			return true;
		}
	}

	const FString OutputDirectory = FPaths::GetPath(OutMeshPath);
	FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*OutputDirectory);
	OutResult.AddInfo(FString::Printf(TEXT("Converting Granny mesh: %s"), *Record.VirtualPath), Record.AbsolutePath);

	FString Error;
	if (!FMT2GrannyMeshConverter::ConvertGrannyToObj(Record.AbsolutePath, OutMeshPath, Error))
	{
		OutResult.AddError(FString::Printf(TEXT("Granny import failed for %s: %s"), *Record.VirtualPath, *Error), Record.AbsolutePath);
		return false;
	}

	if (!IFileManager::Get().FileExists(*OutMeshPath))
	{
		OutResult.AddError(FString::Printf(TEXT("Converter finished but did not create mesh source: %s"), *OutMeshPath), Record.AbsolutePath);
		return false;
	}

	return true;
}

void FMT2StaticMeshImporter::ImportMaterialTexturesForConvertedMesh(const FMT2ImportContext& Context, const FString& MeshPath, FMT2ImportResult& OutResult)
{
	if (!Context.ScanResult)
	{
		OutResult.AddWarning(TEXT("Cannot import static mesh material textures because Context.ScanResult is not available."), MeshPath);
		return;
	}

	const TArray<FString> TextureReferences = ReadMaterialTextureReferences(MeshPath, OutResult);
	if (TextureReferences.Num() == 0)
	{
		return;
	}

	TMap<FString, FMT2AssetRecord> UniqueTexturesByPath;
	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	for (const FString& TextureReference : TextureReferences)
	{
		if (const FMT2AssetRecord* TextureRecord = FMT2TextureImporter::FindRecordForReference(*Context.ScanResult, TextureReference))
		{
			const FString TextureObjectPath = FMT2TextureImporter::BuildObjectPath(Context, *TextureRecord);
			if (AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(TextureObjectPath)).IsValid())
			{
				continue;
			}
			UniqueTexturesByPath.FindOrAdd(TextureRecord->AbsolutePath, *TextureRecord);
		}
		else
		{
			OutResult.AddWarning(FString::Printf(TEXT("Could not resolve material texture reference: %s"), *TextureReference), MeshPath);
		}
	}

	if (UniqueTexturesByPath.Num() == 0)
	{
		return;
	}

	FMT2ImportRequest TextureRequest;
	TextureRequest.Context = Context;
	TextureRequest.Selection.Domain = EMT2ImportDomain::Textures;
	UniqueTexturesByPath.GenerateValueArray(TextureRequest.Selection.AssetRecords);

	FMT2TextureImporter TextureImporter;
	FMT2ImportResult TextureResult;
	TextureImporter.Import(TextureRequest, TextureResult);
	OutResult.CreatedPackages.Append(TextureResult.CreatedPackages);
	OutResult.Messages.Append(TextureResult.Messages);
	OutResult.AddInfo(FString::Printf(TEXT("Imported/resolved %d material texture(s) for %s."), UniqueTexturesByPath.Num(), *FPaths::GetCleanFilename(MeshPath)), MeshPath);
}

void FMT2StaticMeshImporter::ImportMaterialTexturesForSlots(const FMT2ImportContext& Context, const FMT2AssetRecord& Record, const TArray<FMT2GrannyMaterialSlot>& MaterialSlots, FMT2ImportResult& OutResult)
{
	if (!Context.ScanResult)
	{
		OutResult.AddWarning(TEXT("Cannot import static mesh material textures because Context.ScanResult is not available."), Record.AbsolutePath);
		return;
	}

	TArray<FString> TextureReferences;
	for (const FMT2GrannyMaterialSlot& Slot : MaterialSlots)
	{
		const TArray<FString> SlotReferences =
		{
			Slot.AmbientTextureReference,
			Slot.DiffuseTextureReference,
			Slot.SpecularTextureReference,
			Slot.OpacityTextureReference,
			Slot.BumpTextureReference,
			Slot.ReflectionTextureReference
		};
		for (const FString& Reference : SlotReferences)
		{
			if (!Reference.IsEmpty())
			{
				TextureReferences.AddUnique(Reference);
			}
		}
	}

	if (TextureReferences.Num() == 0)
	{
		return;
	}

	TMap<FString, FMT2AssetRecord> UniqueTexturesByPath;
	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	for (const FString& TextureReference : TextureReferences)
	{
		if (const FMT2AssetRecord* TextureRecord = FMT2TextureImporter::FindRecordForReference(*Context.ScanResult, TextureReference, Record))
		{
			const FString TextureObjectPath = FMT2TextureImporter::BuildObjectPath(Context, *TextureRecord);
			const bool bAlreadyImported = AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(TextureObjectPath)).IsValid();
			if (Context.bEnableDebugLogs)
			{
				OutResult.AddInfo(FString::Printf(TEXT("[Debug][MaterialTextureResolve] Ref='%s' -> Record='%s' Object='%s' AlreadyImported=%s"),
					*TextureReference,
					*TextureRecord->VirtualPath,
					*TextureObjectPath,
					bAlreadyImported ? TEXT("true") : TEXT("false")),
					TextureRecord->AbsolutePath);
			}
			if (bAlreadyImported)
			{
				continue;
			}
			UniqueTexturesByPath.FindOrAdd(TextureRecord->AbsolutePath, *TextureRecord);
		}
		else
		{
			if (Context.bEnableDebugLogs)
			{
				OutResult.AddInfo(FString::Printf(TEXT("[Debug][MaterialTextureResolve] Ref='%s' did not resolve to any scanned texture record."), *TextureReference), Record.AbsolutePath);
			}
			OutResult.AddWarning(FString::Printf(TEXT("Could not resolve Granny material texture reference: %s"), *TextureReference), Record.AbsolutePath);
		}
	}

	if (UniqueTexturesByPath.Num() == 0)
	{
		return;
	}

	FMT2ImportRequest TextureRequest;
	TextureRequest.Context = Context;
	TextureRequest.Selection.Domain = EMT2ImportDomain::Textures;
	UniqueTexturesByPath.GenerateValueArray(TextureRequest.Selection.AssetRecords);

	FMT2TextureImporter TextureImporter;
	FMT2ImportResult TextureResult;
	TextureImporter.Import(TextureRequest, TextureResult);
	OutResult.CreatedPackages.Append(TextureResult.CreatedPackages);
	OutResult.Messages.Append(TextureResult.Messages);
	OutResult.AddInfo(FString::Printf(TEXT("Imported %d missing material texture(s) for %s."), UniqueTexturesByPath.Num(), *Record.VirtualPath), Record.AbsolutePath);
}

TArray<FString> FMT2StaticMeshImporter::ReadMaterialTextureReferences(const FString& MeshPath, FMT2ImportResult& OutResult)
{
	TArray<FString> References;
	const FString MaterialSidecarPath = FPaths::ChangeExtension(MeshPath, TEXT(".mtl"));
	if (!IFileManager::Get().FileExists(*MaterialSidecarPath))
	{
		OutResult.AddWarning(FString::Printf(TEXT("No .mtl sidecar found for converted mesh: %s"), *MaterialSidecarPath), MeshPath);
		return References;
	}

	TArray<FString> Lines;
	FString MaterialText;
	if (!FFileHelper::LoadFileToString(MaterialText, *MaterialSidecarPath))
	{
		OutResult.AddWarning(FString::Printf(TEXT("Could not read .mtl sidecar: %s"), *MaterialSidecarPath), MeshPath);
		return References;
	}
	MaterialText.ParseIntoArrayLines(Lines);

	const TArray<FString> TextureKeys =
	{
		TEXT("map_Kd"),
		TEXT("map_Ka"),
		TEXT("map_Ks"),
		TEXT("map_d"),
		TEXT("map_Bump"),
		TEXT("bump")
	};

	for (FString Line : Lines)
	{
		Line.TrimStartAndEndInline();
		if (Line.IsEmpty() || Line.StartsWith(TEXT("#")))
		{
			continue;
		}

		for (const FString& Key : TextureKeys)
		{
			if (Line.StartsWith(Key + TEXT(" "), ESearchCase::IgnoreCase))
			{
				FString Reference = Line.RightChop(Key.Len());
				Reference.TrimStartAndEndInline();
				Reference.RemoveFromStart(TEXT("\""));
				Reference.RemoveFromEnd(TEXT("\""));
				if (!Reference.IsEmpty())
				{
					References.Add(Reference);
				}
				break;
			}
		}
	}

	return References;
}

UMaterial* FMT2StaticMeshImporter::CreateOrUpdateSharedMaterial(const FMT2ImportContext& Context, const FMT2AssetRecord& Record, const FMT2GrannyMaterialSlot& Slot, int32 SlotIndex, FMT2ImportResult& OutResult, FString* OutMaterialObjectPath)
{
	FString AmbientTextureObjectPath;
	FString DiffuseTextureObjectPath;
	FString SpecularTextureObjectPath;
	FString OpacityTextureObjectPath;
	FString BumpTextureObjectPath;
	FString ReflectionTextureObjectPath;
	UTexture2D* AmbientTexture = ResolveMaterialTexture(Context, Record, Slot.AmbientTextureReference, AmbientTextureObjectPath);
	UTexture2D* DiffuseTexture = ResolveMaterialTexture(Context, Record, Slot.DiffuseTextureReference, DiffuseTextureObjectPath);
	UTexture2D* SpecularTexture = ResolveMaterialTexture(Context, Record, Slot.SpecularTextureReference, SpecularTextureObjectPath);
	UTexture2D* OpacityTexture = ResolveMaterialTexture(Context, Record, Slot.OpacityTextureReference, OpacityTextureObjectPath);
	UTexture2D* BumpTexture = ResolveMaterialTexture(Context, Record, Slot.BumpTextureReference, BumpTextureObjectPath);
	UTexture2D* ReflectionTexture = ResolveMaterialTexture(Context, Record, Slot.ReflectionTextureReference, ReflectionTextureObjectPath);

	const FString MaterialObjectPath = BuildSharedMaterialObjectPath(Context, Slot, SlotIndex, DiffuseTextureObjectPath, OpacityTextureObjectPath);
	if (OutMaterialObjectPath)
	{
		*OutMaterialObjectPath = MaterialObjectPath;
	}
	const FString MaterialPackageName = FPackageName::ObjectPathToPackageName(MaterialObjectPath);
	const FString MaterialAssetName = FPackageName::ObjectPathToObjectName(MaterialObjectPath);
	UMaterial* Material = LoadObject<UMaterial>(nullptr, *MaterialObjectPath);
	const bool bNewMaterial = Material == nullptr;
	UPackage* Package = nullptr;
	if (!Material)
	{
		Package = CreatePackage(*MaterialPackageName);
		Material = NewObject<UMaterial>(Package, *MaterialAssetName, RF_Public | RF_Standalone | RF_Transactional);
	}
	else
	{
		Package = Material->GetOutermost();
	}
	if (!Material || !Package)
	{
		OutResult.AddWarning(FString::Printf(TEXT("Failed to create material %s."), *MaterialObjectPath), Record.AbsolutePath);
		return nullptr;
	}

	Material->Modify();
	// Imported MT2 geometry frequently relies on back-face rendering even when the legacy material
	// metadata omitted its two-sided flag. Landscape materials use a separate importer and are not
	// created through this path.
	Material->TwoSided = true;
	Material->BlendMode = BLEND_Opaque;
	UMaterialEditorOnlyData* EditorOnlyData = Material->GetEditorOnlyData();
	if (!EditorOnlyData)
	{
		OutResult.AddWarning(FString::Printf(TEXT("Failed to access editor-only material data for %s."), *MaterialObjectPath), Record.AbsolutePath);
		return Material;
	}

	EditorOnlyData->ExpressionCollection.Empty();
	EditorOnlyData->BaseColor.Expression = nullptr;
	EditorOnlyData->Metallic.Expression = nullptr;
	EditorOnlyData->Specular.Expression = nullptr;
	EditorOnlyData->Roughness.Expression = nullptr;
	EditorOnlyData->EmissiveColor.Expression = nullptr;
	EditorOnlyData->Opacity.Expression = nullptr;
	EditorOnlyData->OpacityMask.Expression = nullptr;
	EditorOnlyData->Normal.Expression = nullptr;

	UMaterialExpression* BaseColorExpression = nullptr;
	UMaterialExpressionTextureSampleParameter2D* BaseTextureSample = nullptr;
	UTexture2D* BaseTexture = DiffuseTexture ? DiffuseTexture : AmbientTexture;
	if (BaseTexture)
	{
		BaseTextureSample = AddTextureSample(Material, EditorOnlyData, BaseTexture, TEXT("Diffuse"), -620, 0);
		BaseColorExpression = BaseTextureSample;
		if (Slot.bHasDiffuseColor)
		{
			BaseColorExpression = AddMultiply(Material, EditorOnlyData, BaseColorExpression, AddColor(Material, EditorOnlyData, Slot.DiffuseColor, -620, 100), -350, 40);
		}
	}
	else
	{
		BaseColorExpression = AddColor(Material, EditorOnlyData, Slot.bHasDiffuseColor ? Slot.DiffuseColor : FLinearColor(0.7f, 0.7f, 0.7f), -350, 0);
	}
	EditorOnlyData->BaseColor.Expression = BaseColorExpression;

	const float Specular = ComputeSpecular(Slot);
	const float Roughness = ComputeRoughness(Slot, Specular);
	UTexture2D* SpecularSourceTexture = SpecularTexture ? SpecularTexture : ReflectionTexture;
	if (SpecularSourceTexture)
	{
		const bool bDedicatedSpecularTexture = SpecularSourceTexture != DiffuseTexture && SpecularSourceTexture != AmbientTexture;
		if (bDedicatedSpecularTexture)
		{
			SpecularSourceTexture->Modify();
			SpecularSourceTexture->SRGB = false;
			SpecularSourceTexture->CompressionSettings = TC_Masks;
			SpecularSourceTexture->PostEditChange();
		}
		UMaterialExpressionTextureSampleParameter2D* SpecularSample = AddTextureSample(Material, EditorOnlyData, SpecularSourceTexture, TEXT("SpecularMap"), -620, 260, bDedicatedSpecularTexture ? SAMPLERTYPE_Masks : SAMPLERTYPE_Color);
		UMaterialExpressionMultiply* SpecularMultiply = NewObject<UMaterialExpressionMultiply>(Material);
		SpecularMultiply->Material = Material;
		SpecularMultiply->A.Expression = SpecularSample;
		SpecularMultiply->A.Mask = 1;
		SpecularMultiply->A.MaskR = 1;
		SpecularMultiply->ConstB = Specular;
		SpecularMultiply->MaterialExpressionEditorX = -350;
		SpecularMultiply->MaterialExpressionEditorY = 260;
		EditorOnlyData->ExpressionCollection.AddExpression(SpecularMultiply);
		EditorOnlyData->Specular.Expression = SpecularMultiply;
	}
	else
	{
		EditorOnlyData->Specular.Expression = AddScalar(Material, EditorOnlyData, Specular, -350, 260);
	}
	EditorOnlyData->Roughness.Expression = AddScalar(Material, EditorOnlyData, Roughness, -350, 340);
	EditorOnlyData->Metallic.Expression = AddScalar(Material, EditorOnlyData, 0.0f, -350, 420);

	// The original CGrannyMaterial only binds Granny diffuse and opacity maps. Its renderer never
	// consumes Granny self-illumination metadata, which is commonly exported as white defaults.
	// Keep EmissiveColor disconnected so those defaults cannot wash out the diffuse texture.

	if (OpacityTexture)
	{
		Material->BlendMode = BLEND_Masked;
		// The client classifies with the opacity map, but alpha-tests diffuse alpha against zero.
		Material->OpacityMaskClipValue = 0.001f;
		if (BaseTextureSample)
		{
			EditorOnlyData->OpacityMask.Expression = BaseTextureSample;
			EditorOnlyData->OpacityMask.OutputIndex = 4;
			EditorOnlyData->OpacityMask.Mask = 1;
			EditorOnlyData->OpacityMask.MaskR = 0;
			EditorOnlyData->OpacityMask.MaskG = 0;
			EditorOnlyData->OpacityMask.MaskB = 0;
			EditorOnlyData->OpacityMask.MaskA = 1;
		}
		else
		{
			EditorOnlyData->OpacityMask.Expression = AddScalar(Material, EditorOnlyData, 1.0f, -350, 680);
			EditorOnlyData->OpacityMask.OutputIndex = 0;
			EditorOnlyData->OpacityMask.Mask = 0;
			EditorOnlyData->OpacityMask.MaskR = 0;
			EditorOnlyData->OpacityMask.MaskG = 0;
			EditorOnlyData->OpacityMask.MaskB = 0;
			EditorOnlyData->OpacityMask.MaskA = 0;
		}
	}
	else if (Slot.Opacity < 0.999f)
	{
		Material->BlendMode = BLEND_Translucent;
		EditorOnlyData->Opacity.Expression = AddScalar(Material, EditorOnlyData, Slot.Opacity, -350, 680);
	}

	if (BumpTexture && IsNormalTextureReference(Slot.BumpTextureReference))
	{
		BumpTexture->Modify();
		BumpTexture->SRGB = false;
		BumpTexture->CompressionSettings = TC_Normalmap;
		BumpTexture->PostEditChange();
		EditorOnlyData->Normal.Expression = AddTextureSample(Material, EditorOnlyData, BumpTexture, TEXT("Normal"), -350, 820, SAMPLERTYPE_Normal);
	}

	Material->PostEditChange();
	Package->MarkPackageDirty();
	if (bNewMaterial)
	{
		FAssetRegistryModule::AssetCreated(Material);
	}
	OutResult.CreatedPackages.AddUnique(MaterialObjectPath);
	if (Context.bEnableDebugLogs)
	{
		OutResult.AddInfo(FString::Printf(TEXT("[Debug][GrannyMaterial] Slot='%s' Material='%s' Diffuse='%s' SpecularMap='%s' Opacity='%s' Bump='%s' Reflection='%s' DiffuseColor=(%.3f,%.3f,%.3f) SpecularColor=(%.3f,%.3f,%.3f) Shininess=%.3f Strength=%.3f UESpecular=%.3f UERoughness=%.3f OpacityValue=%.3f TwoSided=%s"),
			*Slot.SlotName,
			*MaterialObjectPath,
			*Slot.DiffuseTextureReference,
			*Slot.SpecularTextureReference,
			*Slot.OpacityTextureReference,
			*Slot.BumpTextureReference,
			*Slot.ReflectionTextureReference,
			Slot.DiffuseColor.R, Slot.DiffuseColor.G, Slot.DiffuseColor.B,
			Slot.SpecularColor.R, Slot.SpecularColor.G, Slot.SpecularColor.B,
			Slot.Shininess,
			Slot.ShininessStrength,
			Specular,
			Roughness,
			Slot.Opacity,
			TEXT("true")), Record.AbsolutePath);
	}
	return Material;
}

void FMT2StaticMeshImporter::CreateAndAssignMaterials(const FMT2ImportContext& Context, const FMT2AssetRecord& Record, const TArray<FMT2GrannyMaterialSlot>& MaterialSlots, FMT2ImportResult& OutResult)
{
	if (MaterialSlots.Num() == 0)
	{
		OutResult.AddWarning(FString::Printf(TEXT("No Granny material slots found for %s."), *Record.VirtualPath), Record.AbsolutePath);
		return;
	}

	UStaticMesh* StaticMesh = LoadObject<UStaticMesh>(nullptr, *BuildObjectPath(Context, Record));
	if (!StaticMesh)
	{
		OutResult.AddWarning(FString::Printf(TEXT("Imported static mesh could not be loaded for material assignment: %s"), *BuildObjectPath(Context, Record)), Record.AbsolutePath);
		return;
	}

	TArray<UMaterialInterface*> CreatedMaterials;
	CreatedMaterials.Reserve(MaterialSlots.Num());

	for (int32 SlotIndex = 0; SlotIndex < MaterialSlots.Num(); ++SlotIndex)
	{
		const FMT2GrannyMaterialSlot& Slot = MaterialSlots[SlotIndex];
		CreatedMaterials.Add(CreateOrUpdateSharedMaterial(Context, Record, Slot, SlotIndex, OutResult));
	}

	TArray<FStaticMaterial>& StaticMaterials = StaticMesh->GetStaticMaterials();
	for (int32 SlotIndex = 0; SlotIndex < CreatedMaterials.Num(); ++SlotIndex)
	{
		UMaterialInterface* Material = CreatedMaterials[SlotIndex];
		if (!Material)
		{
			continue;
		}

		int32 StaticMaterialIndex = INDEX_NONE;
		const FName GrannySlotName(*MaterialSlots[SlotIndex].SlotName);
		const FName ImportedSlotName(*(MaterialSlots[SlotIndex].ImportedSlotName.IsEmpty() ? MaterialSlots[SlotIndex].SlotName : MaterialSlots[SlotIndex].ImportedSlotName));
		for (int32 Index = 0; Index < StaticMaterials.Num(); ++Index)
		{
			if (StaticMaterials[Index].MaterialSlotName == GrannySlotName ||
				StaticMaterials[Index].ImportedMaterialSlotName == GrannySlotName ||
				StaticMaterials[Index].MaterialSlotName == ImportedSlotName ||
				StaticMaterials[Index].ImportedMaterialSlotName == ImportedSlotName)
			{
				StaticMaterialIndex = Index;
				break;
			}
		}

		if (StaticMaterialIndex == INDEX_NONE && StaticMaterials.IsValidIndex(SlotIndex))
		{
			StaticMaterialIndex = SlotIndex;
		}

		if (StaticMaterialIndex != INDEX_NONE)
		{
			StaticMesh->SetMaterial(StaticMaterialIndex, Material);
		}
	}

	StaticMesh->PostEditChange();
	StaticMesh->MarkPackageDirty();
	OutResult.AddInfo(FString::Printf(TEXT("Created/assigned %d material(s) for %s."), CreatedMaterials.Num(), *Record.VirtualPath), Record.AbsolutePath);
}

TSet<FString> FMT2StaticMeshImporter::CollectMaterialObjectPathsInFolder(const FString& PackageFolder)
{
	TSet<FString> Result;
	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> Assets;
	AssetRegistryModule.Get().GetAssetsByPath(FName(*PackageFolder), Assets, true);
	for (const FAssetData& Asset : Assets)
	{
		UClass* AssetClass = Asset.GetClass();
		if (AssetClass && AssetClass->IsChildOf(UMaterialInterface::StaticClass()))
		{
			Result.Add(Asset.GetSoftObjectPath().ToString());
		}
	}
	return Result;
}

void FMT2StaticMeshImporter::DeleteUnreferencedNewMaterials(const FMT2ImportContext& Context, const FMT2AssetRecord& Record, const TSet<FString>& PreImportMaterialObjectPaths, FMT2ImportResult& OutResult)
{
	UStaticMesh* StaticMesh = LoadObject<UStaticMesh>(nullptr, *BuildObjectPath(Context, Record));
	if (!StaticMesh)
	{
		return;
	}

	TSet<FString> AssignedMaterialPaths;
	for (const FStaticMaterial& StaticMaterial : StaticMesh->GetStaticMaterials())
	{
		if (StaticMaterial.MaterialInterface)
		{
			AssignedMaterialPaths.Add(StaticMaterial.MaterialInterface->GetPathName());
		}
	}

	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> Assets;
	AssetRegistryModule.Get().GetAssetsByPath(FName(*BuildDestinationPath(Context, Record)), Assets, true);

	TArray<FAssetData> AssetsToDelete;
	for (const FAssetData& Asset : Assets)
	{
		UClass* AssetClass = Asset.GetClass();
		if (!AssetClass || !AssetClass->IsChildOf(UMaterialInterface::StaticClass()))
		{
			continue;
		}

		const FString ObjectPath = Asset.GetSoftObjectPath().ToString();
		if (PreImportMaterialObjectPaths.Contains(ObjectPath) || AssignedMaterialPaths.Contains(ObjectPath))
		{
			continue;
		}

		AssetsToDelete.Add(Asset);
	}

	if (AssetsToDelete.Num() == 0)
	{
		return;
	}

	const int32 DeletedCount = ObjectTools::DeleteAssets(AssetsToDelete, false);
	if (DeletedCount > 0)
	{
		OutResult.AddInfo(FString::Printf(TEXT("Deleted %d unreferenced material asset(s) created during import for %s."), DeletedCount, *Record.VirtualPath), Record.AbsolutePath);
	}
}
