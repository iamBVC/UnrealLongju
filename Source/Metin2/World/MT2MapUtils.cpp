/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "World/MT2MapUtils.h"
#include "Config/MT2PathSettings.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogMT2MapUtils, Log, All);

namespace
{
	FString NormalizeMapAlias(const FString& Value)
	{
		FString Alias = FPaths::GetCleanFilename(Value).ToLower();
		if (Alias.StartsWith(TEXT("metin2_")))
		{
			Alias.RightChopInline(7);
		}
		return Alias;
	}

	FString ResolveRedirectors(IAssetRegistry& AssetRegistry, const FString& PackagePath)
	{
		FSoftObjectPath ObjectPath(FString::Printf(
			TEXT("%s.%s"), *PackagePath, *FPackageName::GetShortName(PackagePath)));
		for (int32 RedirectDepth = 0; RedirectDepth < 8; ++RedirectDepth)
		{
			const FAssetData Asset = AssetRegistry.GetAssetByObjectPath(ObjectPath);
			if (!Asset.IsValid())
			{
				return PackagePath;
			}
			if (!Asset.IsRedirector())
			{
				return Asset.PackageName.ToString();
			}

			FString Destination;
			if (!Asset.GetTagValue(TEXT("DestinationObject"), Destination) || Destination.IsEmpty())
			{
				return PackagePath;
			}
			const FString DestinationObjectPath =
				FPackageName::ExportTextPathToObjectPath(Destination);
			if (FPackageName::ObjectPathToPackageName(DestinationObjectPath).IsEmpty())
			{
				return PackagePath;
			}
			ObjectPath = FSoftObjectPath(DestinationObjectPath);
		}
		return PackagePath;
	}

	FString ResolveFromPresentationActor(
		IAssetRegistry& AssetRegistry, const TArray<FString>& NormalizedAliases)
	{
		TArray<FAssetData> ExternalActors;
		AssetRegistry.GetAssetsByPath(
			FName(UMT2PathSettings::Path(TEXT("__ExternalActors___Maps_Game"))), ExternalActors, true, true);
		for (const FAssetData& Asset : ExternalActors)
		{
			FString ActorClass;
			if (!Asset.GetTagValue(TEXT("ActorMetaDataClass"), ActorClass) ||
				!ActorClass.EndsWith(TEXT(".MT2MapPresentationActor")))
			{
				continue;
			}

			FString ActorLabel;
			if (!Asset.GetTagValue(TEXT("ActorLabel"), ActorLabel))
			{
				continue;
			}
			const FString NormalizedLabel = ActorLabel.ToLower();
			if (!NormalizedAliases.ContainsByPredicate(
				[&NormalizedLabel](const FString& Alias)
				{
					return NormalizedLabel == FString::Printf(
						TEXT("mt2_mappresentation_%s"), *Alias) ||
						NormalizedLabel.EndsWith(FString::Printf(TEXT("_%s"), *Alias));
				}))
			{
				continue;
			}

			static const FString ExternalActorsPrefix = TEXT("/Game/__ExternalActors__/");
			const FString ExternalPackage = Asset.PackageName.ToString();
			if (!ExternalPackage.StartsWith(ExternalActorsPrefix))
			{
				continue;
			}

			TArray<FString> PathParts;
			ExternalPackage.Mid(ExternalActorsPrefix.Len()).ParseIntoArray(
				PathParts, TEXT("/"), true);
			// External actor packages end in two hash folders and the actor asset name.
			if (PathParts.Num() <= 3)
			{
				continue;
			}
			PathParts.SetNum(PathParts.Num() - 3);
			const FString WorldPackage = TEXT("/Game/") + FString::Join(PathParts, TEXT("/"));
			if (FPackageName::DoesPackageExist(WorldPackage))
			{
				UE_LOG(LogMT2MapUtils, Display,
					TEXT("Map alias %s resolved from presentation metadata to %s."),
					*ActorLabel, *WorldPackage);
				return WorldPackage;
			}
		}
		return FString();
	}
}

FString MT2MapUtils::ResolvePIEWorldPackage(const TArray<FString>& MapAliases)
{
	IAssetRegistry& AssetRegistry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	TArray<FString> NormalizedAliases;
	for (const FString& Alias : MapAliases)
	{
		const FString Normalized = NormalizeMapAlias(Alias);
		if (!Normalized.IsEmpty())
		{
			NormalizedAliases.AddUnique(Normalized);
		}
	}

	if (const FString ImportedWorld =
		ResolveFromPresentationActor(AssetRegistry, NormalizedAliases);
		!ImportedWorld.IsEmpty())
	{
		return ImportedWorld;
	}

	for (FString Candidate : MapAliases)
	{
		Candidate.TrimStartAndEndInline();
		if (!Candidate.StartsWith(TEXT("/")))
		{
			Candidate = UMT2PathSettings::Format(TEXT("GameMapTemplate"), TEXT("%s"), *FPaths::GetCleanFilename(Candidate));
		}
		if (!FPackageName::DoesPackageExist(Candidate))
		{
			continue;
		}
		const FString Resolved = ResolveRedirectors(AssetRegistry, Candidate);
		if (FPackageName::DoesPackageExist(Resolved))
		{
			return Resolved;
		}
	}
	return FString();
}
#endif
