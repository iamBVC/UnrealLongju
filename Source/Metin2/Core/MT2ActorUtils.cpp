/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Core/MT2ActorUtils.h"

#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Player/MT2PlayerState.h"

AMT2PlayerState* MT2ActorUtils::ResolvePlayerState(AActor* Actor, bool bFollowInstigator)
{
	return const_cast<AMT2PlayerState*>(ResolvePlayerState(
		static_cast<const AActor*>(Actor), bFollowInstigator));
}

const AMT2PlayerState* MT2ActorUtils::ResolvePlayerState(
	const AActor* Actor, bool bFollowInstigator)
{
	if (const AMT2PlayerState* State = Cast<AMT2PlayerState>(Actor))
	{
		return State;
	}
	if (const AController* Controller = Cast<AController>(Actor))
	{
		return Controller->GetPlayerState<AMT2PlayerState>();
	}
	if (const APawn* Pawn = Cast<APawn>(Actor))
	{
		return Pawn->GetPlayerState<AMT2PlayerState>();
	}
	const AController* Instigator = bFollowInstigator && Actor
		? Actor->GetInstigatorController() : nullptr;
	return Instigator ? Instigator->GetPlayerState<AMT2PlayerState>() : nullptr;
}
