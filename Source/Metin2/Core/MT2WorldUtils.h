/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"

class UGameInstance;

namespace MT2WorldUtils
{
	// Resolves a game instance from actors, components, widgets, subsystems, or the game instance itself.
	METIN2_API UGameInstance* GetGameInstance(const UObject* WorldContextObject);
}
