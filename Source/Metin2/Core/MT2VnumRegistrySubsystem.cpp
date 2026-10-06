/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Core/MT2VnumRegistrySubsystem.h"
#include "Config/MT2PathSettings.h"

#include "Core/MT2VnumRegistry.h"
#include "Items/MT2ItemTemplate.h"
#include "Mobs/MT2Mob.h"

namespace
{
	const TCHAR* RegistryObjectPath() { return UMT2PathSettings::Path(TEXT("VnumRegistry")); }
}

void UMT2VnumRegistrySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ReloadRegistryAsset();
}

bool UMT2VnumRegistrySubsystem::ReloadRegistryAsset()
{
	SortedItemVnums.Reset();
	ResolvedItemAliases.Reset();
	Registry = LoadObject<UMT2VnumRegistry>(nullptr, RegistryObjectPath());
	if (!Registry)
	{
		UE_LOG(LogTemp, Error,
			TEXT("MT2 VNUM registry is missing at %s. Open the editor or rebuild it from the MT2UE menu."),
			RegistryObjectPath());
		return false;
	}
	Registry->GetItemTemplates().GenerateKeyArray(SortedItemVnums);
	SortedItemVnums.Sort();
	UE_LOG(LogTemp, Log, TEXT("MT2 VNUM registry loaded: %d mobs, %d items."),
		Registry->GetMobClasses().Num(), Registry->GetItemTemplates().Num());
	return true;
}

bool UMT2VnumRegistrySubsystem::HasMob(int32 Vnum) const
{
	return Registry && Registry->GetMobClasses().Contains(Vnum);
}

bool UMT2VnumRegistrySubsystem::HasItem(int32 Vnum) const
{
	return FindItemTemplate(Vnum) != nullptr;
}

TSubclassOf<AMT2Mob> UMT2VnumRegistrySubsystem::ResolveMobClass(int32 Vnum)
{
	if (!Registry)
	{
		return nullptr;
	}
	const TSoftClassPtr<AMT2Mob>* MobClass = Registry->GetMobClasses().Find(Vnum);
	return MobClass ? MobClass->LoadSynchronous() : nullptr;
}

TSubclassOf<UMT2ItemTemplate> UMT2VnumRegistrySubsystem::ResolveItemTemplateClass(int32 Vnum)
{
	const TSoftClassPtr<UMT2ItemTemplate>* ItemClass = FindItemTemplate(Vnum);
	return ItemClass ? ItemClass->LoadSynchronous() : nullptr;
}

FSoftClassPath UMT2VnumRegistrySubsystem::GetMobClassPath(int32 Vnum) const
{
	const TSoftClassPtr<AMT2Mob>* MobClass = Registry ? Registry->GetMobClasses().Find(Vnum) : nullptr;
	return MobClass ? FSoftClassPath(MobClass->ToSoftObjectPath().ToString()) : FSoftClassPath();
}

FSoftClassPath UMT2VnumRegistrySubsystem::GetItemTemplatePath(int32 Vnum) const
{
	const TSoftClassPtr<UMT2ItemTemplate>* ItemClass = FindItemTemplate(Vnum);
	return ItemClass ? FSoftClassPath(ItemClass->ToSoftObjectPath().ToString()) : FSoftClassPath();
}

const TSoftClassPtr<UMT2ItemTemplate>* UMT2VnumRegistrySubsystem::FindItemTemplate(int32 Vnum) const
{
	if (!Registry || Vnum <= 0)
	{
		return nullptr;
	}
	const TMap<int32, TSoftClassPtr<UMT2ItemTemplate>>& Templates = Registry->GetItemTemplates();
	if (const TSoftClassPtr<UMT2ItemTemplate>* Exact = Templates.Find(Vnum))
	{
		return Exact;
	}
	if (const int32* CachedBaseVnum = ResolvedItemAliases.Find(Vnum))
	{
		return Templates.Find(*CachedBaseVnum);
	}

	int32 Lower = 0;
	int32 Upper = SortedItemVnums.Num();
	while (Lower < Upper)
	{
		const int32 Middle = Lower + (Upper - Lower) / 2;
		if (SortedItemVnums[Middle] <= Vnum)
		{
			Lower = Middle + 1;
		}
		else
		{
			Upper = Middle;
		}
	}
	if (Lower <= 0)
	{
		return nullptr;
	}

	const TSoftClassPtr<UMT2ItemTemplate>* Candidate = Templates.Find(SortedItemVnums[Lower - 1]);
	const UClass* CandidateClass = Candidate ? Candidate->LoadSynchronous() : nullptr;
	const UMT2ItemTemplate* Defaults = CandidateClass
		? CandidateClass->GetDefaultObject<UMT2ItemTemplate>() : nullptr;
	if (!Defaults || Defaults->VnumRange <= 0 ||
		Vnum > Defaults->Vnum + Defaults->VnumRange)
	{
		return nullptr;
	}
	ResolvedItemAliases.Add(Vnum, Defaults->Vnum);
	return Candidate;
}
