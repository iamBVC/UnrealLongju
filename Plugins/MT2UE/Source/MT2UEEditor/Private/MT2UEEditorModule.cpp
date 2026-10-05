/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "MT2UEEditorModule.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserMenuContexts.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "LevelEditor.h"
#include "MT2CharacterAnimationGenerator.h"
#include "MT2AssetOptimizer.h"
#include "MT2ItemBlueprintThumbnailRenderer.h"
#include "MT2UnreferencedAssetCleaner.h"
#include "MT2UEImporterWidget.h"
#include "MT2VnumRegistryBuilder.h"
#include "Importers/MT2QuestImporter.h"
#include "Importers/MT2SkillImporter.h"
#include "Importers/MT2MapObjectImporter.h"
#include "Misc/MessageDialog.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "Styling/AppStyle.h"
#include "ThumbnailRendering/BlueprintThumbnailRenderer.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "FMT2UEEditorModule"

namespace
{
	const FName MT2ImporterTabName(TEXT("MT2UEImporter"));
}

void FMT2UEEditorModule::StartupModule()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		MT2ImporterTabName,
		FOnSpawnTab::CreateRaw(this, &FMT2UEEditorModule::SpawnImporterTab))
		.SetDisplayName(LOCTEXT("ImporterTabTitle", "MT2UE"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	RegisterMenus();
	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(
			this, &FMT2UEEditorModule::RegisterContentBrowserMenus));
	RegisterVnumRegistryTracking();
	UThumbnailManager& ThumbnailManager = UThumbnailManager::Get();
	// RegisterCustomRenderer deliberately refuses to replace an existing class mapping. Blueprint
	// already has Unreal's renderer, so remove that exact mapping before installing our delegating one.
	ThumbnailManager.UnregisterCustomRenderer(UBlueprint::StaticClass());
	ThumbnailManager.RegisterCustomRenderer(
		UBlueprint::StaticClass(), UMT2ItemBlueprintThumbnailRenderer::StaticClass());
	if (!FMT2VnumRegistryBuilder::RegistryExists())
	{
		ScheduleVnumRegistryRebuild();
	}
}

void FMT2UEEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	if (UObjectInitialized())
	{
		// Restore Unreal's renderer so live plugin unload/reload cannot leave all Blueprints without
		// thumbnails for the remainder of the editor session.
		UThumbnailManager& ThumbnailManager = UThumbnailManager::Get();
		ThumbnailManager.UnregisterCustomRenderer(UBlueprint::StaticClass());
		ThumbnailManager.RegisterCustomRenderer(
			UBlueprint::StaticClass(), UBlueprintThumbnailRenderer::StaticClass());
	}
	UnregisterVnumRegistryTracking();
	if (FModuleManager::Get().IsModuleLoaded(TEXT("LevelEditor")) && MenuExtender.IsValid())
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		LevelEditorModule.GetMenuExtensibilityManager()->RemoveExtender(MenuExtender);
	}

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(MT2ImporterTabName);
}

void FMT2UEEditorModule::RegisterContentBrowserMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("ContentBrowser.FolderContextMenu"));
	if (!Menu)
	{
		return;
	}

	Menu->AddDynamicSection(TEXT("MT2UEFolderCleanup"),
		FNewToolMenuDelegate::CreateLambda([](UToolMenu* DynamicMenu)
		{
			const UContentBrowserFolderContext* Context =
				DynamicMenu ? DynamicMenu->FindContext<UContentBrowserFolderContext>() : nullptr;
			if (!Context || !Context->bCanBeModified || Context->GetSelectedPackagePaths().IsEmpty())
			{
				return;
			}

			const TArray<FString> SelectedPaths = Context->GetSelectedPackagePaths();
			FToolMenuSection& CleanupSection = DynamicMenu->AddSection(
				TEXT("MT2UEFolderCleanupActions"), LOCTEXT("MT2UEFolderCleanupHeading", "MT2UE"),
				FToolMenuInsert(TEXT("PathContextBulkOperations"), EToolMenuInsertType::Before));
			CleanupSection.AddMenuEntry(
				TEXT("MT2UEDeleteUnreferencedAssetsAction"),
				LOCTEXT("DeleteUnreferencedAssetsLabel", "Delete unreferenced assets"),
				LOCTEXT("DeleteUnreferencedAssetsTooltip",
					"Recursively find assets with no on-disk package referencers and open Unreal's safe deletion dialog. Maps and primary assets are excluded."),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Delete")),
				FUIAction(FExecuteAction::CreateLambda([SelectedPaths]()
				{
					FMT2UnreferencedAssetCleaner::DeleteFromFolders(SelectedPaths);
				})));
		}));
}

void FMT2UEEditorModule::RegisterMenus()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
	MenuExtender = MakeShared<FExtender>();
	MenuExtender->AddMenuExtension(
		TEXT("WindowLayout"),
		EExtensionHook::After,
		nullptr,
		FMenuExtensionDelegate::CreateRaw(this, &FMT2UEEditorModule::AddMenuEntry));

	LevelEditorModule.GetMenuExtensibilityManager()->AddExtender(MenuExtender);
}

void FMT2UEEditorModule::AddMenuEntry(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.AddMenuEntry(
		LOCTEXT("OpenImporterLabel", "MT2UE Importer"),
		LOCTEXT("OpenImporterTooltip", "Open the Metin2 asset importer."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(MT2ImporterTabName);
		}))); 

	MenuBuilder.AddMenuEntry(
		LOCTEXT("ReimportSkillsLabel", "Reimport Skills"),
		LOCTEXT("ReimportSkillsTooltip",
			"Refresh skill definitions and skill sets from locale/en skilltable.txt and skilldesc.txt."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FMT2UEEditorModule::ReimportSkills)));

	MenuBuilder.AddMenuEntry(
		LOCTEXT("GeneratePlayerAnimBPLabel", "Generate Player Animation Blueprints"),
		LOCTEXT("GeneratePlayerAnimBPTooltip", "Import missing base player animations and generate race/gender Animation Blueprints."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FMT2UEEditorModule::GeneratePlayerAnimationBlueprints)));

	MenuBuilder.AddMenuEntry(
		LOCTEXT("ImportQuestsLabel", "Import Quests"),
		LOCTEXT("ImportQuestsTooltip",
			"Convert the old server's Lua .quest scripts into quest Blueprints under /Game/Quests."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FMT2UEEditorModule::ImportQuests)));

	MenuBuilder.AddMenuEntry(
		LOCTEXT("RebuildVnumRegistryLabel", "Rebuild VNUM Registry"),
		LOCTEXT("RebuildVnumRegistryTooltip", "Validate mob and item VNUMs and rebuild /Game/Logic/DA_MT2VnumRegistry."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FMT2UEEditorModule::RebuildVnumRegistry)));

	MenuBuilder.AddMenuEntry(
		LOCTEXT("OptimizeImportedAssetsLabel", "Optimize Imported Assets"),
		LOCTEXT("OptimizeImportedAssetsTooltip", "Generate mesh LODs, configure texture mip streaming, and update current-map cull distances."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FMT2UEEditorModule::OptimizeImportedAssets)));

	MenuBuilder.AddMenuEntry(
		LOCTEXT("RepairImportedMapActorsLabel", "Repair Imported Map Actors"),
		LOCTEXT("RepairImportedMapActorsTooltip",
			"Remove broken imported mesh actors and exact placement duplicates from the current map."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FMT2UEEditorModule::RepairImportedMapActors)));
}

void FMT2UEEditorModule::ImportQuests()
{
	FScopedSlowTask Progress(1.0f, LOCTEXT("ImportQuestsProgress", "Converting Metin2 quest scripts..."));
	Progress.MakeDialog(true);
	Progress.EnterProgressFrame(1.0f, LOCTEXT("ImportQuestsReading", "Translating .quest scripts"));

	FMT2QuestImportResult Result;
	const bool bSucceeded = FMT2QuestImporter::Import(
		TEXT("D:/Giochi/Metin2/Development/my_server/server_src/share/locale/italy/quest"),
		TEXT("/Game"), Result,
		TEXT("D:/Giochi/Metin2/Development/Dumps/my_dump"));

	FString Message = Result.BuildSummary();
	if (Result.StatementsUnconverted > 0)
	{
		Message += FString::Printf(
			TEXT("\n\n%d statement(s) could not be translated and were kept as TODO nodes carrying the ")
			TEXT("original Lua. See Saved/MT2QuestConversionReport.txt."),
			Result.StatementsUnconverted);
	}
	if (!Result.Warnings.IsEmpty())
	{
		const int32 VisibleWarningCount = FMath::Min(Result.Warnings.Num(), 15);
		Message += TEXT("\n\nWarnings:\n");
		for (int32 Index = 0; Index < VisibleWarningCount; ++Index)
		{
			Message += Result.Warnings[Index] + TEXT("\n");
		}
	}
	if (!Result.Errors.IsEmpty())
	{
		Message += TEXT("\n\nErrors:\n") + FString::Join(Result.Errors, TEXT("\n"));
	}

	for (const FString& Warning : Result.Warnings)
	{
		UE_LOG(LogTemp, Warning, TEXT("MT2 quest import: %s"), *Warning);
	}
	for (const FString& Error : Result.Errors)
	{
		UE_LOG(LogTemp, Error, TEXT("MT2 quest import: %s"), *Error);
	}
	UE_LOG(LogTemp, Display, TEXT("MT2 quest import: %s"), *Result.BuildSummary());

	FMessageDialog::Open(
		EAppMsgType::Ok, FText::FromString(Message),
		bSucceeded
			? LOCTEXT("ImportQuestsCompleteTitle", "Quest Import Complete")
			: LOCTEXT("ImportQuestsFailedTitle", "Quest Import Failed"));
}

void FMT2UEEditorModule::ReimportSkills()
{
	FScopedSlowTask Progress(
		1.0f, LOCTEXT("ReimportSkillsProgress", "Reimporting Metin2 skills..."));
	Progress.MakeDialog(true);
	Progress.EnterProgressFrame(
		1.0f, LOCTEXT("ReimportSkillsReading", "Refreshing skill definitions and sets"));

	FMT2SkillImportResult Result;
	const bool bSucceeded = FMT2SkillImporter::Import(
		TEXT("D:/Giochi/Metin2/Development/Dumps/my_dump"), TEXT("/Game"), Result);

	FString Message = Result.BuildSummary();
	if (!Result.Warnings.IsEmpty())
	{
		const int32 VisibleWarningCount = FMath::Min(Result.Warnings.Num(), 20);
		Message += TEXT("\n\nWarnings:\n");
		for (int32 Index = 0; Index < VisibleWarningCount; ++Index)
		{
			Message += Result.Warnings[Index] + TEXT("\n");
		}
		if (VisibleWarningCount < Result.Warnings.Num())
		{
			Message += FString::Printf(
				TEXT("... and %d more warning(s). See Output Log."),
				Result.Warnings.Num() - VisibleWarningCount);
		}
	}
	if (!Result.Errors.IsEmpty())
	{
		Message += TEXT("\n\nErrors:\n") + FString::Join(Result.Errors, TEXT("\n"));
	}

	for (const FString& Warning : Result.Warnings)
	{
		UE_LOG(LogTemp, Warning, TEXT("MT2 skill reimport: %s"), *Warning);
	}
	for (const FString& Error : Result.Errors)
	{
		UE_LOG(LogTemp, Error, TEXT("MT2 skill reimport: %s"), *Error);
	}
	UE_LOG(LogTemp, Display, TEXT("MT2 skill reimport: %s"), *Result.BuildSummary());

	FMessageDialog::Open(
		EAppMsgType::Ok,
		FText::FromString(Message),
		bSucceeded
			? LOCTEXT("ReimportSkillsCompleteTitle", "Skill Reimport Complete")
			: LOCTEXT("ReimportSkillsFailedTitle", "Skill Reimport Failed"));
}

void FMT2UEEditorModule::OptimizeImportedAssets()
{
	FMT2AssetOptimizationResult Result;
	FMT2AssetOptimizer::OptimizeImportedAssets(Result);
	FString Message = Result.BuildSummary();
	if (!Result.Warnings.IsEmpty())
	{
		Message += TEXT("\n\n") + FString::Join(Result.Warnings, TEXT("\n"));
	}
	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
}

void FMT2UEEditorModule::RepairImportedMapActors()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	ULevel* Level = World ? World->GetCurrentLevel() : nullptr;
	int32 BrokenCount = 0;
	int32 DuplicateCount = 0;
	TArray<FString> Warnings;
	const int32 RemovedCount = FMT2MapObjectImporter::RepairImportedActors(
		World, Level, BrokenCount, DuplicateCount, Warnings);

	FString Message = FString::Printf(
		TEXT("Removed %d imported map actor(s).\nBroken: %d\nDuplicates: %d"),
		RemovedCount, BrokenCount, DuplicateCount);
	if (!Warnings.IsEmpty())
	{
		Message += TEXT("\n\nWarnings:\n") + FString::Join(Warnings, TEXT("\n"));
	}
	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
}

void FMT2UEEditorModule::GeneratePlayerAnimationBlueprints()
{
	FMT2CharacterAnimationGenerationResult Result;
	const bool bSucceeded = FMT2CharacterAnimationGenerator::Generate(
		TEXT("D:/Giochi/Metin2/Development/Dumps/my_dump"), true, Result);
	FString Message = Result.BuildSummary();
	if (!Result.Errors.IsEmpty())
	{
		Message += TEXT("\n\n") + FString::Join(Result.Errors, TEXT("\n"));
	}
	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
}

void FMT2UEEditorModule::RebuildVnumRegistry()
{
	TArray<FString> Errors;
	int32 MobCount = 0;
	int32 ItemCount = 0;
	const bool bSucceeded = FMT2VnumRegistryBuilder::RebuildAndSave(
		Errors, MobCount, ItemCount);
	const FString Message = bSucceeded
		? FString::Printf(TEXT("VNUM registry saved. Mobs: %d | Items: %d"), MobCount, ItemCount)
		: TEXT("VNUM registry not changed because validation failed:\n\n") + FString::Join(Errors, TEXT("\n"));
	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
}

void FMT2UEEditorModule::RegisterVnumRegistryTracking()
{
	FAssetRegistryModule& Module =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	AssetAddedHandle = Module.Get().OnAssetAdded().AddRaw(
		this, &FMT2UEEditorModule::HandleTrackedAssetChanged);
	AssetUpdatedHandle = Module.Get().OnAssetUpdated().AddRaw(
		this, &FMT2UEEditorModule::HandleTrackedAssetChanged);
	AssetRemovedHandle = Module.Get().OnAssetRemoved().AddRaw(
		this, &FMT2UEEditorModule::HandleTrackedAssetChanged);
	AssetRenamedHandle = Module.Get().OnAssetRenamed().AddRaw(
		this, &FMT2UEEditorModule::HandleTrackedAssetRenamed);
	if (GEditor)
	{
		BlueprintPreCompileHandle = GEditor->OnBlueprintPreCompile().AddRaw(
			this, &FMT2UEEditorModule::HandleBlueprintPreCompile);
		BlueprintCompiledHandle = GEditor->OnBlueprintCompiled().AddRaw(
			this, &FMT2UEEditorModule::HandleBlueprintCompiled);
	}
	RegistryTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FMT2UEEditorModule::TickVnumRegistry));
}

void FMT2UEEditorModule::UnregisterVnumRegistryTracking()
{
	if (FModuleManager::Get().IsModuleLoaded(TEXT("AssetRegistry")))
	{
		FAssetRegistryModule& Module =
			FModuleManager::GetModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		Module.Get().OnAssetAdded().Remove(AssetAddedHandle);
		Module.Get().OnAssetUpdated().Remove(AssetUpdatedHandle);
		Module.Get().OnAssetRemoved().Remove(AssetRemovedHandle);
		Module.Get().OnAssetRenamed().Remove(AssetRenamedHandle);
	}
	if (GEditor)
	{
		GEditor->OnBlueprintPreCompile().Remove(BlueprintPreCompileHandle);
		GEditor->OnBlueprintCompiled().Remove(BlueprintCompiledHandle);
	}
	if (RegistryTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(RegistryTickerHandle);
	}
}

void FMT2UEEditorModule::ScheduleVnumRegistryRebuild()
{
	bRegistryRebuildPending = true;
	RegistryRebuildAtSeconds = FPlatformTime::Seconds() + 1.0;
}

void FMT2UEEditorModule::HandleTrackedAssetChanged(const FAssetData& AssetData)
{
	if (IsTrackedAsset(AssetData))
	{
		ScheduleVnumRegistryRebuild();
	}
}

void FMT2UEEditorModule::HandleTrackedAssetRenamed(
	const FAssetData& AssetData, const FString& OldObjectPath)
{
	if (IsTrackedAsset(AssetData) || OldObjectPath.StartsWith(TEXT("/Game/Mobs/Blueprints/")) ||
		OldObjectPath.StartsWith(TEXT("/Game/Items/Blueprints/")))
	{
		ScheduleVnumRegistryRebuild();
	}
}

void FMT2UEEditorModule::HandleBlueprintPreCompile(UBlueprint* Blueprint)
{
	bTrackedBlueprintCompiling = IsTrackedBlueprint(Blueprint);
}

void FMT2UEEditorModule::HandleBlueprintCompiled()
{
	if (bTrackedBlueprintCompiling)
	{
		ScheduleVnumRegistryRebuild();
		bTrackedBlueprintCompiling = false;
	}
}

bool FMT2UEEditorModule::TickVnumRegistry(float DeltaSeconds)
{
	if (!bRegistryRebuildPending || FPlatformTime::Seconds() < RegistryRebuildAtSeconds)
	{
		return true;
	}
	bRegistryRebuildPending = false;
	TArray<FString> Errors;
	int32 MobCount = 0;
	int32 ItemCount = 0;
	if (!FMT2VnumRegistryBuilder::RebuildAndSave(Errors, MobCount, ItemCount))
	{
		for (const FString& Error : Errors)
		{
			UE_LOG(LogTemp, Error, TEXT("MT2 VNUM registry: %s"), *Error);
		}
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("MT2 VNUM registry saved: %d mobs, %d items."),
			MobCount, ItemCount);
	}
	return true;
}

bool FMT2UEEditorModule::IsTrackedBlueprint(const UBlueprint* Blueprint) const
{
	if (!Blueprint)
	{
		return false;
	}
	const FString PackageName = Blueprint->GetOutermost()->GetName();
	return PackageName.StartsWith(TEXT("/Game/Mobs/Blueprints/")) ||
		PackageName.StartsWith(TEXT("/Game/Items/Blueprints/"));
}

bool FMT2UEEditorModule::IsTrackedAsset(const FAssetData& AssetData) const
{
	const FString PackageName = AssetData.PackageName.ToString();
	return PackageName.StartsWith(TEXT("/Game/Mobs/Blueprints/")) ||
		PackageName.StartsWith(TEXT("/Game/Items/Blueprints/"));
}

TSharedRef<SDockTab> FMT2UEEditorModule::SpawnImporterTab(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SMT2UEImporterWidget)
		];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMT2UEEditorModule, MT2UEEditor)
