/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"

class UMaterial;

struct FMT2MaterialPolicyResult
{
	int32 MaterialsChanged = 0;
	int32 LandscapeMaterialsSkipped = 0;
	int32 MaterialsAlreadyCompliant = 0;
	int32 SaveFailures = 0;
};

class FMT2MaterialPolicy
{
public:
	static bool IsLandscapeMaterial(const UMaterial* Material);
	static bool Apply(UMaterial* Material);
	static bool ApplyToProject(FMT2MaterialPolicyResult& OutResult, bool bSavePackages);
};
