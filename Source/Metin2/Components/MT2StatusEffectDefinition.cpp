/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Components/MT2StatusEffectDefinition.h"

#include "Characters/MT2CharacterBase.h"
#include "Config/MT2GameplaySettings.h"
#include "Components/MT2HealthComponent.h"
#include "Components/MT2ManaComponent.h"
#include "Items/MT2ItemBonusSettings.h"

namespace
{
	// The legacy affect pass applies an integer percentage of the integer maximum once per second.
	// Keep the pool as the source of truth and deduct the requested chunk even when the resource setter
	// clamps the actual heal at maximum. Shared by HP and MP recovery; returns true when the effect ends.
	template <typename FGetCurrent, typename FGetMax, typename FSetCurrent>
	bool DrainRecoveryPool(FMT2StatusEffect& Effect, int32 PercentPerSecond,
		FGetCurrent&& GetCurrent, FGetMax&& GetMax, FSetCurrent&& SetCurrent)
	{
		const float Current = GetCurrent();
		const float Max = GetMax();
		if (Effect.ApplyValue <= 0 || !FMath::IsFinite(Current) || !FMath::IsFinite(Max) || Current >= Max)
		{
			return true; // pool spent or resource full -> done
		}

		// Character maxima are legacy integer stats. Bound before conversion so malformed/extreme
		// project values cannot overflow the conversion. Clamp config-loaded rates to the same range
		// exposed by Project Settings, then use 64-bit math for maximum * percentage.
		const double BoundedMaximum = FMath::Clamp(static_cast<double>(Max), 0.0, static_cast<double>(MAX_int32));
		const int32 MaximumPoints = static_cast<int32>(BoundedMaximum);
		const int32 BoundedPercent = FMath::Clamp(PercentPerSecond, 1, 100);
		const int64 LegacyChunk = static_cast<int64>(MaximumPoints) * BoundedPercent / 100;
		const int64 Chunk = FMath::Min<int64>(Effect.ApplyValue, LegacyChunk);
		if (Chunk <= 0)
		{
			return false; // legacy integer division can produce a zero-point pass
		}

		SetCurrent(FMath::Min(Current + static_cast<float>(Chunk), Max));
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
	switch (Effect.ApplyType)
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

	if (Effect.ApplyType != EMT2ItemBonusType::None)
	{
		return FText::FromString(GetDefault<UMT2ItemBonusSettings>()->FormatBonus(
			static_cast<int32>(Effect.ApplyType), Effect.ApplyValue));
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
	case EMT2StatusEffectKind::ContinuousHealthRecovery: return GetDefault<UMT2StatusEffect_ContinuousHealthRecovery>();
	case EMT2StatusEffectKind::ContinuousManaRecovery: return GetDefault<UMT2StatusEffect_ContinuousManaRecovery>();
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
	return DrainRecoveryPool(Effect, UMT2GameplaySettings::Get().HealthRecoveryPercentPerSecond,
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
	return DrainRecoveryPool(Effect, UMT2GameplaySettings::Get().ManaRecoveryPercentPerSecond,
		[Mana] { return Mana->GetMana(); },
		[Mana] { return Mana->GetMaxMana(); },
		[Mana](float V) { Mana->SetCurrentValue(V); });
}

bool UMT2StatusEffect_ContinuousHealthRecovery::Tick(AMT2CharacterBase* Target, FMT2StatusEffect& Effect) const
{
	auto* Health = Target ? Target->GetHealthComponent() : nullptr;
	if (!Health || Health->IsDead() || Effect.ApplyValue <= 0) return true;
	Health->SetCurrentValue(FMath::Min(Health->GetHealth() + Effect.ApplyValue, Health->GetMaxHealth()));
	return Super::Tick(Target, Effect);
}

FText UMT2StatusEffect_ContinuousHealthRecovery::GetDisplayName() const
{
	return NSLOCTEXT("MT2StatusEffects", "ContinuousHP", "Continuous HP Recovery");
}

FText UMT2StatusEffect_ContinuousHealthRecovery::GetDescription(const FMT2StatusEffect& Effect) const
{
	return FText::Format(NSLOCTEXT("MT2StatusEffects", "ContinuousHPRate", "+{0} HP per second"), FText::AsNumber(Effect.ApplyValue));
}

bool UMT2StatusEffect_ContinuousManaRecovery::Tick(AMT2CharacterBase* Target, FMT2StatusEffect& Effect) const
{
	auto* Mana = Target ? Target->GetManaComponent() : nullptr;
	if (!Mana || !Target->GetHealthComponent() || Target->GetHealthComponent()->IsDead() || Effect.ApplyValue <= 0) return true;
	Mana->SetCurrentValue(FMath::Min(Mana->GetMana() + Effect.ApplyValue, Mana->GetMaxMana()));
	return Super::Tick(Target, Effect);
}

FText UMT2StatusEffect_ContinuousManaRecovery::GetDisplayName() const
{
	return NSLOCTEXT("MT2StatusEffects", "ContinuousMP", "Continuous MP Recovery");
}

FText UMT2StatusEffect_ContinuousManaRecovery::GetDescription(const FMT2StatusEffect& Effect) const
{
	return FText::Format(NSLOCTEXT("MT2StatusEffects", "ContinuousMPRate", "+{0} MP per second"), FText::AsNumber(Effect.ApplyValue));
}
