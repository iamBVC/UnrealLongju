/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Player/MT2PlayerTypes.h"
#include "MT2PartyTypes.generated.h"

class AMT2PlayerState;

USTRUCT(BlueprintType)
struct METIN2_API FMT2PartyMemberData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Party")
	TObjectPtr<AMT2PlayerState> PlayerState;

	// Persistent identity used by the coordinator. PlayerState is only valid when the member is
	// connected to this map-server instance.
	UPROPERTY(BlueprintReadOnly, Category = "Party")
	FString CharacterId;

	UPROPERTY(BlueprintReadOnly, Category = "Party")
	int32 PlayerId = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Party")
	FString CharacterName;

	UPROPERTY(BlueprintReadOnly, Category = "Party")
	int32 Level = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Party")
	FMT2CharacterAppearance Appearance;

	UPROPERTY(BlueprintReadOnly, Category = "Party")
	float Health = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Party")
	float MaxHealth = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Party")
	bool bLeader = false;

	UPROPERTY(BlueprintReadOnly, Category = "Party")
	FVector_NetQuantize10 WorldLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Party")
	bool bHasWorldLocation = false;

	UPROPERTY(BlueprintReadOnly, Category = "Party")
	FString MapId;

	UPROPERTY(BlueprintReadOnly, Category = "Party")
	int32 Channel = 1;

	bool operator==(const FMT2PartyMemberData& Other) const
	{
		return PlayerState == Other.PlayerState && CharacterId == Other.CharacterId
			&& PlayerId == Other.PlayerId
			&& CharacterName == Other.CharacterName
			&& Level == Other.Level && Appearance == Other.Appearance
			&& FMath::IsNearlyEqual(Health, Other.Health)
			&& FMath::IsNearlyEqual(MaxHealth, Other.MaxHealth)
			&& bLeader == Other.bLeader
			&& WorldLocation == Other.WorldLocation
			&& bHasWorldLocation == Other.bHasWorldLocation
			&& MapId == Other.MapId && Channel == Other.Channel;
	}
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMT2PartyMembersChangedSignature);
