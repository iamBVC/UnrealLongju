/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Components/MT2StatusEffectDefinition.h"

#include "Characters/MT2CharacterBase.h"
#include "Components/MT2HealthComponent.h"
#include "Components/MT2ManaComponent.h"
#include "Items/MT2ItemBonusSettings.h"

namespace
{
	// Per-second drain of the recovery pool, as a % of the resource maximum. The old game drained the
	// pool every 3s (HP 9%, SP 7%); this component ticks every 1s, so a third of those keeps the same
	// overall rate. A minimum of 1 guarantees the pool always empties, so the effect can never get stuck.
	constexpr int32 HealthRecoverPercentPerSecond = 3;
	constexpr int32 ManaRecoverPercentPerSecond = 3;

	// Heals from a pool held in Effect.ApplyValue; returns true when the effect is finished. Shared by
	// the HP and SP recovery classes since only the resource getters/setters differ.
	template <typename FGetCurrent, typename FGetMax, typename FSetCurrent>
	bool DrainRecoveryPool(FMT2StatusEffect& Effect, int32 PercentPerSecond,
		FGetCurrent&& GetCurrent, FGetMax&& GetMax, FSetCurrent&& SetCurrent)
	{
		const float Current = GetCurrent();
		const float Max = GetMax();
		if (Effect.ApplyValue <= 0 || Current >= Max)
		{
			return true; // pool spent or resource full -> done
		}
		const int32 Chunk = FMath::Max(1, FMath::Min(Effect.ApplyValue, FMath::FloorToInt(Max * PercentPerSecond / 100.0f)));
		SetCurrent(FMath::Min(Current + Chunk, Max));
		Effect.ApplyValue -= Chunk;
		return Effect.ApplyValue <= 0;
	}
}

bool UMT2StatusEffectDefinition::Tick(AMT2CharacterBase* Target, FMT2StatusEffect& Effect) const
{
	// SP upkeep: drain per tick, end the effect the moment it can't be paid (old ProcessAffect).
	if (Effect.SPCostPerTick > 0)
	{
		UMT2ManaComponent* Mana = Target ? Target->GetManaComponent() : nullptr;
		if (Mana)
		{
			if (Mana->GetCurrentValue() < Effect.SPCostPerTick)
			{
				return true;
			}
			Mana->SetCurrentValue(Mana->GetCurrentValue() - Effect.SPCostPerTick);
		}
	}

	if (!Effect.IsInfinite() && --Effect.RemainingSeconds <= 0)
	{
		return true;
	}
	return false;
}

FText UMT2StatusEffectDefinition::GetDisplayName() const
{
	return NSLOCTEXT("MT2StatusEffects", "Buff", "Buff");
}

FText UMT2StatusEffectDefinition::GetDescription(const FMT2StatusEffect& Effect) const
{
	// Premium affects use the original EApplyTypes ordinals. Keep their familiar short wording and
	// value-first layout; other generic stat affects share the configurable item-bonus formatter.
	switch (static_cast<EMT2ItemBonusType>(Effect.ApplyType))
	{
	case EMT2ItemBonusType::MallExperienceBonus:
		return FText::Format(NSLOCTEXT("MT2StatusEffects", "ExperienceBonus", "+{0}% EXP"),
			FText::AsNumber(Effect.ApplyValue));
	case EMT2ItemBonusType::MallItemDropBonus:
		return FText::Format(NSLOCTEXT("MT2StatusEffects", "ItemDropBonus", "+{0}% Item Drop Chance"),
			FText::AsNumber(Effect.ApplyValue));
	case EMT2ItemBonusType::MallYangBonus:
		return FText::Format(NSLOCTEXT("MT2StatusEffects", "YangBonus", "+{0}% Yang"),
			FText::AsNumber(Effect.ApplyValue));
	default:
		break;
	}

	if (Effect.ApplyType > 0)
	{
		return FText::FromString(GetDefault<UMT2ItemBonusSettings>()->FormatBonus(
			Effect.ApplyType, Effect.ApplyValue));
	}
	return GetDisplayName();
}

FText UMT2StatusEffectDefinition::GetMagnitudeLabel(const FMT2StatusEffect&) const
{
	return FText::GetEmpty();
}

const UMT2StatusEffectDefinition* UMT2StatusEffectDefinition::Get(EMT2StatusEffectKind Kind)
{
	switch (Kind)
	{
	case EMT2StatusEffectKind::HealthRecovery: return GetDefault<UMT2StatusEffect_HealthRecovery>();
	case EMT2StatusEffectKind::ManaRecovery:   return GetDefault<UMT2StatusEffect_ManaRecovery>();
	case EMT2StatusEffectKind::AttackSpeed:    return GetDefault<UMT2StatusEffect_AttackSpeed>();
	case EMT2StatusEffectKind::MovementSpeed:  return GetDefault<UMT2StatusEffect_MovementSpeed>();
	case EMT2StatusEffectKind::AutoHealthRecovery: return GetDefault<UMT2StatusEffect_AutoHealthRecovery>();
	case EMT2StatusEffectKind::AutoManaRecovery: return GetDefault<UMT2StatusEffect_AutoManaRecovery>();
	case EMT2StatusEffectKind::StatBonus:
	default:                                   return GetDefault<UMT2StatusEffectDefinition>();
	}
}

FText UMT2StatusEffect_AutoHealthRecovery::GetDisplayName() const
{
	return NSLOCTEXT("MT2StatusEffects", "AutoHealthRecovery", "Automatic HP Recovery");
}

FText UMT2StatusEffect_AutoHealthRecovery::GetDescription(const FMT2StatusEffect& Effect) const
{
	return FText::Format(NSLOCTEXT("MT2StatusEffects", "AutoHealthRemaining", "Remaining HP: {0}"),
		FText::AsNumber(FMath::Max(Effect.ApplyValue, 0)));
}

FText UMT2StatusEffect_AutoHealthRecovery::GetMagnitudeLabel(const FMT2StatusEffect& Effect) const
{
	return FText::AsNumber(FMath::Max(Effect.ApplyValue, 0));
}

FText UMT2StatusEffect_AutoManaRecovery::GetDisplayName() const
{
	return NSLOCTEXT("MT2StatusEffects", "AutoManaRecovery", "Automatic MP Recovery");
}

FText UMT2StatusEffect_AutoManaRecovery::GetDescription(const FMT2StatusEffect& Effect) const
{
	return FText::Format(NSLOCTEXT("MT2StatusEffects", "AutoManaRemaining", "Remaining MP: {0}"),
		FText::AsNumber(FMath::Max(Effect.ApplyValue, 0)));
}

FText UMT2StatusEffect_AutoManaRecovery::GetMagnitudeLabel(const FMT2StatusEffect& Effect) const
{
	return FText::AsNumber(FMath::Max(Effect.ApplyValue, 0));
}

FText UMT2StatusEffect_HealthRecovery::GetDisplayName() const
{
	return NSLOCTEXT("MT2StatusEffects", "HealthRecovery", "HP Recovery");
}

FText UMT2StatusEffect_HealthRecovery::GetMagnitudeLabel(const FMT2StatusEffect& Effect) const
{
	return FText::FromString(FString::Printf(TEXT("+%d"), Effect.ApplyValue)); // HP still to recover
}

FText UMT2StatusEffect_ManaRecovery::GetDisplayName() const
{
	return NSLOCTEXT("MT2StatusEffects", "ManaRecovery", "MP Recovery");
}

FText UMT2StatusEffect_ManaRecovery::GetMagnitudeLabel(const FMT2StatusEffect& Effect) const
{
	return FText::FromString(FString::Printf(TEXT("+%d"), Effect.ApplyValue)); // SP still to recover
}

FText UMT2StatusEffect_AttackSpeed::GetDisplayName() const
{
	return NSLOCTEXT("MT2StatusEffects", "AttackSpeed", "Attack Speed");
}

FText UMT2StatusEffect_AttackSpeed::GetMagnitudeLabel(const FMT2StatusEffect& Effect) const
{
	return FText::FromString(FString::Printf(TEXT("+%d%%"), Effect.ApplyValue));
}

FText UMT2StatusEffect_MovementSpeed::GetDisplayName() const
{
	return NSLOCTEXT("MT2StatusEffects", "MovementSpeed", "Movement Speed");
}

FText UMT2StatusEffect_MovementSpeed::GetMagnitudeLabel(const FMT2StatusEffect& Effect) const
{
	return FText::FromString(FString::Printf(TEXT("+%d%%"), Effect.ApplyValue));
}

bool UMT2StatusEffect_HealthRecovery::CanApply(const AMT2CharacterBase* Target, const FMT2StatusEffect&) const
{
	const UMT2HealthComponent* Health = Target ? Target->GetHealthComponent() : nullptr;
	return Health && Health->GetHealth() < Health->GetMaxHealth(); // deny when already full
}

bool UMT2StatusEffect_HealthRecovery::Tick(AMT2CharacterBase* Target, FMT2StatusEffect& Effect) const
{
	UMT2HealthComponent* Health = Target ? Target->GetHealthComponent() : nullptr;
	if (!Health)
	{
		return true;
	}
	return DrainRecoveryPool(Effect, HealthRecoverPercentPerSecond,
		[Health] { return Health->GetHealth(); },
		[Health] { return Health->GetMaxHealth(); },
		[Health](float V) { Health->SetCurrentValue(V); });
}

bool UMT2StatusEffect_ManaRecovery::CanApply(const AMT2CharacterBase* Target, const FMT2StatusEffect&) const
{
	const UMT2ManaComponent* Mana = Target ? Target->GetManaComponent() : nullptr;
	return Mana && Mana->GetMana() < Mana->GetMaxMana(); // deny when already full
}

bool UMT2StatusEffect_ManaRecovery::Tick(AMT2CharacterBase* Target, FMT2StatusEffect& Effect) const
{
	UMT2ManaComponent* Mana = Target ? Target->GetManaComponent() : nullptr;
	if (!Mana)
	{
		return true;
	}
	return DrainRecoveryPool(Effect, ManaRecoverPercentPerSecond,
		[Mana] { return Mana->GetMana(); },
		[Mana] { return Mana->GetMaxMana(); },
		[Mana](float V) { Mana->SetCurrentValue(V); });
}
