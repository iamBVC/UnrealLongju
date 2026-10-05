/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "MT2MobSpawnTypes.generated.h"

UENUM(BlueprintType)
enum class EMT2MobSpawnSource : uint8
{
	Regen,
	Npc,
	Stone,
	Boss
};

UENUM(BlueprintType)
enum class EMT2MobSpawnType : uint8
{
	Mob,
	Group,
	AggressiveGroup,
	GroupGroup,
	Anywhere
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2MobSpawnMember
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn")
	int32 MobVnum = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn")
	bool bLeader = false;
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2MobSpawnVariant
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn", meta = (ClampMin = "1"))
	int32 Weight = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn")
	int32 GroupVnum = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn")
	TArray<FMT2MobSpawnMember> Members;
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2MobSpawnEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn")
	FName SourceId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn")
	EMT2MobSpawnSource Source = EMT2MobSpawnSource::Regen;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn")
	EMT2MobSpawnType Type = EMT2MobSpawnType::Mob;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn")
	int32 SourceVnum = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn")
	FVector Center = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn")
	FVector2D HorizontalExtent = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn")
	float Yaw = -1.0f;

	// Zero means initial spawn only, matching old NO_REGEN behavior.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn", meta = (ClampMin = "0.0", Units = "s"))
	float RespawnDelay = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float SpawnChancePercent = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn", meta = (ClampMin = "0"))
	int32 DesiredGroupCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn")
	int32 ZSection = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn")
	bool bForceAggressive = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn")
	TArray<FMT2MobSpawnVariant> Variants;
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2MobSpawnExclusion
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn")
	FName SourceId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn")
	FVector2D Center = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn")
	FVector2D HorizontalExtent = FVector2D::ZeroVector;

	bool Contains(const FVector& Location) const
	{
		return FMath::Abs(Location.X - Center.X) <= HorizontalExtent.X
			&& FMath::Abs(Location.Y - Center.Y) <= HorizontalExtent.Y;
	}
};
