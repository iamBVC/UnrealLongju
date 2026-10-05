/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"

class FMT2VnumRegistryBuilder
{
public:
	static bool RegistryExists();
	static bool RebuildAndSave(TArray<FString>& OutErrors, int32& OutMobCount, int32& OutItemCount);
};
