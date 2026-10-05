/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"

enum class EMT2AssetKind : uint8
{
	Unknown,
	Texture,
	Granny,
	ModelScript,
	MotionScript,
	Property,
	Terrain,
	MapText,
	EffectScript,
	Tree,
	Audio
};

struct FMT2AssetRecord
{
	FString AbsolutePath;
	FString RelativePath;
	FString ContentPath;
	FString VirtualPath;
	FString PackName;
	FString Extension;
	EMT2AssetKind Kind = EMT2AssetKind::Unknown;
	int64 Size = 0;
};

struct FMT2AssetScanResult
{
	FString RootDirectory;
	TArray<FMT2AssetRecord> Records;
	TMap<FString, int32> ExtensionCounts;
	TMap<EMT2AssetKind, int32> KindCounts;

	int32 GetCount(EMT2AssetKind Kind) const;
	TArray<FMT2AssetRecord> GetRecordsByKind(EMT2AssetKind Kind) const;
};

class FMT2AssetScanner
{
public:
	bool Scan(const FString& RootDirectory, FMT2AssetScanResult& OutResult, FString& OutError) const;

	static EMT2AssetKind ClassifyExtension(const FString& Extension, const FString& FileName);
	static FString KindToString(EMT2AssetKind Kind);
	static FString NormalizeVirtualPath(const FString& RelativePath, FString& OutPackName, FString& OutContentPath);
	static FString SanitizePackagePathSegment(const FString& Segment);
	static FString BuildContentPackagePath(const FString& DestinationRoot, const FMT2AssetRecord& Record);
	static FString BuildContentObjectPath(const FString& DestinationRoot, const FMT2AssetRecord& Record);
};
