/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Importers/MT2SpeedTreeConverter.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	constexpr const TCHAR* SpeedTreeConversionVersion = TEXT("MT2UE_SPEEDTREE_CONVERSION_V1");

	FString GetConverterPath()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("MT2UE"));
		return Plugin.IsValid()
			? Plugin->GetBaseDir() / TEXT("Binaries/ThirdParty/SpeedTree/Win32/SpeedTreeToObj.exe")
			: FString();
	}
}

bool FMT2SpeedTreeConverter::ConvertToObj(const FString& InputTreePath, const FString& OutputObjPath,
	TArray<FMT2GrannyMaterialSlot>& OutMaterialSlots, FString& OutError)
{
	OutMaterialSlots.Reset();
	OutError.Reset();
	const FString ManifestPath = FPaths::ChangeExtension(OutputObjPath, TEXT("materials"));

	FString ExistingObj;
	if (FFileHelper::LoadFileToString(ExistingObj, *OutputObjPath) &&
		ExistingObj.Contains(SpeedTreeConversionVersion) &&
		IFileManager::Get().FileExists(*ManifestPath))
	{
		return ReadMaterialManifest(ManifestPath, OutMaterialSlots, OutError);
	}

	const FString ConverterPath = GetConverterPath();
	if (ConverterPath.IsEmpty() || !IFileManager::Get().FileExists(*ConverterPath))
	{
		OutError = FString::Printf(TEXT("Bundled SpeedTree converter is missing: %s"), *ConverterPath);
		return false;
	}

	FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*FPaths::GetPath(OutputObjPath));
	IFileManager::Get().Delete(*OutputObjPath, false, true);
	IFileManager::Get().Delete(*ManifestPath, false, true);

	const FString Arguments = FString::Printf(TEXT("\"%s\" \"%s\" \"%s\""),
		*InputTreePath, *OutputObjPath, *ManifestPath);
	int32 ReturnCode = INDEX_NONE;
	FString StdOut;
	FString StdErr;
	if (!FPlatformProcess::ExecProcess(*ConverterPath, *Arguments, &ReturnCode, &StdOut, &StdErr) || ReturnCode != 0)
	{
		OutError = FString::Printf(TEXT("SpeedTree converter failed (%d): %s%s%s"), ReturnCode,
			*StdErr.TrimStartAndEnd(), StdErr.IsEmpty() || StdOut.IsEmpty() ? TEXT("") : TEXT(" | "),
			*StdOut.TrimStartAndEnd());
		return false;
	}

	if (!IFileManager::Get().FileExists(*OutputObjPath))
	{
		OutError = TEXT("SpeedTree converter completed without producing an OBJ file.");
		return false;
	}
	return ReadMaterialManifest(ManifestPath, OutMaterialSlots, OutError);
}

bool FMT2SpeedTreeConverter::ReadMaterialManifest(const FString& ManifestPath,
	TArray<FMT2GrannyMaterialSlot>& OutMaterialSlots, FString& OutError)
{
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *ManifestPath))
	{
		OutError = FString::Printf(TEXT("Could not read SpeedTree material manifest: %s"), *ManifestPath);
		return false;
	}

	FString BranchTexture;
	FString FoliageTexture;
	for (const FString& Line : Lines)
	{
		FString Key;
		FString Value;
		if (!Line.Split(TEXT("="), &Key, &Value)) continue;
		Key.TrimStartAndEndInline();
		Value.TrimStartAndEndInline();
		if (Key.Equals(TEXT("Branch"), ESearchCase::IgnoreCase)) BranchTexture = Value;
		else if (Key.Equals(TEXT("Foliage"), ESearchCase::IgnoreCase)) FoliageTexture = Value;
	}

	FMT2GrannyMaterialSlot Branch;
	Branch.SlotName = TEXT("Branch");
	Branch.ImportedSlotName = Branch.SlotName;
	Branch.DiffuseTextureReference = BranchTexture;
	Branch.bHasMaterialProperties = true;
	OutMaterialSlots.Add(MoveTemp(Branch));

	FMT2GrannyMaterialSlot Foliage;
	Foliage.SlotName = TEXT("Foliage");
	Foliage.ImportedSlotName = Foliage.SlotName;
	Foliage.DiffuseTextureReference = FoliageTexture;
	Foliage.OpacityTextureReference = FoliageTexture;
	Foliage.bTwoSided = true;
	Foliage.bHasMaterialProperties = true;
	OutMaterialSlots.Add(MoveTemp(Foliage));
	return true;
}
