/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Stats/MT2PrimaryStatsComponent.h"

#include "Net/UnrealNetwork.h"

UMT2PrimaryStatsComponent::UMT2PrimaryStatsComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UMT2PrimaryStatsComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMT2PrimaryStatsComponent, BaseStats);
	DOREPLIFETIME(UMT2PrimaryStatsComponent, BonusStats);
}

FMT2PrimaryStats UMT2PrimaryStatsComponent::GetCalculatedStats() const
{
	FMT2PrimaryStats Result;
	Result.Strength = BaseStats.Strength + BonusStats.Strength;
	Result.Dexterity = BaseStats.Dexterity + BonusStats.Dexterity;
	Result.Constitution = BaseStats.Constitution + BonusStats.Constitution;
	Result.Intelligence = BaseStats.Intelligence + BonusStats.Intelligence;
	return ClampStats(Result);
}

bool UMT2PrimaryStatsComponent::SetBaseStats(const FMT2PrimaryStats& NewStats)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}
	const FMT2PrimaryStats ValidStats = ClampStats(NewStats);
	if (BaseStats == ValidStats)
	{
		return false;
	}
	const FMT2PrimaryStats OldCalculated = GetCalculatedStats();
	BaseStats = ValidStats;
	BroadcastChange(OldCalculated);
	return true;
}

bool UMT2PrimaryStatsComponent::SetBonusStats(const FMT2PrimaryStats& NewBonuses)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}
	const FMT2PrimaryStats ValidStats = ClampStats(NewBonuses);
	if (BonusStats == ValidStats)
	{
		return false;
	}
	const FMT2PrimaryStats OldCalculated = GetCalculatedStats();
	BonusStats = ValidStats;
	BroadcastChange(OldCalculated);
	return true;
}

void UMT2PrimaryStatsComponent::ConfigureBaseStats(const FMT2PrimaryStats& NewStats)
{
	const FMT2PrimaryStats OldCalculated = GetCalculatedStats();
	BaseStats = ClampStats(NewStats);
	BroadcastChange(OldCalculated);
}

FMT2PrimaryStats UMT2PrimaryStatsComponent::ClampStats(const FMT2PrimaryStats& Stats)
{
	FMT2PrimaryStats Result = Stats;
	Result.Strength = FMath::Max(Result.Strength, 0);
	Result.Dexterity = FMath::Max(Result.Dexterity, 0);
	Result.Constitution = FMath::Max(Result.Constitution, 0);
	Result.Intelligence = FMath::Max(Result.Intelligence, 0);
	return Result;
}

void UMT2PrimaryStatsComponent::BroadcastChange(const FMT2PrimaryStats& OldCalculatedStats)
{
	const FMT2PrimaryStats NewCalculatedStats = GetCalculatedStats();
	if (!(OldCalculatedStats == NewCalculatedStats))
	{
		OnPrimaryStatsChanged.Broadcast(OldCalculatedStats, NewCalculatedStats);
	}
}

void UMT2PrimaryStatsComponent::OnRep_BaseStats(FMT2PrimaryStats OldStats)
{
	FMT2PrimaryStats OldCalculated = GetCalculatedStats();
	OldCalculated.Strength += OldStats.Strength - BaseStats.Strength;
	OldCalculated.Dexterity += OldStats.Dexterity - BaseStats.Dexterity;
	OldCalculated.Constitution += OldStats.Constitution - BaseStats.Constitution;
	OldCalculated.Intelligence += OldStats.Intelligence - BaseStats.Intelligence;
	BroadcastChange(OldCalculated);
}

void UMT2PrimaryStatsComponent::OnRep_BonusStats(FMT2PrimaryStats OldStats)
{
	FMT2PrimaryStats OldCalculated = GetCalculatedStats();
	OldCalculated.Strength += OldStats.Strength - BonusStats.Strength;
	OldCalculated.Dexterity += OldStats.Dexterity - BonusStats.Dexterity;
	OldCalculated.Constitution += OldStats.Constitution - BonusStats.Constitution;
	OldCalculated.Intelligence += OldStats.Intelligence - BonusStats.Intelligence;
	BroadcastChange(OldCalculated);
}
