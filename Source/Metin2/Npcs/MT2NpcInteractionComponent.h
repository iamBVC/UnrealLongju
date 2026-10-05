/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Npcs/MT2NpcTypes.h"
#include "MT2NpcInteractionComponent.generated.h"

class AMT2PlayerCharacter;

// Click behavior of an NPC: dispatches by the imported mob_proto OnClickType - shop NPCs open
// their shop, talk NPCs go to the quest system (the old CQuestManager::Click path). Config lives
// in the NPC Blueprint's class defaults, written by the importer.
UCLASS(ClassGroup = "MT2", meta = (BlueprintSpawnableComponent))
class METIN2_API UMT2NpcInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// Server: full interaction dispatch (range check, face the player, OnClickType routing).
	// Returns false when out of range or the NPC has no interaction.
	bool Interact(AMT2PlayerCharacter* Player);

	UFUNCTION(BlueprintPure, Category = "NPC")
	EMT2NpcOnClickType GetOnClickType() const { return OnClickType; }

	UFUNCTION(BlueprintPure, Category = "NPC")
	float GetInteractionRange() const { return InteractionRange; }

	void SetOnClickType(EMT2NpcOnClickType NewType) { OnClickType = NewType; }

private:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPC", meta = (AllowPrivateAccess = "true"))
	EMT2NpcOnClickType OnClickType = EMT2NpcOnClickType::None;

	// Old shop/talk distance (~250cm) with a little slack for latency.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPC", meta = (Units = "cm", AllowPrivateAccess = "true"))
	float InteractionRange = 250.0f;
};
