/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "AbilitySystemGlobals.h"

#include "MT2AbilitySystemGlobals.generated.h"

UCLASS()
class METIN2_API UMT2AbilitySystemGlobals : public UAbilitySystemGlobals
{
	GENERATED_BODY()

public:
	virtual void StartAsyncLoadingObjectLibraries() override
	{
		// MT2 currently has no GameplayCueNotify assets. Avoid synchronously scanning the large
		// imported content tree every time PIE creates its first ability-system component.
	}
};
