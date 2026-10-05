/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Npcs/MT2Npc.h"

#include "GameFramework/CharacterMovementComponent.h"
#include "Npcs/MT2NpcInteractionComponent.h"
#include "Npcs/MT2NpcShopComponent.h"
#include "UI/MT2QuestArrowComponent.h"

AMT2Npc::AMT2Npc()
{
	InteractionComponent = CreateDefaultSubobject<UMT2NpcInteractionComponent>(TEXT("InteractionComponent"));
	ShopComponent = CreateDefaultSubobject<UMT2NpcShopComponent>(TEXT("ShopComponent"));

	// Overhead quest marker; hides itself unless an active quest points at this NPC.
	QuestArrowComponent = CreateDefaultSubobject<UMT2QuestArrowComponent>(TEXT("QuestArrowComponent"));
	QuestArrowComponent->SetupAttachment(GetRootComponent());

	// No AI controller at all: NPCs never wander, chase or search for targets. This is what
	// actually keeps them still - the mob AI is driven by AMT2MobAIController, not a component tick.
	AutoPossessAI = EAutoPossessAI::Disabled;
	AIControllerClass = nullptr;
}

void AMT2Npc::ConfigureFromDefinition(const FMT2MobDefinition& Definition)
{
	Super::ConfigureFromDefinition(Definition);
	InteractionComponent->SetOnClickType(static_cast<EMT2NpcOnClickType>(
		FMath::Clamp(Definition.OnClickType, 0, 2)));
}

void AMT2Npc::BeginPlay()
{
	Super::BeginPlay();

	// NPCs never fight or die. Everything else (mesh, motions, nameplate) stays inherited mob
	// behavior, matching the old game where NPCs are just mob rows.
	SetCanBeDamaged(false);
	GetCharacterMovement()->DisableMovement();
}
