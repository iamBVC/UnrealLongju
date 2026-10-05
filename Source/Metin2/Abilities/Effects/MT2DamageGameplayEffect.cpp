/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Abilities/Effects/MT2DamageGameplayEffect.h"

#include "Abilities/MT2CoreAttributeSet.h"
#include "Abilities/MT2GameplayTags.h"

UMT2DamageGameplayEffect::UMT2DamageGameplayEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	FSetByCallerFloat DamageMagnitude;
	DamageMagnitude.DataTag = MT2GameplayTags::Data_Damage;

	FGameplayModifierInfo& Modifier = Modifiers.AddDefaulted_GetRef();
	Modifier.Attribute = UMT2CoreAttributeSet::GetDamageAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(DamageMagnitude);
}
