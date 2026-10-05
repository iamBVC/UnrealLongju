/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "MT2DamageTypes.generated.h"

UENUM(BlueprintType)
enum class EMT2DamageDisplayType : uint8
{
	Normal,
	Critical,
	Penetrating,
	Poison,
	CriticalPenetrating
};
