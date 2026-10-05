/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Player/MT2PlayerTypes.h"
#include "MT2ServerRuntimeTypes.generated.h"

UENUM(BlueprintType)
enum class EMT2ServerRuntimeMode : uint8
{
	Client,
	Gateway,
	Map,
	Coordinator
};

UENUM(BlueprintType)
enum class EMT2MapServerState : uint8
{
	Starting,
	LoadingWorld,
	Ready,
	Draining
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2MapServerDescriptor
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Server")
	FString InstanceId;

	UPROPERTY(BlueprintReadOnly, Category = "Server")
	FString MapId;

	UPROPERTY(BlueprintReadOnly, Category = "Server")
	FString MapPath;

	UPROPERTY(BlueprintReadOnly, Category = "Server")
	int32 Channel = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Server")
	FString PublicIp;

	UPROPERTY(BlueprintReadOnly, Category = "Server")
	int32 GamePort = 11001;

	UPROPERTY(BlueprintReadOnly, Category = "Server")
	int32 PlayerCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Server")
	int32 MaxPlayers = 1000;

	UPROPERTY(BlueprintReadOnly, Category = "Server")
	EMT2MapServerState State = EMT2MapServerState::Starting;

	UPROPERTY(BlueprintReadOnly, Category = "Server")
	FString BuildVersion;

	double LastHeartbeatSeconds = 0.0;
};

struct FMT2ServerRuntimeConfig
{
	EMT2ServerRuntimeMode Mode = EMT2ServerRuntimeMode::Client;
	FString InstanceId;
	FString MapId;
	FString MapPath;
	int32 Channel = 1;
	// Map and gateway processes must advertise their externally reachable endpoint explicitly.
	// Host advertised by this process to clients. It must be supplied explicitly with
	// -public_ip because map servers may run on machines different from the coordinator.
	FString PublicIp;
	int32 GamePort = 11001;
	int32 MaxPlayers = 1000;
	int32 ServerTickRate = 30;
	bool bIncrementalGarbageCollection = true;
	float GarbageCollectionTimeBudgetMilliseconds = 2.0f;

	FString CoordinatorIp = TEXT("127.0.0.1");
	FString CoordinatorBindIp = TEXT("127.0.0.1");
	FString CoordinatorMapPath = TEXT("/Game/Maps/System/Coordinator");
	FString GatewayMapPath = TEXT("/Game/Maps/System/Gateway");
	int32 CoordinatorPort = 11099;
	FString CoordinatorToken;
	float HeartbeatIntervalSeconds = 5.0f;
	float ServerTimeoutSeconds = 20.0f;
	float TransferTicketLifetimeSeconds = 30.0f;
	float RequestTimeoutSeconds = 15.0f;
	bool bAllowAccountRegistration = false;

	FString DatabaseRoot;
	FString DatabaseFile = TEXT("metin2.db");
	FString DatabaseSynchronousMode = TEXT("FULL");
	int32 DatabaseBusyTimeoutMilliseconds = 5000;
	bool bCheckDatabaseIntegrity = true;
	float AutosaveIntervalSeconds = 60.0f;

	bool IsCoordinator() const { return Mode == EMT2ServerRuntimeMode::Coordinator; }
	bool IsGateway() const { return Mode == EMT2ServerRuntimeMode::Gateway; }
	bool IsMapServer() const { return Mode == EMT2ServerRuntimeMode::Map; }
	bool HasDatabaseConfiguration() const
	{
		return IsCoordinator() && !DatabaseRoot.IsEmpty() && !DatabaseFile.IsEmpty();
	}
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2CharacterSummary
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Character")
	FString CharacterId;

	UPROPERTY(BlueprintReadOnly, Category = "Character")
	FString CharacterName;

	UPROPERTY(BlueprintReadOnly, Category = "Character")
	int32 Level = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Character")
	FMT2CharacterAppearance Appearance;

	UPROPERTY(BlueprintReadOnly, Category = "Character")
	EMT2Empire Empire = EMT2Empire::None;

	UPROPERTY(BlueprintReadOnly, Category = "Character")
	FString MapId;

	UPROPERTY(BlueprintReadOnly, Category = "Character")
	int32 Channel = 1;

	// Lightweight equipment snapshot sent with the character list. VNUMs keep the gateway payload
	// small; the client resolves their imported templates through the existing VNUM registry.
	UPROPERTY(BlueprintReadOnly, Category = "Character|Preview")
	int32 EquippedArmorVnum = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Character|Preview")
	int32 EquippedWeaponVnum = 0;

	// Costume hair (wear slot 20) takes precedence over normal head/hair equipment (slot 1).
	UPROPERTY(BlueprintReadOnly, Category = "Character|Preview")
	int32 EquippedHairVnum = 0;
};

struct FMT2AccountLoginResult
{
	bool bSucceeded = false;
	bool bReplacedExistingSession = false;
	FString AccountId;
	FString SessionToken;
	TArray<FMT2CharacterSummary> Characters;
	FString Error;
};

struct FMT2AccountRegistrationResult
{
	bool bSucceeded = false;
	FString AccountId;
	FString Error;
};

struct FMT2CharacterCreateResult
{
	bool bSucceeded = false;
	FMT2CharacterSummary Character;
	FString Error;
};

struct FMT2CharacterAdmissionResult
{
	bool bSucceeded = false;
	FString Ticket;
	FMT2MapServerDescriptor Server;
	FString Error;
};

struct FMT2MapRouteResult
{
	bool bSucceeded = false;
	FMT2MapServerDescriptor Server;
	FString Error;
};

struct FMT2TransferTicketResult
{
	bool bSucceeded = false;
	FString Ticket;
	FMT2MapServerDescriptor Server;
	FString Error;
};

struct FMT2TransferClaimResult
{
	bool bSucceeded = false;
	bool bForceTownSpawn = false;
	FString CharacterId;
	FString AccountId;
	FString SourceInstanceId;
	FMT2CharacterSummary Character;
	FString Error;
};

struct FMT2TransferCompleteResult
{
	bool bSucceeded = false;
	FString Error;
};
