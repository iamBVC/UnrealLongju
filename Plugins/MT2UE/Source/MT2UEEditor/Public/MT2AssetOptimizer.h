/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"

class UStaticMesh;
class USkeletalMesh;
class UTexture2D;

struct FMT2AssetOptimizationResult
{
	int32 StaticMeshes = 0;
	int32 SkeletalMeshes = 0;
	int32 Textures = 0;
	int32 WorldComponents = 0;
	TArray<FString> Warnings;

	FString BuildSummary() const;
};

class MT2UEEDITOR_API FMT2AssetOptimizer
{
public:
	static bool OptimizeImportedAssets(FMT2AssetOptimizationResult& OutResult);
	static bool ConfigureStaticMesh(UStaticMesh* Mesh);
	static bool ConfigureSkeletalMesh(USkeletalMesh* Mesh);
	static bool ConfigureTexture(UTexture2D* Texture);
	static float ComputeMapObjectCullDistance(const FBoxSphereBounds& Bounds);
};
