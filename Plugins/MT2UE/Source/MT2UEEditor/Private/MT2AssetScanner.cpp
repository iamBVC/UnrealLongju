/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "MT2AssetScanner.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"

namespace
{
	FString PrefixAssetName(EMT2AssetKind Kind, const FString& AssetName)
	{
		const TCHAR* Prefix = TEXT("");
		switch (Kind)
		{
		case EMT2AssetKind::Texture:
			Prefix = TEXT("T_");
			break;
		case EMT2AssetKind::Granny:
		case EMT2AssetKind::Tree:
			Prefix = TEXT("SM_");
			break;
		default:
			break;
		}

		const FString PrefixString(Prefix);
		if (PrefixString.IsEmpty() || AssetName.StartsWith(PrefixString, ESearchCase::IgnoreCase))
		{
			return AssetName;
		}

		return PrefixString + AssetName;
	}
}

int32 FMT2AssetScanResult::GetCount(EMT2AssetKind Kind) const
{
	const int32* Count = KindCounts.Find(Kind);
	return Count ? *Count : 0;
}

TArray<FMT2AssetRecord> FMT2AssetScanResult::GetRecordsByKind(EMT2AssetKind Kind) const
{
	TArray<FMT2AssetRecord> Filtered;
	for (const FMT2AssetRecord& Record : Records)
	{
		if (Record.Kind == Kind)
		{
			Filtered.Add(Record);
		}
	}
	return Filtered;
}

bool FMT2AssetScanner::Scan(const FString& RootDirectory, FMT2AssetScanResult& OutResult, FString& OutError) const
{
	FString NormalizedRoot = RootDirectory;
	FPaths::NormalizeDirectoryName(NormalizedRoot);

	if (NormalizedRoot.IsEmpty() || !IFileManager::Get().DirectoryExists(*NormalizedRoot))
	{
		OutError = FString::Printf(TEXT("Directory does not exist: %s"), *RootDirectory);
		return false;
	}

	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *NormalizedRoot, TEXT("*"), true, false);

	OutResult = FMT2AssetScanResult();
	OutResult.RootDirectory = NormalizedRoot;
	OutResult.Records.Reserve(Files.Num());

	for (FString AbsolutePath : Files)
	{
		FPaths::NormalizeFilename(AbsolutePath);

		FString RelativePath = AbsolutePath;
		FPaths::MakePathRelativeTo(RelativePath, *NormalizedRoot);
		FPaths::NormalizeFilename(RelativePath);

		FMT2AssetRecord Record;
		Record.AbsolutePath = AbsolutePath;
		Record.RelativePath = RelativePath;
		Record.Extension = FPaths::GetExtension(AbsolutePath).ToLower();
		Record.Kind = ClassifyExtension(Record.Extension, FPaths::GetCleanFilename(AbsolutePath));
		Record.VirtualPath = NormalizeVirtualPath(RelativePath, Record.PackName, Record.ContentPath);
		Record.Size = IFileManager::Get().FileSize(*AbsolutePath);

		OutResult.ExtensionCounts.FindOrAdd(Record.Extension)++;
		OutResult.KindCounts.FindOrAdd(Record.Kind)++;
		OutResult.Records.Add(MoveTemp(Record));
	}

	return true;
}

EMT2AssetKind FMT2AssetScanner::ClassifyExtension(const FString& Extension, const FString& FileName)
{
	const FString LowerFileName = FileName.ToLower();

	if (Extension == TEXT("dds") || Extension == TEXT("tga") || Extension == TEXT("jpg") ||
		Extension == TEXT("jpeg") || Extension == TEXT("png") || Extension == TEXT("bmp"))
	{
		return EMT2AssetKind::Texture;
	}

	if (Extension == TEXT("gr2"))
	{
		return EMT2AssetKind::Granny;
	}

	if (Extension == TEXT("msm"))
	{
		return EMT2AssetKind::ModelScript;
	}

	if (Extension == TEXT("msa") || Extension == TEXT("mss") || Extension == TEXT("msf"))
	{
		return EMT2AssetKind::MotionScript;
	}

	if (Extension == TEXT("prb") || Extension == TEXT("prt") || Extension == TEXT("prd") || Extension == TEXT("pre"))
	{
		return EMT2AssetKind::Property;
	}

	if (Extension == TEXT("raw") || Extension == TEXT("atr") || Extension == TEXT("wtr") || Extension == TEXT("msk"))
	{
		return EMT2AssetKind::Terrain;
	}

	if (Extension == TEXT("mse") || Extension == TEXT("mde") || Extension == TEXT("msenv"))
	{
		return EMT2AssetKind::EffectScript;
	}

	if (Extension == TEXT("spt"))
	{
		return EMT2AssetKind::Tree;
	}

	if (Extension == TEXT("wav") || Extension == TEXT("mp3"))
	{
		return EMT2AssetKind::Audio;
	}

	if (Extension == TEXT("txt") && (LowerFileName == TEXT("setting.txt") || LowerFileName == TEXT("areadata.txt") ||
		LowerFileName == TEXT("areaproperty.txt") || LowerFileName == TEXT("mapproperty.txt")))
	{
		return EMT2AssetKind::MapText;
	}

	return EMT2AssetKind::Unknown;
}

FString FMT2AssetScanner::KindToString(EMT2AssetKind Kind)
{
	switch (Kind)
	{
	case EMT2AssetKind::Texture:
		return TEXT("Textures");
	case EMT2AssetKind::Granny:
		return TEXT("Granny .gr2");
	case EMT2AssetKind::ModelScript:
		return TEXT("Model scripts .msm");
	case EMT2AssetKind::MotionScript:
		return TEXT("Motion scripts .msa/.mss");
	case EMT2AssetKind::Property:
		return TEXT("Properties .prb/.prt/.prd");
	case EMT2AssetKind::Terrain:
		return TEXT("Terrain data");
	case EMT2AssetKind::MapText:
		return TEXT("Map text");
	case EMT2AssetKind::EffectScript:
		return TEXT("Effect scripts");
	case EMT2AssetKind::Tree:
		return TEXT("SpeedTree .spt");
	case EMT2AssetKind::Audio:
		return TEXT("Audio");
	default:
		return TEXT("Unknown");
	}
}

FString FMT2AssetScanner::NormalizeVirtualPath(const FString& RelativePath, FString& OutPackName, FString& OutContentPath)
{
	TArray<FString> Parts;
	RelativePath.ParseIntoArray(Parts, TEXT("/"), true);

	OutPackName = Parts.Num() > 0 ? Parts[0] : FString();
	OutContentPath = RelativePath;

	if (Parts.Num() > 1)
	{
		OutContentPath.Reset();
		for (int32 Index = 1; Index < Parts.Num(); ++Index)
		{
			if (!OutContentPath.IsEmpty())
			{
				OutContentPath += TEXT("/");
			}
			OutContentPath += Parts[Index];
		}
	}

	const FString YmirPrefix = TEXT("ymir work/");
	if (OutContentPath.StartsWith(YmirPrefix, ESearchCase::IgnoreCase))
	{
		return FString(TEXT("d:/")) + OutContentPath;
	}

	return OutContentPath;
}

FString FMT2AssetScanner::SanitizePackagePathSegment(const FString& Segment)
{
	FString Sanitized;
	Sanitized.Reserve(Segment.Len());

	for (int32 Index = 0; Index < Segment.Len(); ++Index)
	{
		const TCHAR Character = Segment[Index];
		if (FChar::IsAlnum(Character) || Character == TCHAR('_') || Character == TCHAR('-'))
		{
			Sanitized.AppendChar(Character);
		}
		else
		{
			Sanitized.AppendChar(TCHAR('_'));
		}
	}

	while (Sanitized.Contains(TEXT("__")))
	{
		Sanitized = Sanitized.Replace(TEXT("__"), TEXT("_"));
	}

	Sanitized.RemoveFromStart(TEXT("_"));
	Sanitized.RemoveFromEnd(TEXT("_"));
	return Sanitized.IsEmpty() ? TEXT("Asset") : Sanitized;
}

FString FMT2AssetScanner::BuildContentPackagePath(const FString& DestinationRoot, const FMT2AssetRecord& Record)
{
	TArray<FString> Parts;
	Record.ContentPath.ParseIntoArray(Parts, TEXT("/"), true);
	if (Parts.Num() > 0)
	{
		Parts.Pop();
	}

	FString Result = DestinationRoot;
	Result.RemoveFromEnd(TEXT("/"));
	for (const FString& Part : Parts)
	{
		Result /= SanitizePackagePathSegment(Part);
	}
	return Result;
}

FString FMT2AssetScanner::BuildContentObjectPath(const FString& DestinationRoot, const FMT2AssetRecord& Record)
{
	const FString AssetName = PrefixAssetName(Record.Kind, SanitizePackagePathSegment(FPaths::GetBaseFilename(Record.ContentPath)));
	const FString PackagePath = BuildContentPackagePath(DestinationRoot, Record) / AssetName;
	return PackagePath + TEXT(".") + AssetName;
}
