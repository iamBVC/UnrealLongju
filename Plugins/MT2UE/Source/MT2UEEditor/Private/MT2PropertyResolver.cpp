/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "MT2PropertyResolver.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

bool FMT2PropertyResolver::Resolve(const FMT2AssetScanResult& ScanResult, FMT2ResolvedWorldResult& OutResult, FString& OutError) const
{
	OutResult = FMT2ResolvedWorldResult();

	TMap<FString, const FMT2AssetRecord*> AssetLookup;
	BuildAssetLookup(ScanResult, AssetLookup);

	for (const FMT2AssetRecord& Record : ScanResult.Records)
	{
		if (Record.Kind != EMT2AssetKind::Property)
		{
			continue;
		}

		FMT2PropertyRecord Property;
		if (!ParsePropertyFile(Record, Property))
		{
			OutResult.InvalidPropertyFiles++;
			continue;
		}

		ResolvePropertyAsset(Property, AssetLookup);
		if (OutResult.PropertiesByCrc.Contains(Property.Crc))
		{
			OutResult.DuplicatePropertyCrcs++;
		}
		OutResult.PropertiesByCrc.Add(Property.Crc, MoveTemp(Property));
		OutResult.PropertyFilesRead++;
	}

	for (const FMT2AssetRecord& Record : ScanResult.Records)
	{
		if (FPaths::GetCleanFilename(Record.AbsolutePath).ToLower() != TEXT("areadata.txt"))
		{
			continue;
		}

		TArray<FMT2MapObjectPlacement> Placements;
		if (!ParseAreaDataFile(Record, Placements))
		{
			continue;
		}

		OutResult.AreaDataFilesRead++;
		for (FMT2MapObjectPlacement& Placement : Placements)
		{
			if (const FMT2PropertyRecord* Property = OutResult.PropertiesByCrc.Find(Placement.PropertyCrc))
			{
				Placement.Property = Property;
				Placement.ReferencedAsset = Property->ReferencedAsset;
				OutResult.ObjectsWithProperty++;

				if (Placement.ReferencedAsset)
				{
					OutResult.ObjectsWithResolvedAsset++;
					if (Placement.ReferencedAsset->Kind == EMT2AssetKind::Granny)
					{
						OutResult.StaticGrannyObjects++;
					}
				}
			}

			OutResult.MapObjects.Add(MoveTemp(Placement));
		}
	}

	if (OutResult.PropertyFilesRead == 0)
	{
		OutError = TEXT("No valid Metin2 property files were found.");
		return false;
	}

	return true;
}

bool FMT2PropertyResolver::ParsePropertyFile(const FMT2AssetRecord& Record, FMT2PropertyRecord& OutProperty)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Record.AbsolutePath))
	{
		return false;
	}

	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, false);
	if (Lines.Num() < 2 || Lines[0].TrimStartAndEnd() != TEXT("YPRT"))
	{
		return false;
	}

	OutProperty = FMT2PropertyRecord();
	OutProperty.AbsolutePath = Record.AbsolutePath;
	OutProperty.VirtualPath = Record.VirtualPath;
	OutProperty.Crc = static_cast<uint32>(FCString::Atoi64(*Lines[1].TrimStartAndEnd()));

	if (OutProperty.Crc == 0)
	{
		return false;
	}

	for (int32 LineIndex = 2; LineIndex < Lines.Num(); ++LineIndex)
	{
		FString Key;
		TArray<FString> Values;
		if (!ParseTokenLine(Lines[LineIndex], Key, Values))
		{
			continue;
		}

		OutProperty.Tokens.Add(Key, Values);
	}

	auto GetFirstValue = [&OutProperty](const TCHAR* KeyName) -> FString
	{
		if (const TArray<FString>* Values = OutProperty.Tokens.Find(FString(KeyName).ToLower()))
		{
			return Values->Num() > 0 ? (*Values)[0] : FString();
		}
		return FString();
	};

	OutProperty.PropertyType = GetFirstValue(TEXT("propertytype"));
	OutProperty.PropertyName = GetFirstValue(TEXT("propertyname"));

	const FString TypeLower = OutProperty.PropertyType.ToLower();
	if (TypeLower == TEXT("building"))
	{
		OutProperty.ReferencedAssetPath = GetFirstValue(TEXT("buildingfile"));
	}
	else if (TypeLower == TEXT("tree"))
	{
		OutProperty.ReferencedAssetPath = GetFirstValue(TEXT("treefile"));
	}
	else if (TypeLower == TEXT("effect"))
	{
		OutProperty.ReferencedAssetPath = GetFirstValue(TEXT("effectfile"));
	}
	else if (TypeLower == TEXT("dungeonblock"))
	{
		OutProperty.ReferencedAssetPath = GetFirstValue(TEXT("dungeonblockfile"));
	}

	OutProperty.ReferencedAssetVirtualPath = NormalizeReferencePath(OutProperty.ReferencedAssetPath);
	return true;
}

bool FMT2PropertyResolver::ParseAreaDataFile(const FMT2AssetRecord& Record, TArray<FMT2MapObjectPlacement>& OutPlacements)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Record.AbsolutePath))
	{
		return false;
	}

	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, false);
	if (Lines.Num() == 0 || Lines[0].TrimStartAndEnd() != TEXT("AreaDataFile"))
	{
		return false;
	}

	FString MapName;
	FString CellName;
	ResolveMapMetadata(Record, MapName, CellName);

	for (int32 LineIndex = 1; LineIndex < Lines.Num(); ++LineIndex)
	{
		const FString Line = Lines[LineIndex].TrimStartAndEnd();
		if (!Line.StartsWith(TEXT("Start Object"), ESearchCase::IgnoreCase))
		{
			continue;
		}

		TArray<FString> BlockLines;
		for (++LineIndex; LineIndex < Lines.Num(); ++LineIndex)
		{
			const FString BlockLine = Lines[LineIndex].TrimStartAndEnd();
			if (BlockLine.Equals(TEXT("End Object"), ESearchCase::IgnoreCase))
			{
				break;
			}
			if (!BlockLine.IsEmpty())
			{
				BlockLines.Add(BlockLine);
			}
		}

		if (BlockLines.Num() < 2)
		{
			continue;
		}

		FMT2MapObjectPlacement Placement;
		Placement.MapName = MapName;
		Placement.CellName = CellName;
		Placement.AreaDataPath = Record.AbsolutePath;
		Placement.PropertyCrc = static_cast<uint32>(FCString::Atoi64(*BlockLines[1]));

		ParseFloatTriple(BlockLines[0], Placement.Position);
		if (BlockLines.Num() > 2)
		{
			ParseRotation(BlockLines[2], Placement.Rotation);
		}
		if (BlockLines.Num() > 3)
		{
			Placement.HeightBias = FCString::Atof(*BlockLines[3]);
		}

		OutPlacements.Add(MoveTemp(Placement));
	}

	return true;
}

void FMT2PropertyResolver::BuildAssetLookup(const FMT2AssetScanResult& ScanResult, TMap<FString, const FMT2AssetRecord*>& OutLookup)
{
	auto AddLookupPath = [&OutLookup](const FString& Path, const FMT2AssetRecord& Record)
	{
		FString Normalized = NormalizeReferencePath(Path).ToLower();
		if (!Normalized.IsEmpty())
		{
			OutLookup.Add(Normalized, &Record);
		}
	};

	for (const FMT2AssetRecord& Record : ScanResult.Records)
	{
		AddLookupPath(Record.VirtualPath, Record);
		AddLookupPath(Record.ContentPath, Record);
		AddLookupPath(Record.RelativePath, Record);

		const FString RelativePath = Record.RelativePath.Replace(TEXT("\\"), TEXT("/"));
		if (RelativePath.StartsWith(TEXT("zone/"), ESearchCase::IgnoreCase) ||
			RelativePath.StartsWith(TEXT("terrainmaps/"), ESearchCase::IgnoreCase) ||
			RelativePath.StartsWith(TEXT("textureset/"), ESearchCase::IgnoreCase) ||
			RelativePath.StartsWith(TEXT("ymir work/"), ESearchCase::IgnoreCase))
		{
			AddLookupPath(FString(TEXT("d:/ymir work/")) + RelativePath, Record);
		}
	}
}

void FMT2PropertyResolver::ResolvePropertyAsset(FMT2PropertyRecord& Property, const TMap<FString, const FMT2AssetRecord*>& AssetLookup)
{
	if (Property.ReferencedAssetVirtualPath.IsEmpty())
	{
		return;
	}

	if (const FMT2AssetRecord* const* Record = AssetLookup.Find(Property.ReferencedAssetVirtualPath.ToLower()))
	{
		Property.ReferencedAsset = *Record;
	}
}

void FMT2PropertyResolver::ResolveMapMetadata(const FMT2AssetRecord& AreaDataRecord, FString& OutMapName, FString& OutCellName)
{
	FString ParentDirectory = FPaths::GetPath(AreaDataRecord.AbsolutePath);
	FPaths::NormalizeFilename(ParentDirectory);
	const FString ParentName = FPaths::GetCleanFilename(ParentDirectory);
	const FString GrandParentName = FPaths::GetCleanFilename(FPaths::GetPath(ParentDirectory));

	if (ParentName.Len() == 6 && ParentName.IsNumeric() && !GrandParentName.IsEmpty())
	{
		OutMapName = GrandParentName;
		OutCellName = ParentName;
		return;
	}

	TArray<FString> Parts;
	AreaDataRecord.RelativePath.ParseIntoArray(Parts, TEXT("/"), true);

	if (Parts.Num() >= 3)
	{
		OutMapName = Parts[Parts.Num() - 3];
		OutCellName = Parts[Parts.Num() - 2];
		return;
	}

	Parts.Reset();
	AreaDataRecord.ContentPath.ParseIntoArray(Parts, TEXT("/"), true);
	OutMapName = Parts.Num() >= 3 ? Parts[Parts.Num() - 3] : AreaDataRecord.PackName;
	OutCellName = Parts.Num() >= 2 ? Parts[Parts.Num() - 2] : FString();

	if (OutMapName.IsEmpty() || OutCellName.IsEmpty())
	{
		OutMapName = AreaDataRecord.PackName;
		OutCellName = TEXT("UnknownCell");
	}
}

FString FMT2PropertyResolver::NormalizeReferencePath(const FString& RawPath)
{
	FString Path = RawPath;
	Path.TrimStartAndEndInline();
	Path.ReplaceInline(TEXT("\\"), TEXT("/"));
	Path.RemoveFromStart(TEXT("\""));
	Path.RemoveFromEnd(TEXT("\""));

	if (Path.StartsWith(TEXT("d:/"), ESearchCase::IgnoreCase))
	{
		return Path;
	}

	if (Path.StartsWith(TEXT("ymir work/"), ESearchCase::IgnoreCase))
	{
		return FString(TEXT("d:/")) + Path;
	}

	return Path;
}

bool FMT2PropertyResolver::ParseTokenLine(const FString& Line, FString& OutKey, TArray<FString>& OutValues)
{
	FString Trimmed = Line.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		return false;
	}

	int32 KeyEnd = INDEX_NONE;
	for (int32 Index = 0; Index < Trimmed.Len(); ++Index)
	{
		if (FChar::IsWhitespace(Trimmed[Index]))
		{
			KeyEnd = Index;
			break;
		}
	}

	if (KeyEnd == INDEX_NONE)
	{
		return false;
	}

	OutKey = Trimmed.Left(KeyEnd).ToLower();
	FString Rest = Trimmed.Mid(KeyEnd).TrimStartAndEnd();

	while (!Rest.IsEmpty())
	{
		if (Rest[0] == TCHAR('"'))
		{
			Rest = Rest.Mid(1);
			int32 QuoteEnd = INDEX_NONE;
			if (!Rest.FindChar(TCHAR('"'), QuoteEnd))
			{
				break;
			}

			OutValues.Add(Rest.Left(QuoteEnd));
			Rest = Rest.Mid(QuoteEnd + 1).TrimStartAndEnd();
		}
		else
		{
			FString Token;
			FString Remainder;
			if (Rest.Split(TEXT(" "), &Token, &Remainder))
			{
				OutValues.Add(Token.TrimStartAndEnd());
				Rest = Remainder.TrimStartAndEnd();
			}
			else
			{
				OutValues.Add(Rest);
				break;
			}
		}
	}

	return !OutKey.IsEmpty();
}

bool FMT2PropertyResolver::ParseFloatTriple(const FString& Line, FVector& OutVector)
{
	TArray<FString> Tokens;
	Line.ParseIntoArrayWS(Tokens);
	if (Tokens.Num() < 3)
	{
		return false;
	}

	OutVector.X = FCString::Atof(*Tokens[0]);
	OutVector.Y = FCString::Atof(*Tokens[1]);
	OutVector.Z = FCString::Atof(*Tokens[2]);
	return true;
}

bool FMT2PropertyResolver::ParseRotation(const FString& Line, FRotator& OutRotation)
{
	TArray<FString> Tokens;
	Line.ParseIntoArray(Tokens, TEXT("#"), true);
	if (Tokens.Num() >= 3)
	{
		const float Yaw = FCString::Atof(*Tokens[0]);
		const float Pitch = FCString::Atof(*Tokens[1]);
		const float Roll = FCString::Atof(*Tokens[2]);
		OutRotation = FRotator(Pitch, Yaw, Roll);
		return true;
	}

	OutRotation = FRotator(0.0f, FCString::Atof(*Line), 0.0f);
	return true;
}
