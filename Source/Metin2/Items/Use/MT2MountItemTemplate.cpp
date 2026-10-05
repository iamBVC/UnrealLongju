/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Items/Use/MT2MountItemTemplate.h"

#include "Characters/MT2PlayerCharacter.h"
#include "Mounts/MT2MountComponent.h"
#include "Mounts/MT2MountDefinition.h"
#include "Player/MT2PlayerState.h"

EMT2ItemUseExecution UMT2MountItemTemplate::ExecuteUse(
	AMT2PlayerCharacter& Character, UMT2InventoryComponent& Inventory,
	int32 InventorySlot) const
{
	UMT2MountComponent* Mounts = Character.GetMountComponent();
	UMT2MountDefinition* Definition = MountDefinition.LoadSynchronous();
	const AMT2PlayerState* PlayerState = Character.GetPlayerState<AMT2PlayerState>();
	if (!Character.HasAuthority() || !Mounts || !Definition ||
		(MinimumPlayerLevel > 0 &&
			(!PlayerState || PlayerState->GetCharacterLevel() < MinimumPlayerLevel)))
	{
		return EMT2ItemUseExecution::Rejected;
	}
	if (Mounts->IsMounted())
	{
		Mounts->ForceDismount();
		return EMT2ItemUseExecution::Accepted;
	}
	if (!Mounts->CallSpecialMount(Definition, RideDurationSeconds))
	{
		return EMT2ItemUseExecution::Rejected;
	}
	return bConsumeOnMount ? EMT2ItemUseExecution::Consume : EMT2ItemUseExecution::Accepted;
}
