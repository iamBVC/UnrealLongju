/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "MT2ImportUITexturesCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "Factories/TextureFactory.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

UMT2ImportUITexturesCommandlet::UMT2ImportUITexturesCommandlet()
{
	IsClient = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UMT2ImportUITexturesCommandlet::Main(const FString& Params)
{
	FString SourceRoot = TEXT("D:/Giochi/Metin2/Development/Dumps/my_dump");
	FParse::Value(*Params, TEXT("SourceRoot="), SourceRoot);
	for (int32 Index = 1; Index <= 3; ++Index)
	{
		const FString BaseName = FString::Printf(TEXT("T_ex_gemshop_button_0%d"), Index);
		const FString Filename = FPaths::Combine(SourceRoot, TEXT("UI/game/taskbar"), BaseName.RightChop(2) + TEXT(".tga"));
		const FString PackageName = TEXT("/Game/ymir_work/ui/game/taskbar/") + BaseName;
		if (!FPaths::FileExists(Filename))
		{
			UE_LOG(LogTemp, Error, TEXT("Missing MT2 UI texture: %s"), *Filename);
			return 1;
		}
		if (LoadObject<UTexture2D>(nullptr, *(PackageName + TEXT(".") + BaseName)))
		{
			continue;
		}

		UPackage* Package = CreatePackage(*PackageName);
		UTextureFactory* Factory = NewObject<UTextureFactory>();
		Factory->AddToRoot();
		UTexture2D* Texture = Cast<UTexture2D>(UFactory::StaticImportObject(UTexture2D::StaticClass(), Package,
			FName(*BaseName), RF_Public | RF_Standalone, *Filename, nullptr, Factory));
		Factory->RemoveFromRoot();
		if (!Texture)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to import MT2 UI texture: %s"), *Filename);
			return 1;
		}
		Texture->LODGroup = TEXTUREGROUP_UI;
		Texture->MipGenSettings = TMGS_NoMipmaps;
		Texture->CompressionSettings = TC_EditorIcon;
		Texture->SRGB = true;
		Texture->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(Texture);

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		const FString PackageFilename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		if (!UPackage::SavePackage(Package, Texture, *PackageFilename, SaveArgs))
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to save MT2 UI texture: %s"), *PackageFilename);
			return 1;
		}
	}
	return 0;
}
