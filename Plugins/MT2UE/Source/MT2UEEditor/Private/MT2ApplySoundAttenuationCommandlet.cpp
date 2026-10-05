/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "MT2ApplySoundAttenuationCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	const TCHAR* SharedAttenuationPackage = TEXT("/Game/sound/SA_MT2_CharacterSounds");
	const TCHAR* SharedAttenuationName = TEXT("SA_MT2_CharacterSounds");

	// UI feedback must stay 2D, and ambience beds are placed/managed separately.
	const TCHAR* ExcludedRoots[] = { TEXT("/Game/sound/ui"), TEXT("/Game/sound/ambience") };

	bool SavePackageToDisk(UPackage* Package, UObject* Asset)
	{
		const FString Filename = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		return UPackage::SavePackage(Package, Asset, *Filename, Args);
	}
}

UMT2ApplySoundAttenuationCommandlet::UMT2ApplySoundAttenuationCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UMT2ApplySoundAttenuationCommandlet::Main(const FString& Params)
{
	// The one attenuation profile every character sound shares. Created only if missing, so hand
	// tuning it in the editor survives re-runs of this pass.
	const FString AttenuationObjectPath =
		FString(SharedAttenuationPackage) + TEXT(".") + SharedAttenuationName;
	USoundAttenuation* Attenuation = LoadObject<USoundAttenuation>(nullptr, *AttenuationObjectPath);
	if (!Attenuation)
	{
		UPackage* Package = CreatePackage(SharedAttenuationPackage);
		Attenuation = NewObject<USoundAttenuation>(
			Package, SharedAttenuationName, RF_Public | RF_Standalone | RF_Transactional);
		// A realistic character-sound profile: full volume up to 4m, natural falloff, inaudible
		// past ~40m. Roughly the reach of the old game's audible neighbourhood.
		Attenuation->Attenuation.bAttenuate = true;
		Attenuation->Attenuation.bSpatialize = true;
		Attenuation->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
		Attenuation->Attenuation.AttenuationShapeExtents = FVector(400.0f, 0.0f, 0.0f);
		Attenuation->Attenuation.FalloffDistance = 3600.0f;
		Attenuation->Attenuation.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
		FAssetRegistryModule::AssetCreated(Attenuation);
		if (!SavePackageToDisk(Package, Attenuation))
		{
			UE_LOG(LogTemp, Error, TEXT("[MT2SoundAtten] Failed to save %s"), *AttenuationObjectPath);
			return 1;
		}
		UE_LOG(LogTemp, Display, TEXT("[MT2SoundAtten] Created %s"), *AttenuationObjectPath);
	}

	FAssetRegistryModule& AssetRegistry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	AssetRegistry.Get().SearchAllAssets(true);

	TArray<FAssetData> SoundAssets;
	AssetRegistry.Get().GetAssetsByPath(TEXT("/Game/sound"), SoundAssets, true);

	int32 Assigned = 0;
	int32 AlreadySet = 0;
	int32 Failed = 0;
	for (const FAssetData& AssetData : SoundAssets)
	{
		const FString PackagePath = AssetData.PackageName.ToString();
		bool bExcluded = false;
		for (const TCHAR* Excluded : ExcludedRoots)
		{
			if (PackagePath.StartsWith(Excluded))
			{
				bExcluded = true;
				break;
			}
		}
		if (bExcluded)
		{
			continue;
		}

		USoundBase* Sound = Cast<USoundBase>(AssetData.GetAsset());
		if (!Sound)
		{
			continue;
		}
		// Only fill the gap; a sound someone already gave an attenuation keeps it.
		if (Sound->AttenuationSettings)
		{
			AlreadySet++;
			continue;
		}

		Sound->Modify();
		Sound->AttenuationSettings = Attenuation;
		Sound->MarkPackageDirty();
		if (SavePackageToDisk(Sound->GetPackage(), Sound))
		{
			Assigned++;
		}
		else
		{
			Failed++;
			UE_LOG(LogTemp, Warning, TEXT("[MT2SoundAtten] Could not save %s"), *PackagePath);
		}
	}

	UE_LOG(LogTemp, Display,
		TEXT("[MT2SoundAtten] Done: %d sound(s) attenuated, %d already had one, %d failed to save."),
		Assigned, AlreadySet, Failed);
	return Failed == 0 ? 0 : 1;
}
