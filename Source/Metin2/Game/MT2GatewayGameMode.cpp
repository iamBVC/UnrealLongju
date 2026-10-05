/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Game/MT2GatewayGameMode.h"

#include "GameFramework/SpectatorPawn.h"
#include "Player/MT2PlayerController.h"
#include "UI/MT2GatewayHUD.h"

AMT2GatewayGameMode::AMT2GatewayGameMode()
{
	// No playable character on the gateway map - a spectator pawn keeps the camera at whatever the
	// user places (PlayerStart / camera actor) while the login UI drives everything.
	DefaultPawnClass = ASpectatorPawn::StaticClass();
	PlayerControllerClass = AMT2PlayerController::StaticClass();
	HUDClass = AMT2GatewayHUD::StaticClass();
}
