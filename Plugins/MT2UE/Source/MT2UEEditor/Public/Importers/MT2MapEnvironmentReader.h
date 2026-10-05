/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"

struct FMT2MapEnvironmentData
{
	FString SourcePath;
	bool bDirectionalLightEnabled = false;
	FVector Direction = FVector(0.5, 0.5, -0.5);
	FLinearColor DirectionalDiffuse = FLinearColor::White;
	FLinearColor DirectionalAmbient = FLinearColor::Black;
	FLinearColor MaterialAmbient = FLinearColor(0.8f, 0.8f, 0.8f);
	FLinearColor MaterialEmissive = FLinearColor(0.8f, 0.8f, 0.8f);
	bool bFogEnabled = false;
	bool bDensityFog = false;
	float FogNearDistance = 12800.0f;
	float FogFarDistance = 17920.0f;
	FLinearColor FogColor = FLinearColor(0.5f, 0.5f, 0.5f);
};

class FMT2MapEnvironmentReader
{
public:
	static bool Load(const FString& Path, FMT2MapEnvironmentData& OutData, FString& OutError);
};
