/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Items/Use/MT2MobLureItemTemplate.h"

#include "Characters/MT2PlayerCharacter.h"
#include "Components/MT2HealthComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "Mobs/MT2Mob.h"
#include "Mobs/MT2MobAIComponent.h"

EMT2ItemUseExecution UMT2MobLureItemTemplate::ExecuteUse(
	AMT2PlayerCharacter& Character, UMT2InventoryComponent& Inventory,
	int32 InventorySlot) const
{
	UWorld* World = Character.GetWorld();
	if (!Character.HasAuthority() || !World || LureRadius <= 0.0f)
	{
		return EMT2ItemUseExecution::Rejected;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MT2MobLure), false, &Character);
	FCollisionObjectQueryParams ObjectTypes;
	// Mob capsules use the dedicated "Mob" object channel after the collision optimization.
	// Keep Pawn too so older/custom mob Blueprints remain compatible.
	ObjectTypes.AddObjectTypesToQuery(ECC_Pawn);
	ObjectTypes.AddObjectTypesToQuery(ECC_GameTraceChannel1);
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(
		Overlaps, Character.GetActorLocation(), FQuat::Identity, ObjectTypes,
		FCollisionShape::MakeSphere(LureRadius), QueryParams);

	TSet<TObjectPtr<AMT2Mob>> ProcessedMobs;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AMT2Mob* Mob = Cast<AMT2Mob>(Overlap.GetActor());
		if (!Mob || ProcessedMobs.Contains(Mob) || Mob->GetMobType() != EMT2MobType::Monster ||
			!Mob->GetHealthComponent() || Mob->GetHealthComponent()->IsDead())
		{
			continue;
		}
		ProcessedMobs.Add(Mob);
		if (UMT2MobAIComponent* AI = Mob->GetMobAIComponent())
		{
			if (!AI->GetTargetActor() &&
				FMath::RandHelper(100) < FMath::Clamp(AggroChancePercent, 0, 100))
			{
				AI->SetTargetActor(&Character);
			}
		}
	}

	return EMT2ItemUseExecution::Consume;
}
