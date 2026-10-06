/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "MT2VnumRegistryBuilder.h"
#include "Config/MT2PathSettings.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Core/MT2VnumRegistry.h"
#include "Engine/Blueprint.h"
#include "FileHelpers.h"
#include "Items/MT2ItemTemplate.h"
#include "Mobs/MT2Mob.h"
#include "Modules/ModuleManager.h"

namespace
{
	const TCHAR* RegistryPackageName() { return UMT2PathSettings::Path(TEXT("VnumRegistryPackage")); }
	const TCHAR* RegistryObjectPath() { return UMT2PathSettings::Path(TEXT("VnumRegistry")); }

	bool AddMob(
		TMap<int32, TSoftClassPtr<AMT2Mob>>& Entries,
		TMap<int32, FString>& Sources,
		UClass* Class,
		const FString& Source,
		TArray<FString>& OutErrors)
	{
		const AMT2Mob* Defaults = Class ? Class->GetDefaultObject<AMT2Mob>() : nullptr;
		const int32 Vnum = Defaults ? Defaults->GetMobVnum() : 0;
		if (Vnum <= 0)
		{
			OutErrors.Add(FString::Printf(TEXT("Mob Blueprint has no valid VNUM: %s"), *Source));
			return false;
		}
		if (const FString* Existing = Sources.Find(Vnum))
		{
			OutErrors.Add(FString::Printf(
				TEXT("Duplicate mob VNUM %d: %s and %s"), Vnum, **Existing, *Source));
			return false;
		}
		Sources.Add(Vnum, Source);
		Entries.Add(Vnum, TSoftClassPtr<AMT2Mob>(Class));
		return true;
	}

	bool AddItem(
		TMap<int32, TSoftClassPtr<UMT2ItemTemplate>>& Entries,
		TMap<int32, FString>& Sources,
		UClass* Class,
		const FString& Source,
		TArray<FString>& OutErrors)
	{
		const UMT2ItemTemplate* Defaults = Class
			? Class->GetDefaultObject<UMT2ItemTemplate>() : nullptr;
		const int32 Vnum = Defaults ? Defaults->Vnum : 0;
		if (Vnum <= 0)
		{
			OutErrors.Add(FString::Printf(TEXT("Item Blueprint has no valid VNUM: %s"), *Source));
			return false;
		}
		if (const FString* Existing = Sources.Find(Vnum))
		{
			OutErrors.Add(FString::Printf(
				TEXT("Duplicate item VNUM %d: %s and %s"), Vnum, **Existing, *Source));
			return false;
		}
		Sources.Add(Vnum, Source);
		Entries.Add(Vnum, TSoftClassPtr<UMT2ItemTemplate>(Class));
		return true;
	}
}

bool FMT2VnumRegistryBuilder::RegistryExists()
{
	return LoadObject<UMT2VnumRegistry>(nullptr, RegistryObjectPath()) != nullptr;
}

bool FMT2VnumRegistryBuilder::RebuildAndSave(
	TArray<FString>& OutErrors, int32& OutMobCount, int32& OutItemCount)
{
	OutErrors.Reset();
	OutMobCount = 0;
	OutItemCount = 0;

	FARFilter Filter;
	// NPCs and metin stones are mob_proto rows too (just AMT2Npc / AMT2MetinStone subclasses), so
	// they belong in the vnum registry alongside monsters - that is what lets "/m <vnum>" spawn them
	// and lets metin waves resolve their group members.
	Filter.PackagePaths.Add(UMT2PathSettings::Path(TEXT("Mobs_Blueprints")));
	Filter.PackagePaths.Add(UMT2PathSettings::Path(TEXT("Npcs_Blueprints")));
	Filter.PackagePaths.Add(UMT2PathSettings::Path(TEXT("Metins_Blueprints")));
	Filter.PackagePaths.Add(UMT2PathSettings::Path(TEXT("Items_Blueprints")));
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.bRecursivePaths = true;
	TArray<FAssetData> Assets;
	FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"))
		.Get().GetAssets(Filter, Assets);
	Assets.Sort([](const FAssetData& A, const FAssetData& B)
	{
		return A.GetSoftObjectPath().ToString() < B.GetSoftObjectPath().ToString();
	});

	TMap<int32, TSoftClassPtr<AMT2Mob>> MobEntries;
	TMap<int32, TSoftClassPtr<UMT2ItemTemplate>> ItemEntries;
	TMap<int32, FString> MobSources;
	TMap<int32, FString> ItemSources;
	for (const FAssetData& Asset : Assets)
	{
		UBlueprint* Blueprint = Cast<UBlueprint>(Asset.GetAsset());
		UClass* Class = Blueprint ? Blueprint->GeneratedClass : nullptr;
		if (!Class || Class->HasAnyClassFlags(CLASS_Abstract))
		{
			continue;
		}

		const FString Source = Asset.GetSoftObjectPath().ToString();
		if (Class->IsChildOf(AMT2Mob::StaticClass()))
		{
			AddMob(MobEntries, MobSources, Class, Source, OutErrors);
		}
		else if (Class->IsChildOf(UMT2ItemTemplate::StaticClass()))
		{
			AddItem(ItemEntries, ItemSources, Class, Source, OutErrors);
		}
	}

	if (!OutErrors.IsEmpty())
	{
		return false;
	}

	UPackage* Package = CreatePackage(RegistryPackageName());
	UMT2VnumRegistry* Registry = LoadObject<UMT2VnumRegistry>(nullptr, RegistryObjectPath());
	if (!Registry)
	{
		Registry = NewObject<UMT2VnumRegistry>(
			Package, TEXT("DA_MT2VnumRegistry"), RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Registry);
	}
	if (!Registry)
	{
		OutErrors.Add(TEXT("Could not create /Game/Logic/DA_MT2VnumRegistry."));
		return false;
	}

	Registry->Modify();
	OutMobCount = MobEntries.Num();
	OutItemCount = ItemEntries.Num();
	Registry->SetEntries(MoveTemp(MobEntries), MoveTemp(ItemEntries));
	TArray<UPackage*> PackagesToSave{Registry->GetOutermost()};
	if (!UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, true))
	{
		OutErrors.Add(TEXT("VNUM registry was updated but could not be saved."));
		return false;
	}
	return true;
}
