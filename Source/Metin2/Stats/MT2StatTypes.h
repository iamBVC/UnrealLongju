/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "MT2StatTypes.generated.h"

UENUM(BlueprintType)
enum class EMT2PrimaryStat : uint8
{
	Constitution,
	Intelligence,
	Strength,
	Dexterity
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2PrimaryStats
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Primary Stats", meta = (ClampMin = "0"))
	int32 Strength = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Primary Stats", meta = (ClampMin = "0"))
	int32 Dexterity = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Primary Stats", meta = (ClampMin = "0"))
	int32 Constitution = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Primary Stats", meta = (ClampMin = "0"))
	int32 Intelligence = 0;

	bool operator==(const FMT2PrimaryStats& Other) const = default;
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2CombatStats
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "0.0"))
	float Defense = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "0.0"))
	float DamageMin = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "0.0"))
	float DamageMax = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "0.0"))
	float DamageMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "0.0"))
	float MagicAttack = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "0.0"))
	float MagicDefense = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "0.0"))
	float Evasion = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "1"))
	int32 AttackSpeed = 100;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0"))
	int32 MovementSpeed = 100;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "0.0", Units = "cm"))
	float AttackRange = 175.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vitality", meta = (ClampMin = "1.0"))
	float MaxHealth = 100.0f;

	bool operator==(const FMT2CombatStats& Other) const = default;
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2CombatStatBonuses
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float Defense = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float DamageMin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float DamageMax = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "0.0"))
	float DamageMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float MagicAttack = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float MagicDefense = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float Evasion = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	int32 AttackSpeed = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
	int32 MovementSpeed = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (Units = "cm"))
	float AttackRange = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vitality")
	float MaxHealth = 0.0f;

	bool operator==(const FMT2CombatStatBonuses& Other) const = default;
};
