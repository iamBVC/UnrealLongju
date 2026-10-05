/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Npcs/MT2NpcInteractionComponent.h"

#include "Characters/MT2PlayerCharacter.h"
#include "GameFramework/Actor.h"
#include "Npcs/MT2Npc.h"
#include "Player/MT2PlayerState.h"
#include "Quests/MT2QuestManagerComponent.h"

bool UMT2NpcInteractionComponent::Interact(AMT2PlayerCharacter* Player)
{
	AMT2Npc* Npc = Cast<AMT2Npc>(GetOwner());
	if (!Npc || !Player || !Npc->HasAuthority() || OnClickType == EMT2NpcOnClickType::None)
	{
		return false;
	}

	// Slack over the configured range covers client/server position drift.
	const float MaxDistance = InteractionRange + 100.0f;
	if (FVector::Dist2D(Npc->GetActorLocation(), Player->GetActorLocation()) > MaxDistance)
	{
		return false;
	}

	// The old client turns the NPC toward whoever talks to it.
	FVector ToPlayer = Player->GetActorLocation() - Npc->GetActorLocation();
	ToPlayer.Z = 0.0;
	if (!ToPlayer.IsNearlyZero())
	{
		Npc->SetActorRotation(FRotator(0.0, ToPlayer.Rotation().Yaw, 0.0));
	}

	// Both shop and talk NPCs go through the quest menu first. It gathers the shop entry (when this NPC
	// has one) together with every quest option available here, so a vendor that is also a quest target
	// lets the player choose instead of the shop pre-empting the quest. The menu is skipped when there
	// is only one thing to do, so a plain vendor still opens its shop on a single click.
	{
		const bool bHasShop = OnClickType == EMT2NpcOnClickType::Shop;
		AMT2PlayerState* State = Player->GetPlayerState<AMT2PlayerState>();
		UMT2QuestManagerComponent* QuestManager = State ? State->GetQuestManagerComponent() : nullptr;
		if (QuestManager && QuestManager->DispatchNpcInteraction(Npc->GetMobVnum(), Npc, bHasShop))
		{
			return true;
		}
	}

	switch (OnClickType)
	{
	case EMT2NpcOnClickType::Shop:
		Player->ClientOpenNpcShop(Npc);
		return true;
	case EMT2NpcOnClickType::Talk:
		// Reached only when no quest offered anything for this NPC. There is nothing to say, so nothing
		// opens - the old game is silent here too, and the click type is set on mounts and other
		// non-speaking mobs that should never produce a dialog.
		return false;
	default:
		return false;
	}
}
