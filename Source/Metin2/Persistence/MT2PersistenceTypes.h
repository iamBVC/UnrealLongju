/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "MT2PersistenceTypes.generated.h"

UENUM(BlueprintType)
enum class EMT2PersistenceState : uint8
{
	Disabled,
	Ready,
	Loading,
	Saving,
	Error
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2PersistentRecord
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Persistence")
	FName EntityType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Persistence")
	FString EntityId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Persistence")
	FString OwnerId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Persistence")
	int32 SchemaVersion = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Persistence")
	int64 Revision = 0;

	// A player row exists immediately after character creation, before it has a saved world transform.
	// Keep that distinction so first login uses the map's town spawn.
	UPROPERTY(BlueprintReadOnly, Category = "Persistence")
	bool bHasWorldPosition = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Persistence")
	FString PayloadJson;

	bool IsValid() const
	{
		return !EntityType.IsNone() && !EntityId.IsEmpty();
	}
};

struct FMT2PersistenceLoadResult
{
	bool bSucceeded = false;
	bool bFound = false;
	FMT2PersistentRecord Record;
	FString Error;
};

struct FMT2PersistenceSaveResult
{
	bool bSucceeded = false;
	int64 Revision = 0;
	FString Error;
};
