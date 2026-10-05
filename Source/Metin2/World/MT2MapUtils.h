/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"

namespace MT2MapUtils
{
#if WITH_EDITOR
	// Resolves server/client map aliases and deleted redirectors to the actual imported world package.
	METIN2_API FString ResolvePIEWorldPackage(const TArray<FString>& MapAliases);
#endif
}
