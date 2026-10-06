/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"

class UWorld;
class AActor;

namespace MT2MapUtils
{
#if WITH_EDITOR
	// Resolves server/client map aliases and deleted redirectors to the actual imported world package.
	METIN2_API FString ResolvePIEWorldPackage(const TArray<FString>& MapAliases);
	METIN2_API bool HasPendingPIETravel(const UWorld& World);
	// Complete source partition teardown before seamless travel/network actor cleanup.
	METIN2_API void PreparePIEClientWorldForTravel(UWorld& World, bool bSeamless);
#endif
}
