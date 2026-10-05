/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Components/MT2StatusEffectComponent.h"
#include "MT2StatusEffectDefinition.generated.h"

class AMT2CharacterBase;

// Stable affect ids for the potion effects, so they show up / can be removed by a known Type and never
// collide with skill vnums (which are small). Deny-while-active is keyed on Kind, not Type.
namespace MT2AffectId
{
	constexpr int32 HealthRecovery = 30001;
	constexpr int32 ManaRecovery = 30002;
	constexpr int32 AttackSpeed = 30003;
	constexpr int32 MovementSpeed = 30004;
	constexpr int32 AutoHealthRecovery = 534;
	constexpr int32 AutoManaRecovery = 535;
	// Original server affect ids. These are persistent one-shot book-reading modifiers.
	constexpr int32 SkillBookGuaranteedSuccess = 512;
	constexpr int32 SkillBookNoCooldown = 513;
}

// Behaviour for one kind of status effect. There is exactly one subclass per effect, so each effect's
// logic (can it be applied, what it does each second, what happens when it ends) lives in its own place
// instead of a shared switch. Instances are stateless singletons (CDOs) resolved via Get(); all mutable
// state lives in the replicated FMT2StatusEffect record the methods receive.
//
// The base class IS the generic "apply a stat while active" affect (EMT2StatusEffectKind::StatBonus)
// used by skills/equipment/admin: it drains SP upkeep and counts down the duration, and the character
// reads ApplyType/ApplyValue off the effect list when recomputing bonuses (so attack/move-speed buffs
// need no code here - they just carry ApplyType 7/8).
UCLASS()
class METIN2_API UMT2StatusEffectDefinition : public UObject
{
	GENERATED_BODY()

public:
	// May this effect be (re)applied to Target right now? Return false to reject the item use.
	virtual bool CanApply(const AMT2CharacterBase* Target, const FMT2StatusEffect& Effect) const { return true; }

	// When true, re-using this effect while one of the same Kind is active is rejected rather than
	// refreshed (potions don't stack). StatBonus/skill buffs return false and use override semantics.
	virtual bool DeniesReapplyWhileActive() const { return false; }

	// Called once when the effect is added (after it is in the list). Override for side effects.
	virtual void OnApplied(AMT2CharacterBase* Target, FMT2StatusEffect& Effect) const {}

	// Called once per second. Mutate Effect as needed and return true when the effect is finished and
	// should be removed. Base: pay SP upkeep (end if unaffordable), then decrement a finite duration.
	virtual bool Tick(AMT2CharacterBase* Target, FMT2StatusEffect& Effect) const;

	// Called once when the effect is removed (expiry or clear). Override for cleanup.
	virtual void OnRemoved(AMT2CharacterBase* Target, const FMT2StatusEffect& Effect) const {}

	// Player-facing name shown in the status-effect bar tooltip.
	virtual FText GetDisplayName() const;

	// Full player-facing effect description used by tooltips. Generic stat affects resolve their
	// EApplyTypes value (for example "+50% EXP") instead of displaying the generic "Buff" label.
	virtual FText GetDescription(const FMT2StatusEffect& Effect) const;

	// Short magnitude badge drawn on the status-bar icon (e.g. "+30%", "+600"). Empty for no badge.
	virtual FText GetMagnitudeLabel(const FMT2StatusEffect& Effect) const;

	// Resolve the singleton definition for a kind. Add a case when you add an effect class.
	static const UMT2StatusEffectDefinition* Get(EMT2StatusEffectKind Kind);
};

// Heals HP from a pool over time. The remaining pool is stored in Effect.ApplyValue and drained each
// second (up to a % of max HP, mirroring the old 9%/3s recovery); the effect ends when the pool empties
// or HP is full. CanApply rejects the potion when HP is already full.
UCLASS()
class METIN2_API UMT2StatusEffect_HealthRecovery : public UMT2StatusEffectDefinition
{
	GENERATED_BODY()

public:
	virtual bool CanApply(const AMT2CharacterBase* Target, const FMT2StatusEffect& Effect) const override;
	virtual bool Tick(AMT2CharacterBase* Target, FMT2StatusEffect& Effect) const override;
	virtual FText GetDisplayName() const override;
	virtual FText GetMagnitudeLabel(const FMT2StatusEffect& Effect) const override;
};

// Heals SP from a pool over time; same model as HealthRecovery on the mana component.
UCLASS()
class METIN2_API UMT2StatusEffect_ManaRecovery : public UMT2StatusEffectDefinition
{
	GENERATED_BODY()

public:
	virtual bool CanApply(const AMT2CharacterBase* Target, const FMT2StatusEffect& Effect) const override;
	virtual bool Tick(AMT2CharacterBase* Target, FMT2StatusEffect& Effect) const override;
	virtual FText GetDisplayName() const override;
	virtual FText GetMagnitudeLabel(const FMT2StatusEffect& Effect) const override;
};

UCLASS()
class METIN2_API UMT2StatusEffect_AutoHealthRecovery : public UMT2StatusEffectDefinition
{
	GENERATED_BODY()
public:
	virtual FText GetDisplayName() const override;
	virtual FText GetDescription(const FMT2StatusEffect& Effect) const override;
	virtual FText GetMagnitudeLabel(const FMT2StatusEffect& Effect) const override;
};

UCLASS()
class METIN2_API UMT2StatusEffect_AutoManaRecovery : public UMT2StatusEffectDefinition
{
	GENERATED_BODY()
public:
	virtual FText GetDisplayName() const override;
	virtual FText GetDescription(const FMT2StatusEffect& Effect) const override;
	virtual FText GetMagnitudeLabel(const FMT2StatusEffect& Effect) const override;
};

// Timed +% attack-speed buff. The magnitude rides on ApplyType=APPLY_ATT_SPEED/ApplyValue and is applied
// by the character's stat recompute; here we only forbid stacking (deny re-use while active).
UCLASS()
class METIN2_API UMT2StatusEffect_AttackSpeed : public UMT2StatusEffectDefinition
{
	GENERATED_BODY()

public:
	virtual bool DeniesReapplyWhileActive() const override { return true; }
	virtual FText GetDisplayName() const override;
	virtual FText GetMagnitudeLabel(const FMT2StatusEffect& Effect) const override;
};

// Timed +% movement-speed buff (ApplyType=APPLY_MOV_SPEED); non-stacking like the attack-speed buff.
UCLASS()
class METIN2_API UMT2StatusEffect_MovementSpeed : public UMT2StatusEffectDefinition
{
	GENERATED_BODY()

public:
	virtual bool DeniesReapplyWhileActive() const override { return true; }
	virtual FText GetDisplayName() const override;
	virtual FText GetMagnitudeLabel(const FMT2StatusEffect& Effect) const override;
};
