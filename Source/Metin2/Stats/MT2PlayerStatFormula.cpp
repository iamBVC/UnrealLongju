/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Stats/MT2PlayerStatFormula.h"

namespace
{
	struct FMT2JobInitialPoints
	{
		int32 Strength;
		int32 Constitution;
		int32 Dexterity;
		int32 Intelligence;
		int32 BaseHealth;
		int32 BaseMana;
	};

	constexpr FMT2JobInitialPoints JobInitialPoints[] = {
		{6, 4, 3, 3, 600, 200}, // Warrior
		{4, 3, 6, 3, 650, 200}, // Assassin
		{5, 3, 3, 5, 650, 200}, // Sura
		{3, 4, 3, 6, 700, 200}  // Shaman
	};

	const FMT2JobInitialPoints& GetJobPoints(EMT2CharacterRace Race)
	{
		const int32 Index = FMath::Clamp(static_cast<int32>(Race), 0, UE_ARRAY_COUNT(JobInitialPoints) - 1);
		return JobInitialPoints[Index];
	}
}

FMT2PrimaryStats MT2PlayerStatFormula::GetInitialPrimaryStats(EMT2CharacterRace Race)
{
	const FMT2JobInitialPoints& Job = GetJobPoints(Race);
	FMT2PrimaryStats Result;
	Result.Strength = Job.Strength;
	Result.Constitution = Job.Constitution;
	Result.Dexterity = Job.Dexterity;
	Result.Intelligence = Job.Intelligence;
	return Result;
}

FMT2PlayerDerivedStats MT2PlayerStatFormula::Calculate(
	EMT2CharacterRace Race, int32 Level, const FMT2PrimaryStats& PrimaryStats)
{
	const FMT2JobInitialPoints& Job = GetJobPoints(Race);
	const int32 ValidLevel = FMath::Max(Level, 1);
	const int32 Strength = FMath::Max(PrimaryStats.Strength, 0);
	const int32 Dexterity = FMath::Max(PrimaryStats.Dexterity, 0);
	const int32 Constitution = FMath::Max(PrimaryStats.Constitution, 0);
	const int32 Intelligence = FMath::Max(PrimaryStats.Intelligence, 0);

	int32 StatAttack = Strength * 2;
	switch (Race)
	{
	case EMT2CharacterRace::Assassin:
		StatAttack = (Strength * 4 + Dexterity * 2) / 3;
		break;
	case EMT2CharacterRace::Shaman:
		StatAttack = (Strength * 4 + Intelligence * 2) / 3;
		break;
	default:
		break;
	}

	FMT2PlayerDerivedStats Result;
	Result.Combat.DamageMin = static_cast<float>(ValidLevel * 2 + StatAttack);
	Result.Combat.DamageMax = Result.Combat.DamageMin;
	Result.Combat.Defense = static_cast<float>(ValidLevel + FMath::FloorToInt(Constitution / 1.25f));
	Result.Combat.MagicAttack = static_cast<float>(ValidLevel * 2 + Intelligence * 2);
	Result.Combat.MagicDefense = static_cast<float>(
		ValidLevel + (Intelligence * 3 + Constitution) / 3);
	const int32 EvasionSource = FMath::Min(90, (Dexterity * 4 + ValidLevel * 2) / 6);
	Result.Combat.Evasion = static_cast<float>(EvasionSource * 2 + 5) /
		static_cast<float>(EvasionSource + 95) * 30.0f;
	Result.Combat.AttackSpeed = 100;
	Result.Combat.MovementSpeed = 100;
	Result.Combat.AttackRange = 200.0f;

	// The old server stores random per-level HP/MP rolls (36..44 and 18..22). Until those rolls
	// become persisted fields, use their exact averages so level scaling remains deterministic.
	Result.Combat.MaxHealth = static_cast<float>(
		Job.BaseHealth + (ValidLevel - 1) * 40 + Constitution * 40);
	Result.MaxMana = static_cast<float>(
		Job.BaseMana + (ValidLevel - 1) * 20 + Intelligence * 20);
	Result.MaxStamina = static_cast<float>(800 + Constitution * 5);
	return Result;
}

float MT2PlayerStatFormula::CalculateAttackRating(
	int32 AttackerLevel, int32 AttackerDexterity, int32 DefenderLevel, int32 DefenderDexterity)
{
	const int32 AttackSource = FMath::Min(90,
		(FMath::Max(AttackerDexterity, 0) * 4 + FMath::Max(AttackerLevel, 1) * 2) / 6);
	const int32 EvasionSource = FMath::Min(90,
		(FMath::Max(DefenderDexterity, 0) * 4 + FMath::Max(DefenderLevel, 1) * 2) / 6);
	const float AttackRating = (static_cast<float>(AttackSource) + 210.0f) / 300.0f;
	const float EvasionRating = static_cast<float>(EvasionSource * 2 + 5) /
		static_cast<float>(EvasionSource + 95) * 0.3f;
	return FMath::Clamp(AttackRating - EvasionRating, 0.0f, 1.0f);
}
