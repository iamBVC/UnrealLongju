#include "MT2ConfigureItemBehaviorsCommandlet.h"
#include "MT2TimedConsumableMigration.h"
#include "Items/Templates/MT2ItemUseAbilityUpTemplate.h"
#include "Items/Templates/MT2ItemUseAffectTemplate.h"
#include "Items/Templates/MT2ItemUseDewTemplate.h"
#include "Items/Templates/MT2ItemUseClearTemplate.h"
#include "Items/Templates/MT2ItemAutoRecoveryTemplate.h"

#include "Config/MT2PathSettings.h"
#include "Importers/MT2ItemImporter.h"
#include "Misc/Parse.h"
#include "Engine/Blueprint.h"
#include "Items/MT2ItemTemplate.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/SavePackage.h"

namespace
{
	int32 UpdateAutoRecoveryResources(const TArray<FMT2ItemImportRecord>& Records,
		const FString& DestinationRoot, const FString& Params)
	{
		struct FUpdate
		{
			UMT2ItemAutoRecoveryTemplate* Defaults;
			UBlueprint* Blueprint;
			FString Filename;
			EMT2AutoRecoveryResource Resource;
		};

		TArray<FUpdate> Updates;
		int32 Checked = 0;
		for (const FMT2ItemImportRecord& Record : Records)
		{
			const auto Resource = FMT2ItemImporter::ResolveLegacyAutoRecoveryResource(Record.Definition);
			if (!Resource.IsSet())
			{
				continue;
			}

			const FString ObjectPath = FMT2ItemImporter::BuildBlueprintObjectPath(DestinationRoot, Record.Definition);
			const FString PackageName = FPackageName::ObjectPathToPackageName(ObjectPath);
			FString Filename;
			if (!FPackageName::DoesPackageExist(PackageName, &Filename))
			{
				continue;
			}

			UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
			auto* Defaults = Blueprint && Blueprint->GeneratedClass
				? Cast<UMT2ItemAutoRecoveryTemplate>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
			if (!Defaults || Defaults->Vnum != Record.Definition.Vnum || Blueprint->GetOutermost()->IsDirty())
			{
				UE_LOG(LogTemp, Error, TEXT("Unexpected or dirty reservoir package: %s"), *ObjectPath);
				return 1;
			}

			++Checked;
			if (Defaults->Resource != Resource.GetValue())
			{
				Updates.Add({Defaults, Blueprint, MoveTemp(Filename), Resource.GetValue()});
				UE_LOG(LogTemp, Display, TEXT("Reservoir %d: %s -> %s"), Defaults->Vnum,
					*StaticEnum<EMT2AutoRecoveryResource>()->GetNameStringByValue(static_cast<int64>(Defaults->Resource)),
					*StaticEnum<EMT2AutoRecoveryResource>()->GetNameStringByValue(static_cast<int64>(Resource.GetValue())));
			}
		}

		UE_LOG(LogTemp, Display, TEXT("Reservoir packages checked: %d; resource changes needed: %d"), Checked, Updates.Num());
		if (Checked == 0)
		{
			return 1;
		}
		if (Updates.IsEmpty() || FParse::Param(*Params, TEXT("Preview")))
		{
			return 0;
		}

		FString BackupRoot = FPaths::ProjectSavedDir() / TEXT("ItemMigrations/AutoRecoveryResources")
			/ FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"));
		FParse::Value(*Params, TEXT("BackupRoot="), BackupRoot);
		IFileManager& Files = IFileManager::Get();
		Files.MakeDirectory(*BackupRoot, true);
		for (const FUpdate& Update : Updates)
		{
			const FString Backup = BackupRoot / FPaths::GetCleanFilename(Update.Filename);
			if (Files.Copy(*Backup, *Update.Filename, false) != COPY_OK)
			{
				UE_LOG(LogTemp, Error, TEXT("Cannot create original package backup: %s"), *Backup);
				return 1;
			}
		}
		UE_LOG(LogTemp, Display, TEXT("Original reservoir packages backed up to %s"), *BackupRoot);

		for (const FUpdate& Update : Updates)
		{
			Update.Defaults->Resource = Update.Resource;
			FSavePackageArgs Args;
			Args.TopLevelFlags = RF_Public | RF_Standalone;
			if (!UPackage::SavePackage(Update.Blueprint->GetOutermost(), Update.Blueprint, *Update.Filename, Args))
			{
				UE_LOG(LogTemp, Error, TEXT("Cannot save reservoir package: %s"), *Update.Filename);
				return 1;
			}
		}
		return 0;
	}
}

UMT2ConfigureItemBehaviorsCommandlet::UMT2ConfigureItemBehaviorsCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UMT2ConfigureItemBehaviorsCommandlet::Main(const FString& Params)
{
	FString SourceRoot = UMT2PathSettings::Path(TEXT("LegacyDumpRoot"));
	FString DestinationRoot = TEXT("/Game");
	FParse::Value(*Params, TEXT("SourceRoot="), SourceRoot);
	FParse::Value(*Params, TEXT("DestinationRoot="), DestinationRoot);
	TArray<FMT2ItemImportRecord> Records;
	TArray<FString> Warnings;
	FString Error;
	if (!FMT2ItemImporter::Discover(SourceRoot, DestinationRoot, Records, Warnings, Error))
	{
		UE_LOG(LogTemp, Error, TEXT("Item behavior discovery failed: %s"), *Error);
		return 1;
	}
	for (const auto& Warning : Warnings) UE_LOG(LogTemp, Display, TEXT("%s"), *Warning);
	const bool bBeginnerBonusesOnly = FParse::Param(*Params, TEXT("BeginnerBonusesOnly"));
	const bool bInstantPotionsOnly = FParse::Param(*Params, TEXT("InstantPotionsOnly"));
	const bool bRecoveryPotionsOnly = FParse::Param(*Params, TEXT("RecoveryPotionsOnly"));
	const bool bTimedConsumablesOnly = FParse::Param(*Params, TEXT("TimedConsumablesOnly"));
	const bool bAutoRecoveryResourcesOnly = FParse::Param(*Params, TEXT("AutoRecoveryResourcesOnly"));
	if (static_cast<int32>(bBeginnerBonusesOnly) + static_cast<int32>(bInstantPotionsOnly) + static_cast<int32>(bRecoveryPotionsOnly) + static_cast<int32>(bTimedConsumablesOnly) + static_cast<int32>(bAutoRecoveryResourcesOnly) > 1)
	{
		UE_LOG(LogTemp, Error, TEXT("Choose only one item behavior filter."));
		return 1;
	}
	if (bAutoRecoveryResourcesOnly)
	{
		return UpdateAutoRecoveryResources(Records, DestinationRoot, Params);
	}
	Records.RemoveAll([bBeginnerBonusesOnly, bInstantPotionsOnly, bRecoveryPotionsOnly, bTimedConsumablesOnly](const FMT2ItemImportRecord& Record)
	{
		if (bTimedConsumablesOnly)
			return Record.Definition.ItemType != EMT2ItemType::Use ||
				(Record.Definition.SubType.Use != EMT2UseSubType::AbilityUp && Record.Definition.SubType.Use != EMT2UseSubType::Affect && Record.Definition.SubType.Use != EMT2UseSubType::Clear);
		if (bRecoveryPotionsOnly)
		{
			const auto Type = Record.Definition.SubType.Use;
			return Record.Definition.ItemType != EMT2ItemType::Use ||
				(Type != EMT2UseSubType::Potion && Type != EMT2UseSubType::PotionNoDelay && Type != EMT2UseSubType::PotionContinue);
		}
		if (bInstantPotionsOnly)
			return Record.Definition.ItemType != EMT2ItemType::Use || Record.Definition.SubType.Use != EMT2UseSubType::PotionNoDelay;
		if (bBeginnerBonusesOnly)
		{
			const int32 Vnum = Record.Definition.Vnum;
			return Vnum != 71151 && Vnum != 71152 && Vnum != 76023 && Vnum != 76024;
		}
		if (Record.Definition.ItemType == EMT2ItemType::Blend || Record.Definition.ItemType == EMT2ItemType::Fish ||
			Record.Definition.ItemType == EMT2ItemType::Campfire) return false;
		if (Record.Definition.ItemType != EMT2ItemType::Use) return true;
		const auto Type = Record.Definition.SubType.Use;
		return Type != EMT2UseSubType::AbilityUp && Type != EMT2UseSubType::PotionContinue && Type != EMT2UseSubType::Clear;
	});
	if (bBeginnerBonusesOnly && Records.Num() != 4)
	{
		UE_LOG(LogTemp, Error, TEXT("Expected all four beginner bonus-item definitions; refusing a partial refresh."));
		return 1;
	}
	if (Records.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("No item behavior definitions matched the source proto."));
		return 1;
	}
	UE_LOG(LogTemp, Display, TEXT("Item behavior definitions: %d"), Records.Num());
	if (FParse::Param(*Params, TEXT("Preview"))) return 0;
	if (bTimedConsumablesOnly)
	{
		int32 Migrated = 0;
		FString BackupRoot = FPaths::ProjectSavedDir() / TEXT("ItemMigrations/TimedConsumables") / FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"));
		FParse::Value(*Params, TEXT("BackupRoot="), BackupRoot);
		IFileManager::Get().MakeDirectory(*BackupRoot, true);
		for (const FMT2ItemImportRecord& Record : Records)
		{
			const FString ObjectPath = FMT2ItemImporter::BuildBlueprintObjectPath(DestinationRoot, Record.Definition);
			UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
			if (!Blueprint) { UE_LOG(LogTemp, Error, TEXT("Missing timed item %s; migration stopped."), *ObjectPath); return 1; }
			const FString Filename = FPackageName::LongPackageNameToFilename(Blueprint->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
			const FString Backup = BackupRoot / FPaths::GetCleanFilename(Filename);
			// Never replace the original backup during repeat runs.
			if (!IFileManager::Get().FileExists(*Backup) && IFileManager::Get().Copy(*Backup, *Filename) != COPY_OK)
			{
				UE_LOG(LogTemp, Error, TEXT("Cannot back up %s; migration stopped."), *Filename); return 1;
			}
			const bool bDew = Record.Definition.SubType.Use == EMT2UseSubType::AbilityUp && Record.Definition.Vnum > 50800 && Record.Definition.Vnum <= 50820;
			FString MigrationError;
			UClass* DesiredParent = bDew ? UMT2ItemUseDewTemplate::StaticClass() :
				(Record.Definition.SubType.Use == EMT2UseSubType::AbilityUp ? UMT2ItemUseAbilityUpTemplate::StaticClass() :
				(Record.Definition.SubType.Use == EMT2UseSubType::Clear ? UMT2ItemUseClearTemplate::StaticClass() : UMT2ItemUseAffectTemplate::StaticClass()));
			if (!MigrateMT2TimedConsumable(*Blueprint, DesiredParent, MigrationError))
			{
				UE_LOG(LogTemp, Error, TEXT("%s: %s"), *ObjectPath, *MigrationError); return 1;
			}
			FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone;
			if (!UPackage::SavePackage(Blueprint->GetOutermost(), Blueprint, *Filename, Args)) return 1;
			++Migrated;
		}
		UE_LOG(LogTemp, Display, TEXT("Timed consumables migrated: %d; all authored properties preserved."), Migrated);
		return 0;
	}
	if (bRecoveryPotionsOnly)
	{
		// A behavior migration must not replace authored ground visuals with newly discovered meshes.
		for (FMT2ItemImportRecord& Record : Records)
		{
			const FString Path = FMT2ItemImporter::BuildBlueprintObjectPath(DestinationRoot, Record.Definition);
			const UBlueprint* Existing = LoadObject<UBlueprint>(nullptr, *Path);
			const UMT2ItemTemplate* Defaults = Existing && Existing->GeneratedClass
				? Cast<UMT2ItemTemplate>(Existing->GeneratedClass->GetDefaultObject()) : nullptr;
			if (Defaults) Record.Definition.WorldMeshObjectPath = Defaults->WorldMesh.ToSoftObjectPath().ToString();
		}
	}
	FMT2ItemImportResult Result;
	const bool bSuccess = FMT2ItemImporter::Import(Records, DestinationRoot, [] { return false; }, Result);
	for (const auto& ImportError : Result.Errors) UE_LOG(LogTemp, Error, TEXT("%s"), *ImportError);
	UE_LOG(LogTemp, Display, TEXT("%s"), *Result.BuildSummary());
	return bSuccess ? 0 : 1;
}
