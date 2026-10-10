#include "MT2ApplyMetinSmokeCommandlet.h"
#include "Config/MT2PathSettings.h"
#include "Importers/MT2MobImporter.h"
#include "Mobs/MT2MetinStone.h"
#include "Engine/Blueprint.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"

UMT2ApplyMetinSmokeCommandlet::UMT2ApplyMetinSmokeCommandlet()
{
	IsClient = false; IsServer = false; IsEditor = true; LogToConsole = true;
}

int32 UMT2ApplyMetinSmokeCommandlet::Main(const FString& Params)
{
	FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().SearchAllAssets(true);
	const FString SourceRoot = UMT2PathSettings::Path(TEXT("LegacyDumpRoot"));
	const FString DestinationRoot = UMT2PathSettings::Path(TEXT("ImportDestinationRoot"));
	TArray<FMT2MobImportRecord> Records; TArray<FString> Warnings; FString Error;
	if (!FMT2MobImporter::Discover(SourceRoot, DestinationRoot, Records, Warnings, Error, true))
	{
		UE_LOG(LogTemp, Error, TEXT("[MetinSmoke] %s"), *Error); return 1;
	}
	int32 Assigned = 0, Failed = 0;
	for (const auto& Record : Records)
	{
		if (Record.Definition.Type != EMT2MobType::Stone) { continue; }
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Record.BlueprintObjectPath);
		auto* Stone = Blueprint && Blueprint->GeneratedClass ? Cast<AMT2MetinStone>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
		if (!Stone) { continue; }
		if (!FMT2MobImporter::ConfigureMetinSmoke(Stone, Record.SourceScriptPath, DestinationRoot, Error))
		{
			UE_LOG(LogTemp, Error, TEXT("[MetinSmoke] %d: %s"), Record.Definition.Vnum, *Error); ++Failed; continue;
		}
		UPackage* Package = Blueprint->GetOutermost(); Package->MarkPackageDirty();
		FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone;
		const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		if (!UPackage::SavePackage(Package, Blueprint, *Filename, Args)) { ++Failed; continue; }
		++Assigned;
		UE_LOG(LogTemp, Display, TEXT("[MetinSmoke] %d: %d stages, %d ambient effects"),
			Record.Definition.Vnum, Stone->SmokeStages.Num(), Stone->AmbientSmoke.Num());
	}
	UE_LOG(LogTemp, Display, TEXT("[MetinSmoke] Assigned %d stones, failed %d."), Assigned, Failed);
	return Failed ? 1 : 0;
}
