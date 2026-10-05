/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MT2GatewayGameMode.generated.h"

// Game mode for the Gateway (login) map: no playable pawn, just the login/character-select/create
// UI chain via AMT2GatewayHUD, on top of the standard AMT2PlayerController gateway RPCs. Assign this
// as the Gateway map's game mode override.
UCLASS(Blueprintable)
class METIN2_API AMT2GatewayGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMT2GatewayGameMode();
};
