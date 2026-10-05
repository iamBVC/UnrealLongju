/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"

// Runtime evaluator for the old game's skill polys (skilltable POINT_POLY / SP_COST_POLY / ...),
// e.g. "-(1.1*atk + (0.3*atk + 0.5*str + wep)*k)". Supports + - * / , parentheses, numbers,
// named variables, floor()/ceil() and number(a,b) (random range, as the old CPoly did).
// The importer bakes the stat-free formulas into curves; the stat-dependent ones (damage) must be
// evaluated at cast time with the caster's live stats, which is what this is for.
namespace MT2SkillFormula
{
	// Returns false when the expression is malformed or uses an unknown variable.
	METIN2_API bool Evaluate(
		const FString& Expression, const TMap<FString, double>& Variables, double& OutValue);
}
