/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Items/Use/MT2SkillTrainingItemTemplates.h"

#include "Characters/MT2PlayerCharacter.h"
#include "Player/MT2PlayerState.h"

EMT2ItemUseExecution UMT2GrandMasterTrainingItemTemplate::ExecuteUse(
	AMT2PlayerCharacter& Character, UMT2InventoryComponent& Inventory,
	int32 InventorySlot) const
{
	return Character.BeginGrandMasterTraining(InventorySlot)
		? EMT2ItemUseExecution::Accepted : EMT2ItemUseExecution::Rejected;
}

EMT2ItemUseExecution UMT2KarmaRecoveryItemTemplate::ExecuteUse(
	AMT2PlayerCharacter& Character, UMT2InventoryComponent& Inventory,
	int32 InventorySlot) const
{
	AMT2PlayerState* State = Character.GetPlayerState<AMT2PlayerState>();
	if (!State || State->GetRawAlignment() >= 0 || KarmaRestored <= 0)
	{
		return EMT2ItemUseExecution::Rejected;
	}
	State->SetRawAlignment(static_cast<int32>(FMath::Min<int64>(int64(State->GetRawAlignment()) + int64(KarmaRestored) * 10, 0)));
	return EMT2ItemUseExecution::Consume;
}
