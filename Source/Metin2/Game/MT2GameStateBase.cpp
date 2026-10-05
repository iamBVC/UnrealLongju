/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Game/MT2GameStateBase.h"

#include "Net/UnrealNetwork.h"
#include "Server/MT2ServerRuntimeSubsystem.h"

void AMT2GameStateBase::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority()) return;

	if (const UMT2ServerRuntimeSubsystem* Runtime = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMT2ServerRuntimeSubsystem>() : nullptr;
		Runtime && Runtime->IsMapServer())
	{
		MapId = Runtime->GetMapId();
		ServerChannel = Runtime->GetChannel();
	}
}

void AMT2GameStateBase::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMT2GameStateBase, MapId);
	DOREPLIFETIME(AMT2GameStateBase, ServerChannel);
}
