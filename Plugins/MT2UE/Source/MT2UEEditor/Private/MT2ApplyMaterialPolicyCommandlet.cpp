/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "MT2ApplyMaterialPolicyCommandlet.h"

#include "MT2MaterialPolicy.h"

UMT2ApplyMaterialPolicyCommandlet::UMT2ApplyMaterialPolicyCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UMT2ApplyMaterialPolicyCommandlet::Main(const FString& Params)
{
	FMT2MaterialPolicyResult Result;
	const bool bSucceeded = FMT2MaterialPolicy::ApplyToProject(Result, true);
	UE_LOG(LogTemp, Display,
		TEXT("[MT2Materials] Two-sided migration: %d changed, %d already compliant, %d landscape skipped, %d save failures."),
		Result.MaterialsChanged, Result.MaterialsAlreadyCompliant,
		Result.LandscapeMaterialsSkipped, Result.SaveFailures);
	return bSucceeded ? 0 : 1;
}
