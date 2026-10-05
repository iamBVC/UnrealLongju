/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"

class AActor;
class AMT2PlayerState;

namespace MT2ActorUtils
{
	// Resolves the player state represented by a state, controller, pawn, or actor instigator.
	METIN2_API AMT2PlayerState* ResolvePlayerState(AActor* Actor, bool bFollowInstigator = true);
	METIN2_API const AMT2PlayerState* ResolvePlayerState(
		const AActor* Actor, bool bFollowInstigator = true);
}
