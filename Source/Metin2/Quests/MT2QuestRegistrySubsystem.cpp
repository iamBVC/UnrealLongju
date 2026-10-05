/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Quests/MT2QuestRegistrySubsystem.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Config/MT2GameplaySettings.h"
#include "Quests/MT2Quest.h"
#include "Quests/MT2QuestTableAsset.h"

DEFINE_LOG_CATEGORY_STATIC(LogMT2Quest, Log, All);

const TCHAR* UMT2QuestRegistrySubsystem::QuestRoot = TEXT("/Game/Quests");

const TArray<TObjectPtr<const UMT2Quest>>& UMT2QuestRegistrySubsystem::GetQuests()
{
	if (!bLoaded)
	{
		ReloadQuests();
	}
	return Quests;
}

const UMT2Quest* UMT2QuestRegistrySubsystem::FindQuest(FName QuestId)
{
	GetQuests();
	const TObjectPtr<const UMT2Quest>* Found = QuestsById.Find(QuestId);
	return Found ? Found->Get() : nullptr;
}

void UMT2QuestRegistrySubsystem::ReloadQuests()
{
	Quests.Reset();
	QuestsById.Reset();
	bLoaded = true;

	IAssetRegistry& AssetRegistry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	AssetRegistry.WaitForCompletion();

	// In the editor quest assets are UBlueprints; in a cooked build only the generated class survives,
	// so accept both and resolve each to its UClass.
	FARFilter Filter;
	Filter.PackagePaths.Add(QuestRoot);
	Filter.bRecursivePaths = true;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.ClassPaths.Add(UBlueprintGeneratedClass::StaticClass()->GetClassPathName());

	TArray<FAssetData> Assets;
	AssetRegistry.GetAssets(Filter, Assets);
	const UMT2QuestTableAsset* QuestManifest = LoadObject<UMT2QuestTableAsset>(
		nullptr, UMT2QuestTableAsset::GetAssetPath());
	const bool bHasActiveManifest = QuestManifest && !QuestManifest->ActiveQuestIds.IsEmpty();
	int32 InactiveCount = 0;
	for (const FAssetData& Asset : Assets)
	{
		UObject* Loaded = Asset.GetAsset();
		UClass* GeneratedClass = nullptr;
		if (const UBlueprint* Blueprint = Cast<UBlueprint>(Loaded))
		{
			GeneratedClass = Blueprint->GeneratedClass;
		}
		else
		{
			GeneratedClass = Cast<UClass>(Loaded);
		}
		if (!GeneratedClass || !GeneratedClass->IsChildOf(UMT2Quest::StaticClass()) ||
			GeneratedClass->HasAnyClassFlags(CLASS_Abstract))
		{
			continue;
		}

		const UMT2Quest* Quest = GeneratedClass->GetDefaultObject<UMT2Quest>();
		if (!Quest)
		{
			continue;
		}
		const FName QuestId = Quest->GetQuestId();
		if (bHasActiveManifest && !QuestManifest->ActiveQuestIds.Contains(QuestId))
		{
			++InactiveCount;
			continue;
		}
		if (UMT2GameplaySettings::Get().DisabledQuestIds.Contains(QuestId))
		{
			UE_LOG(LogMT2Quest, Display, TEXT("Skipping disabled quest '%s' (%s)."),
				*QuestId.ToString(), *GeneratedClass->GetName());
			continue;
		}
		if (QuestsById.Contains(QuestId))
		{
			UE_LOG(LogMT2Quest, Warning,
				TEXT("Duplicate quest id '%s' (%s); keeping the first one loaded."),
				*QuestId.ToString(), *GeneratedClass->GetName());
			continue;
		}
		Quests.Add(Quest);
		QuestsById.Add(QuestId, Quest);
	}

	UE_LOG(LogMT2Quest, Display,
		TEXT("Loaded %d active quest Blueprint(s) from %s%s."), Quests.Num(), QuestRoot,
		InactiveCount > 0 ? *FString::Printf(TEXT("; skipped %d not present in locale_list"), InactiveCount) : TEXT(""));
}
