/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Mobs/MT2MobAIController.h"

#include "Mobs/MT2Mob.h"
#include "Mobs/MT2MobAIComponent.h"

AMT2MobAIController::AMT2MobAIController()
{
	PrimaryActorTick.bCanEverTick = false;
	bAttachToPawn = true;
}

void AMT2MobAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	if (AMT2Mob* Mob = Cast<AMT2Mob>(InPawn))
	{
		Mob->GetMobAIComponent()->InitializeHome(Mob->GetActorLocation());
		Mob->GetMobAIComponent()->ApplyTickPolicy();
	}
}
