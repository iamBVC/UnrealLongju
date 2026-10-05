/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Importers/MT2GrannyMeshConverter.h"
#include "MT2AssetScanner.h"

/** Resolves the GR2 sources that retain skinning because gameplay uses them or animations bind to them. */
class FMT2MeshUsageClassifier
{
public:
	explicit FMT2MeshUsageClassifier(const FMT2AssetScanResult* ScanResult);

	bool IsReady() const { return bReady; }
	bool IsUsedByCharacter(const FMT2AssetRecord& Record) const;
	bool HasBoundAnimation(
		const FMT2AssetRecord& Record,
		const FMT2GrannyFileInspection& Inspection) const;
	bool RequiresSkeletalMesh(
		const FMT2AssetRecord& Record,
		const FMT2GrannyFileInspection& Inspection) const;
	FString GetCanonicalModelPath(const FMT2AssetRecord& Record) const;
	int32 GetCharacterMeshCount() const { return CharacterMeshPaths.Num(); }
	int32 GetAnimatedMeshCount() const { return AnimationBoundMeshPaths.Num(); }
	const TArray<FString>& GetWarnings() const { return Warnings; }

private:
	bool bReady = false;
	TSet<FString> CharacterMeshPaths;
	TSet<FString> AnimationBoundMeshPaths;
	TMap<FString, FString> CanonicalModelPaths;
	TArray<FString> Warnings;
};
