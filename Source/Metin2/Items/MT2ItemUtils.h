/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"

class UMT2ItemTemplate;
struct FMT2ItemSlot;

namespace MT2ItemUtils
{
	METIN2_API const UMT2ItemTemplate* ResolveTemplate(const UObject* WorldContextObject, int32 Vnum);
	METIN2_API int32 GetInventorySize(const UObject* WorldContextObject, int32 Vnum);
	METIN2_API FText GetDisplayName(const UMT2ItemTemplate* Template, int32 FallbackVnum = 0);
	METIN2_API bool HaveSameInstanceData(const FMT2ItemSlot& Left, const FMT2ItemSlot& Right);

	template <typename T>
	const T* ResolveTemplate(const UObject* WorldContextObject, int32 Vnum)
	{
		return Cast<T>(ResolveTemplate(WorldContextObject, Vnum));
	}
}
