/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Player/MT2PlayerTypes.h"
#include "Stats/MT2StatTypes.h"

struct FMT2PlayerDerivedStats
{
	FMT2CombatStats Combat;
	float MaxMana = 0.0f;
	float MaxStamina = 0.0f;
};

namespace MT2PlayerStatFormula
{
	METIN2_API FMT2PrimaryStats GetInitialPrimaryStats(EMT2CharacterRace Race);
	METIN2_API FMT2PlayerDerivedStats Calculate(
		EMT2CharacterRace Race, int32 Level, const FMT2PrimaryStats& PrimaryStats);
	METIN2_API float CalculateAttackRating(
		int32 AttackerLevel, int32 AttackerDexterity, int32 DefenderLevel, int32 DefenderDexterity);
}
