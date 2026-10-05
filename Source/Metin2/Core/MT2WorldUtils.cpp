/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Core/MT2WorldUtils.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"

UGameInstance* MT2WorldUtils::GetGameInstance(const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}
	if (UGameInstance* GameInstance = const_cast<UGameInstance*>(Cast<UGameInstance>(WorldContextObject)))
	{
		return GameInstance;
	}
	const UWorld* World = WorldContextObject->GetWorld();
	return World ? World->GetGameInstance() : nullptr;
}
