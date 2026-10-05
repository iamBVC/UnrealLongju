/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Stats/MT2CombatStatsComponent.h"

#include "Net/UnrealNetwork.h"

UMT2CombatStatsComponent::UMT2CombatStatsComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UMT2CombatStatsComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMT2CombatStatsComponent, BaseStats);
	DOREPLIFETIME(UMT2CombatStatsComponent, Bonuses);
}

FMT2CombatStats UMT2CombatStatsComponent::GetCalculatedStats() const
{
	FMT2CombatStats Result;
	Result.Defense = BaseStats.Defense + Bonuses.Defense;
	Result.DamageMin = BaseStats.DamageMin + Bonuses.DamageMin;
	Result.DamageMax = BaseStats.DamageMax + Bonuses.DamageMax;
	Result.DamageMultiplier = BaseStats.DamageMultiplier * Bonuses.DamageMultiplier;
	Result.MagicAttack = BaseStats.MagicAttack + Bonuses.MagicAttack;
	Result.MagicDefense = BaseStats.MagicDefense + Bonuses.MagicDefense;
	Result.Evasion = BaseStats.Evasion + Bonuses.Evasion;
	Result.AttackSpeed = BaseStats.AttackSpeed + Bonuses.AttackSpeed;
	Result.MovementSpeed = BaseStats.MovementSpeed + Bonuses.MovementSpeed;
	Result.AttackRange = BaseStats.AttackRange + Bonuses.AttackRange;
	Result.MaxHealth = BaseStats.MaxHealth + Bonuses.MaxHealth;
	return ClampStats(Result);
}

float UMT2CombatStatsComponent::GetAverageDamage() const
{
	const FMT2CombatStats Stats = GetCalculatedStats();
	return (Stats.DamageMin + Stats.DamageMax) * 0.5f * Stats.DamageMultiplier;
}

float UMT2CombatStatsComponent::GetAttackInterval() const
{
	return FMath::Clamp(0.65f * 100.0f / GetCalculatedStats().AttackSpeed, 0.1f, 5.0f);
}

bool UMT2CombatStatsComponent::SetBaseStats(const FMT2CombatStats& NewStats)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}
	const FMT2CombatStats ValidStats = ClampStats(NewStats);
	if (BaseStats == ValidStats)
	{
		return false;
	}
	const FMT2CombatStats OldCalculated = GetCalculatedStats();
	BaseStats = ValidStats;
	BroadcastChange(OldCalculated);
	return true;
}

bool UMT2CombatStatsComponent::SetBonuses(const FMT2CombatStatBonuses& NewBonuses)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Bonuses == NewBonuses)
	{
		return false;
	}
	const FMT2CombatStats OldCalculated = GetCalculatedStats();
	Bonuses = NewBonuses;
	Bonuses.DamageMultiplier = FMath::Max(Bonuses.DamageMultiplier, 0.0f);
	BroadcastChange(OldCalculated);
	return true;
}

void UMT2CombatStatsComponent::ConfigureBaseStats(const FMT2CombatStats& NewStats)
{
	const FMT2CombatStats OldCalculated = GetCalculatedStats();
	BaseStats = ClampStats(NewStats);
	BroadcastChange(OldCalculated);
}

FMT2CombatStats UMT2CombatStatsComponent::ClampStats(const FMT2CombatStats& Stats)
{
	FMT2CombatStats Result = Stats;
	Result.Defense = FMath::Max(Result.Defense, 0.0f);
	Result.DamageMin = FMath::Max(Result.DamageMin, 0.0f);
	Result.DamageMax = FMath::Max(Result.DamageMax, Result.DamageMin);
	Result.DamageMultiplier = FMath::Max(Result.DamageMultiplier, 0.0f);
	Result.MagicAttack = FMath::Max(Result.MagicAttack, 0.0f);
	Result.MagicDefense = FMath::Max(Result.MagicDefense, 0.0f);
	Result.Evasion = FMath::Max(Result.Evasion, 0.0f);
	Result.AttackSpeed = FMath::Max(Result.AttackSpeed, 1);
	Result.MovementSpeed = FMath::Max(Result.MovementSpeed, 0);
	Result.AttackRange = FMath::Max(Result.AttackRange, 0.0f);
	Result.MaxHealth = FMath::Max(Result.MaxHealth, 1.0f);
	return Result;
}

void UMT2CombatStatsComponent::BroadcastChange(const FMT2CombatStats& OldCalculatedStats)
{
	const FMT2CombatStats NewCalculatedStats = GetCalculatedStats();
	if (!(OldCalculatedStats == NewCalculatedStats))
	{
		OnCombatStatsChanged.Broadcast(OldCalculatedStats, NewCalculatedStats);
	}
}

void UMT2CombatStatsComponent::OnRep_BaseStats(FMT2CombatStats OldStats)
{
	const FMT2CombatStats CurrentBase = BaseStats;
	BaseStats = OldStats;
	const FMT2CombatStats OldCalculated = GetCalculatedStats();
	BaseStats = CurrentBase;
	BroadcastChange(OldCalculated);
}

void UMT2CombatStatsComponent::OnRep_Bonuses(FMT2CombatStatBonuses OldBonuses)
{
	const FMT2CombatStatBonuses CurrentBonuses = Bonuses;
	Bonuses = OldBonuses;
	const FMT2CombatStats OldCalculated = GetCalculatedStats();
	Bonuses = CurrentBonuses;
	BroadcastChange(OldCalculated);
}
