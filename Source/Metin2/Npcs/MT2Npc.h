/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Mobs/MT2Mob.h"
#include "MT2Npc.generated.h"

class UMT2NpcInteractionComponent;
class UMT2QuestArrowComponent;
class UMT2NpcShopComponent;

// An NPC is a mob_proto row with bType NPC/WARP/GOTO - same data pipeline, meshes and motions as
// monsters (exactly the old game's model), but clicking interacts instead of attacking, and it
// takes no damage. The importer generates BP_Npc_* Blueprints from this class under /Game/Npcs.
UCLASS(Blueprintable)
class METIN2_API AMT2Npc : public AMT2Mob
{
	GENERATED_BODY()

public:
	AMT2Npc();

	virtual bool IsAttackable() const override { return false; }
	virtual void ConfigureFromDefinition(const FMT2MobDefinition& Definition) override;

	UFUNCTION(BlueprintPure, Category = "NPC")
	UMT2NpcInteractionComponent* GetInteractionComponent() const { return InteractionComponent; }

	UFUNCTION(BlueprintPure, Category = "NPC")
	UMT2NpcShopComponent* GetShopComponent() const { return ShopComponent; }

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2NpcInteractionComponent> InteractionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2NpcShopComponent> ShopComponent;

	// Floats a quest arrow overhead while an active quest points the local player at this NPC.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2QuestArrowComponent> QuestArrowComponent;
};
