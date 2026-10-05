/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Importers/MT2AudioImporter.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"

FMT2AudioImporter::FMT2AudioImporter()
	: FMT2ImporterBase(EMT2ImportDomain::Audio, TEXT("AudioImporter"))
{
}

bool FMT2AudioImporter::Discover(const FMT2ImportContext& Context, const FMT2AssetScanResult& ScanResult, FMT2ImportDiscovery& OutDiscovery) const
{
	OutDiscovery.Domain = GetDomain();
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	for (const FMT2AssetRecord& Record : ScanResult.GetRecordsByKind(EMT2AssetKind::Audio))
	{
		const FString ObjectPath = BuildObjectPath(Context, Record);
		if (Context.bReplaceExisting || !AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(ObjectPath)).IsValid())
		{
			OutDiscovery.AssetRecords.Add(Record);
		}
	}
	OutDiscovery.ItemsDiscovered = OutDiscovery.AssetRecords.Num();
	for (const FMT2AssetRecord& Record : OutDiscovery.AssetRecords)
	{
		OutDiscovery.EntryNames.Add(Record.VirtualPath.IsEmpty() ? Record.ContentPath : Record.VirtualPath);
	}
	return true;
}

bool FMT2AudioImporter::Import(const FMT2ImportRequest& Request, FMT2ImportResult& OutResult)
{
	if (!CanImport(Request, OutResult))
	{
		return false;
	}

	if (Request.Selection.AssetRecords.Num() == 0)
	{
		OutResult.AddWarning(TEXT("No audio records were selected."));
		OutResult.bSucceeded = true;
		return true;
	}

	TArray<UAssetImportTask*> Tasks;
	const int32 MaxItems = Request.MaxItems > 0 ? Request.MaxItems : Request.Selection.AssetRecords.Num();
	int32 ConsideredCount = 0;
	FScopedSlowTask AudioProgress(
		static_cast<float>(FMath::Max(1, MaxItems * 2)),
		NSLOCTEXT("FMT2AudioImporter", "ImportSoundsProgress", "Importing sounds..."));
	AudioProgress.MakeDialog(true);
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	for (const FMT2AssetRecord& Record : Request.Selection.AssetRecords)
	{
		if (ConsideredCount >= MaxItems)
		{
			break;
		}
		AudioProgress.EnterProgressFrame(1.0f, FText::Format(
			NSLOCTEXT("FMT2AudioImporter", "CheckSoundProgressFormat", "Checking sound {0} of {1}: {2}"),
			FText::AsNumber(ConsideredCount + 1),
			FText::AsNumber(MaxItems),
			FText::FromString(Record.VirtualPath.IsEmpty() ? Record.ContentPath : Record.VirtualPath)));
		if (Request.Context.IsStopRequested() || AudioProgress.ShouldCancel())
		{
			OutResult.AddWarning(TEXT("Sound import stopped by user."));
			break;
		}

		ConsideredCount++;
		OutResult.ItemsDiscovered++;

		if (Record.Kind != EMT2AssetKind::Audio)
		{
			OutResult.ItemsSkipped++;
			OutResult.AddWarning(FString::Printf(TEXT("Skipped non-audio record: %s"), *Record.VirtualPath), Record.AbsolutePath);
			continue;
		}

		if (!IFileManager::Get().FileExists(*Record.AbsolutePath))
		{
			OutResult.ItemsSkipped++;
			OutResult.AddWarning(FString::Printf(TEXT("Skipped missing audio file: %s"), *Record.AbsolutePath), Record.AbsolutePath);
			continue;
		}

		const FString ObjectPath = BuildObjectPath(Request.Context, Record);
		if (!Request.Context.bReplaceExisting && AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(ObjectPath)).IsValid())
		{
			OutResult.ItemsSkipped++;
			continue;
		}

		OutResult.CreatedPackages.Add(ObjectPath);
		if (Request.Context.bDryRun)
		{
			OutResult.ItemsSkipped++;
			continue;
		}

		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->AddToRoot();
		Task->Filename = Record.AbsolutePath;
		Task->DestinationPath = BuildDestinationPath(Request.Context, Record);
		Task->DestinationName = FPackageName::ObjectPathToObjectName(ObjectPath);
		Task->bAutomated = true;
		Task->bSave = false;
		Task->bReplaceExisting = Request.Context.bReplaceExisting;
		Task->Factory = nullptr;
		Tasks.Add(Task);
	}

	int32 ImportedCount = 0;
	if (Tasks.Num() > 0 && !Request.Context.bDryRun)
	{
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
		constexpr int32 ImportBatchSize = 32;
		for (int32 BatchStart = 0; BatchStart < Tasks.Num(); BatchStart += ImportBatchSize)
		{
			if (Request.Context.IsStopRequested() || AudioProgress.ShouldCancel())
			{
				OutResult.AddWarning(TEXT("Sound import stopped by user."));
				break;
			}

			const int32 BatchCount = FMath::Min(ImportBatchSize, Tasks.Num() - BatchStart);
			AudioProgress.EnterProgressFrame(static_cast<float>(BatchCount), FText::Format(
				NSLOCTEXT("FMT2AudioImporter", "CreateSoundsProgressFormat", "Creating sounds {0}-{1} of {2}..."),
				FText::AsNumber(BatchStart + 1),
				FText::AsNumber(BatchStart + BatchCount),
				FText::AsNumber(Tasks.Num())));

			TArray<UAssetImportTask*> Batch;
			Batch.Reserve(BatchCount);
			for (int32 BatchIndex = 0; BatchIndex < BatchCount; ++BatchIndex)
			{
				Batch.Add(Tasks[BatchStart + BatchIndex]);
			}
			AssetToolsModule.Get().ImportAssetTasks(Batch);

			for (UAssetImportTask* Task : Batch)
			{
				if (Task->ImportedObjectPaths.Num() > 0)
				{
					ImportedCount++;
				}
				else
				{
					OutResult.ItemsSkipped++;
					OutResult.AddError(FString::Printf(TEXT("Sound import failed: %s"), *Task->Filename), Task->Filename);
				}
			}
		}
	}

	for (UAssetImportTask* Task : Tasks)
	{
		Task->RemoveFromRoot();
	}

	OutResult.ItemsImported = ImportedCount;
	OutResult.AddInfo(Request.Context.bDryRun
		? FString::Printf(TEXT("Dry run: prepared %d audio import task(s)."), OutResult.CreatedPackages.Num())
		: FString::Printf(TEXT("Submitted %d audio import task(s)."), Tasks.Num()));
	OutResult.bSucceeded = !OutResult.HasErrors();
	return OutResult.bSucceeded;
}

FString FMT2AudioImporter::BuildDestinationPath(const FMT2ImportContext& Context, const FMT2AssetRecord& Record)
{
	return FMT2AssetScanner::BuildContentPackagePath(Context.DestinationRoot, Record);
}

FString FMT2AudioImporter::BuildObjectPath(const FMT2ImportContext& Context, const FMT2AssetRecord& Record)
{
	const FString AssetName = FMT2AssetScanner::SanitizePackagePathSegment(FPaths::GetBaseFilename(Record.ContentPath));
	const FString PackagePath = BuildDestinationPath(Context, Record) / AssetName;
	return PackagePath + TEXT(".") + AssetName;
}
