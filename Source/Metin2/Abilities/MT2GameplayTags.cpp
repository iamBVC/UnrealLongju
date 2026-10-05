/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Abilities/MT2GameplayTags.h"

namespace MT2GameplayTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Attack, "Ability.Attack", "Tag used to activate the primary attack ability.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Data_Damage, "Data.Damage", "Set-by-caller damage magnitude.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Skill, "Skill", "Root tag for player skills.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Status, "Status", "Root tag for status effects.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Status_Dead, "Status.Dead", "Actor has no health and cannot act.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Attacking, "State.Attacking", "Owned while a basic attack is swinging; blocks free movement input.");
}
