/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Server/MT2ServerRuntimeSubsystem.h"
#include "Config/MT2PathSettings.h"

#include "Persistence/MT2PersistenceBackend.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"

#include "Messenger/MT2MessengerComponent.h"
#include "Messenger/MT2MessengerTypes.h"
#include "Guild/MT2GuildComponent.h"

#include "FramePro/FramePro.h"

#include "Authentication/MT2AuthenticationUtils.h"
#include "Config/MT2GameplaySettings.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/GameInstance.h"
#include "Engine/NetDriver.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMisc.h"
#include "IPAddress.h"
#include "Misc/CommandLine.h"
#include "Misc/Guid.h"
#include "Misc/NetworkVersion.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Persistence/MT2PersistenceManager.h"
#include "Persistence/MT2PersistenceComponent.h"
#include "Party/MT2Party.h"
#include "Player/MT2PlayerState.h"
#include "Player/MT2PlayerController.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "TimerManager.h"
#include "UObject/ReachabilityAnalysis.h"

DEFINE_LOG_CATEGORY_STATIC(LogMT2ServerRuntime, Log, All);

namespace
{
	constexpr int32 MaxCoordinatorMessageBytes = 4 * 1024 * 1024;
	constexpr int32 MaxCoordinatorSendBufferBytes = 8 * 1024 * 1024;
	constexpr int32 CoordinatorProtocolVersion = 1;

	const FString& GetRuntimeBuildVersion()
	{
		return FNetworkVersion::GetProjectVersion();
	}

	FString GetEmpireStartMap(EMT2Empire Empire)
	{
		switch (Empire)
		{
		case EMT2Empire::Shinsoo: return TEXT("yongan");
		case EMT2Empire::Chunjo: return TEXT("joan");
		case EMT2Empire::Jinno: return TEXT("pyungmoo");
		default: return FString();
		}
	}

	FString NormalizeMapId(const FString& Value)
	{
		FString MapId = Value.TrimStartAndEnd().ToLower();
		MapId.ReplaceInline(TEXT("\\"), TEXT("/"));
		int32 LastSlash = INDEX_NONE;
		if (MapId.FindLastChar(TEXT('/'), LastSlash)) MapId.RightChopInline(LastSlash + 1);
		int32 ObjectSeparator = INDEX_NONE;
		if (MapId.FindChar(TEXT('.'), ObjectSeparator)) MapId.LeftInline(ObjectSeparator);
		if (MapId.StartsWith(TEXT("metin2_map_"))) MapId.RightChopInline(11);
		else if (MapId.StartsWith(TEXT("map_"))) MapId.RightChopInline(4);
		return MapId;
	}

	bool ReadCommandString(const TCHAR* Key, FString& OutValue)
	{
		return FParse::Value(FCommandLine::Get(), Key, OutValue) && !OutValue.IsEmpty();
	}

	void ReadCommandInt(const TCHAR* Key, int32& InOutValue)
	{
		FParse::Value(FCommandLine::Get(), Key, InOutValue);
	}

	void ReadCommandFloat(const TCHAR* Key, float& InOutValue)
	{
		FParse::Value(FCommandLine::Get(), Key, InOutValue);
	}

	bool ReadCommandBool(const TCHAR* Key, bool DefaultValue)
	{
		int32 Value = DefaultValue ? 1 : 0;
		FParse::Value(FCommandLine::Get(), Key, Value);
		return Value != 0;
	}

	FString ReadSecret(const TCHAR* DirectKey, const TCHAR* EnvironmentKey, const TCHAR* DefaultEnvironment)
	{
		FString Secret;
		if (ReadCommandString(DirectKey, Secret)) return Secret;
		FString EnvironmentName = DefaultEnvironment;
		ReadCommandString(EnvironmentKey, EnvironmentName);
		return EnvironmentName.IsEmpty() ? FString() : FPlatformMisc::GetEnvironmentVariable(*EnvironmentName);
	}

	void DestroySocket(FSocket*& Socket)
	{
		if (!Socket) return;
		Socket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		Socket = nullptr;
	}

	TSharedRef<FJsonObject> MakeMessage(const TCHAR* Type)
	{
		TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
		Message->SetStringField(TEXT("type"), Type);
		return Message;
	}
}

void UMT2ServerRuntimeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	int32 ParentProcessId = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("WaitForParentPid="), ParentProcessId) &&
		ParentProcessId > 0)
	{
		const double Deadline = FPlatformTime::Seconds() + 30.0;
		while (FPlatformProcess::IsApplicationRunning(static_cast<uint32>(ParentProcessId)) &&
			FPlatformTime::Seconds() < Deadline)
		{
			FPlatformProcess::Sleep(0.1f);
		}
	}
	FString Error;
	if (!ParseCommandLine(Error))
	{
		UE_LOG(LogMT2ServerRuntime, Error, TEXT("Server startup rejected: %s"), *Error);
		if (IsRunningDedicatedServer()) FPlatformMisc::RequestExit(false);
		return;
	}

	if (Config.Mode == EMT2ServerRuntimeMode::Client) return;
	UE_LOG(LogMT2ServerRuntime, Display, TEXT("Runtime build version %s."),
		*GetRuntimeBuildVersion());
	SetIncrementalReachabilityAnalysisEnabled(Config.bIncrementalGarbageCollection);
	SetReachabilityAnalysisTimeLimit(Config.GarbageCollectionTimeBudgetMilliseconds / 1000.0f);
	UE_LOG(LogMT2ServerRuntime, Display,
		TEXT("Server GC incremental reachability=%s, time budget=%.2f ms."),
		Config.bIncrementalGarbageCollection ? TEXT("enabled") : TEXT("disabled"),
		Config.GarbageCollectionTimeBudgetMilliseconds);
	PostWorldInitializationHandle = FWorldDelegates::OnPostWorldInitialization.AddUObject(
		this, &UMT2ServerRuntimeSubsystem::HandlePostWorldInitialization);

	if (Config.IsCoordinator())
	{
		if (!StartCoordinatorListener()) FPlatformMisc::RequestExit(false);
		else UE_LOG(LogMT2ServerRuntime, Display, TEXT("Coordinator listening on %s:%d."),
			*Config.CoordinatorBindIp, Config.CoordinatorPort);
	}
	else if (Config.IsGateway())
	{
		UE_LOG(LogMT2ServerRuntime, Display,
			TEXT("Gateway server %s starting public=%s:%d coordinator=%s:%d."),
			*Config.InstanceId, *Config.PublicIp, Config.GamePort,
			*Config.CoordinatorIp, Config.CoordinatorPort);
	}
	else
	{
		UE_LOG(LogMT2ServerRuntime, Display,
			TEXT("Map server %s starting map=%s channel=%d public=%s:%d coordinator=%s:%d."),
			*Config.InstanceId, *Config.MapId, Config.Channel, *Config.PublicIp, Config.GamePort,
			*Config.CoordinatorIp, Config.CoordinatorPort);
	}
}

void UMT2ServerRuntimeSubsystem::Deinitialize()
{
	FWorldDelegates::OnPostWorldInitialization.Remove(PostWorldInitializationHandle);
	if (Config.IsMapServer() && bCoordinatorAuthenticated)
	{
		TSharedRef<FJsonObject> Message = MakeMessage(TEXT("unregister"));
		Message->SetStringField(TEXT("instance_id"), Config.InstanceId);
		QueueMapJson(Message);
		FlushSocket(CoordinatorSocket, CoordinatorSendBuffer, CoordinatorSendOffset);
	}
	StopNetworking();
	Super::Deinitialize();
}

void UMT2ServerRuntimeSubsystem::Tick(float DeltaTime)
{
	FRAMEPRO_NAMED_SCOPE("MT2.ServerRuntime.Tick");
	TRACE_CPUPROFILER_EVENT_SCOPE(MT2_ServerRuntime_Tick);
	const double NowSeconds = FPlatformTime::Seconds();
	if (Config.IsCoordinator()) TickCoordinator(NowSeconds);
	else if (Config.IsMapServer() || Config.IsGateway()) TickMapClient(NowSeconds);
}

TStatId UMT2ServerRuntimeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMT2ServerRuntimeSubsystem, STATGROUP_Tickables);
}

bool UMT2ServerRuntimeSubsystem::IsTickable() const
{
	return !HasAnyFlags(RF_ClassDefaultObject) && Config.Mode != EMT2ServerRuntimeMode::Client;
}

EMT2MapServerState UMT2ServerRuntimeSubsystem::GetMapServerState() const
{
	if (!Config.IsMapServer()) return EMT2MapServerState::Starting;
	if (bLocalMaintenancePreparing) return EMT2MapServerState::Draining;
	return IsConfiguredWorldReady() && bCoordinatorRegistered
		? EMT2MapServerState::Ready : EMT2MapServerState::LoadingWorld;
}

bool UMT2ServerRuntimeSubsystem::IsConfiguredWorldReady() const
{
	const UWorld* World = GetWorld();
	return Config.IsMapServer() && World && World->HasBegunPlay() &&
		FPackageName::GetShortName(World->GetOutermost()->GetName()).Equals(
			FPackageName::GetShortName(Config.MapPath), ESearchCase::IgnoreCase);
}

bool UMT2ServerRuntimeSubsystem::ParseCommandLine(FString& OutError)
{
	Config.Mode = EMT2ServerRuntimeMode::Client;
	if (!IsRunningDedicatedServer() && !FParse::Param(FCommandLine::Get(), TEXT("Coordinator")) &&
		!FParse::Param(FCommandLine::Get(), TEXT("Gateway"))) return true;

	Config.Mode = FParse::Param(FCommandLine::Get(), TEXT("Coordinator"))
		? EMT2ServerRuntimeMode::Coordinator
		: (FParse::Param(FCommandLine::Get(), TEXT("Gateway"))
			? EMT2ServerRuntimeMode::Gateway : EMT2ServerRuntimeMode::Map);
	ReadCommandString(TEXT("instance_id="), Config.InstanceId);
	if (Config.InstanceId.IsEmpty()) Config.InstanceId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	ReadCommandString(TEXT("co_ip="), Config.CoordinatorIp);
	ReadCommandString(TEXT("co_bind="), Config.CoordinatorBindIp);
	ReadCommandString(TEXT("co_map="), Config.CoordinatorMapPath);
	ReadCommandString(TEXT("gateway_map="), Config.GatewayMapPath);
	ReadCommandInt(TEXT("co_port="), Config.CoordinatorPort);
	ReadCommandFloat(TEXT("heartbeat="), Config.HeartbeatIntervalSeconds);
	ReadCommandFloat(TEXT("server_timeout="), Config.ServerTimeoutSeconds);
	ReadCommandFloat(TEXT("ticket_lifetime="), Config.TransferTicketLifetimeSeconds);
	ReadCommandFloat(TEXT("request_timeout="), Config.RequestTimeoutSeconds);
	ReadCommandInt(TEXT("server_tick_rate="), Config.ServerTickRate);
	Config.ServerTickRate = FMath::Clamp(Config.ServerTickRate, 10, 60);
	Config.bIncrementalGarbageCollection = ReadCommandBool(TEXT("incremental_gc="), true);
	ReadCommandFloat(TEXT("gc_budget_ms="), Config.GarbageCollectionTimeBudgetMilliseconds);
	Config.GarbageCollectionTimeBudgetMilliseconds =
		FMath::Clamp(Config.GarbageCollectionTimeBudgetMilliseconds, 0.25f, 10.0f);
	Config.bAllowAccountRegistration = ReadCommandBool(TEXT("allow_registration="), false);
	Config.CoordinatorToken = ReadSecret(TEXT("co_token="), TEXT("co_token_env="), TEXT("MT2_COORDINATOR_TOKEN"));

	Config.CoordinatorPort = FMath::Clamp(Config.CoordinatorPort, 1, 65535);
	Config.HeartbeatIntervalSeconds = FMath::Clamp(Config.HeartbeatIntervalSeconds, 1.0f, 60.0f);
	Config.ServerTimeoutSeconds = FMath::Max(Config.ServerTimeoutSeconds, Config.HeartbeatIntervalSeconds * 2.0f);
	Config.TransferTicketLifetimeSeconds = FMath::Clamp(Config.TransferTicketLifetimeSeconds, 5.0f, 300.0f);
	Config.RequestTimeoutSeconds = FMath::Clamp(Config.RequestTimeoutSeconds, 2.0f, 120.0f);
	if (Config.CoordinatorToken.Len() < 16)
	{
		OutError = TEXT("coordinator token is missing or shorter than 16 characters. Use -co_token or -co_token_env.");
		return false;
	}

	if (Config.IsCoordinator())
	{
		Config.DatabaseRoot = UMT2PathSettings::Path(TEXT("DatabaseRoot"));
		ReadCommandString(TEXT("db_root="), Config.DatabaseRoot);
		ReadCommandString(TEXT("db_file="), Config.DatabaseFile);
		ReadCommandString(TEXT("db_synchronous="), Config.DatabaseSynchronousMode);
		ReadCommandInt(TEXT("db_busy_timeout="), Config.DatabaseBusyTimeoutMilliseconds);
		ReadCommandFloat(TEXT("autosave="), Config.AutosaveIntervalSeconds);
		Config.bCheckDatabaseIntegrity = ReadCommandBool(TEXT("db_integrity_check="), true);
		Config.DatabaseRoot = FPaths::ConvertRelativePathToFull(Config.DatabaseRoot);
		FPaths::NormalizeDirectoryName(Config.DatabaseRoot);
		Config.DatabaseSynchronousMode.ToUpperInline();
		Config.DatabaseBusyTimeoutMilliseconds = FMath::Clamp(
			Config.DatabaseBusyTimeoutMilliseconds, 0, 120000);
		Config.AutosaveIntervalSeconds = FMath::Max(Config.AutosaveIntervalSeconds, 5.0f);
		const auto IsSafeDatabaseFile = [](const FString& Filename)
		{
			return !Filename.IsEmpty() && FPaths::GetCleanFilename(Filename) == Filename &&
				!Filename.Contains(TEXT(".."));
		};
		if (!Config.HasDatabaseConfiguration())
		{
			OutError = TEXT("coordinator SQLite configuration is incomplete. Supply db_root and db_file.");
			return false;
		}
		if (Config.DatabaseRoot.StartsWith(TEXT("\\\\")) || Config.DatabaseRoot.StartsWith(TEXT("//")) ||
			!IsSafeDatabaseFile(Config.DatabaseFile))
		{
			OutError = TEXT("SQLite database must use local storage and a simple filename below db_root.");
			return false;
		}
		if (Config.DatabaseSynchronousMode != TEXT("FULL") &&
			Config.DatabaseSynchronousMode != TEXT("NORMAL"))
		{
			OutError = TEXT("db_synchronous must be FULL or NORMAL.");
			return false;
		}
		return true;
	}

	if (Config.IsGateway())
	{
		ReadCommandString(TEXT("public_ip="), Config.PublicIp);
		Config.GamePort = 11000;
		ReadCommandInt(TEXT("port="), Config.GamePort);
		ReadCommandInt(TEXT("max_players="), Config.MaxPlayers);
		Config.GamePort = FMath::Clamp(Config.GamePort, 1, 65535);
		Config.MaxPlayers = FMath::Max(Config.MaxPlayers, 1);
		if (Config.PublicIp.IsEmpty() || Config.CoordinatorIp.IsEmpty() || Config.GatewayMapPath.IsEmpty())
		{
			OutError = TEXT("gateway requires public_ip, co_ip, and gateway_map parameters.");
			return false;
		}
		return true;
	}

	ReadCommandString(TEXT("map="), Config.MapPath);
	ReadCommandString(TEXT("map_id="), Config.MapId);
	ReadCommandInt(TEXT("channel="), Config.Channel);
	Config.Channel = FMath::Max(Config.Channel, 1);
	ReadCommandString(TEXT("public_ip="), Config.PublicIp);
	Config.GamePort = 11000 + Config.Channel;
	ReadCommandInt(TEXT("port="), Config.GamePort);
	ReadCommandInt(TEXT("max_players="), Config.MaxPlayers);
	Config.GamePort = FMath::Clamp(Config.GamePort, 1, 65535);
	Config.MaxPlayers = FMath::Max(Config.MaxPlayers, 1);
	if (Config.MapId.IsEmpty() && !Config.MapPath.IsEmpty()) Config.MapId = FPackageName::GetShortName(Config.MapPath);
	if (Config.MapPath.IsEmpty() || Config.MapId.IsEmpty() || Config.PublicIp.IsEmpty() || Config.CoordinatorIp.IsEmpty())
	{
		OutError = TEXT("map server requires map, map_id (or derivable map name), public_ip, and co_ip parameters.");
		return false;
	}
	return true;
}

void UMT2ServerRuntimeSubsystem::HandlePostWorldInitialization(
	UWorld* World, const UWorld::InitializationValues InitializationValues)
{
	if (Config.Mode != EMT2ServerRuntimeMode::Client && World && World->GetGameInstance() == GetGameInstance())
	{
		if ((Config.IsMapServer() || Config.IsGateway()) && World->GetNetDriver())
		{
			World->GetNetDriver()->SetNetServerMaxTickRate(Config.ServerTickRate);
			UE_LOG(LogMT2ServerRuntime, Display, TEXT("Server tick rate set to %d Hz."),
				Config.ServerTickRate);
		}
		TryTravelToConfiguredMap(World);
	}
}

void UMT2ServerRuntimeSubsystem::TryTravelToConfiguredMap(UWorld* World)
{
	if (bMapTravelRequested || !World || World->WorldType != EWorldType::Game) return;
	const FString TargetPath = Config.IsCoordinator() ? Config.CoordinatorMapPath
		: (Config.IsGateway() ? Config.GatewayMapPath : Config.MapPath);
	const FString CurrentMap = FPackageName::GetShortName(World->GetOutermost()->GetName());
	const FString TargetMap = FPackageName::GetShortName(TargetPath);
	if (CurrentMap.Equals(TargetMap, ESearchCase::IgnoreCase)) return;
	bMapTravelRequested = true;
	World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, World]()
	{
		if (IsValid(World))
		{
			const FString TargetPath = Config.IsCoordinator() ? Config.CoordinatorMapPath
				: (Config.IsGateway() ? Config.GatewayMapPath : Config.MapPath);
			UE_LOG(LogMT2ServerRuntime, Display, TEXT("Traveling server runtime to %s."), *TargetPath);
			World->ServerTravel(TargetPath, true);
		}
	}));
}

bool UMT2ServerRuntimeSubsystem::StartCoordinatorListener()
{
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	TSharedRef<FInternetAddr> Address = SocketSubsystem->CreateInternetAddr();
	bool bValidAddress = false;
	Address->SetIp(*Config.CoordinatorBindIp, bValidAddress);
	Address->SetPort(Config.CoordinatorPort);
	if (!bValidAddress)
	{
		UE_LOG(LogMT2ServerRuntime, Error, TEXT("Invalid co_bind address: %s"), *Config.CoordinatorBindIp);
		return false;
	}

	ListenerSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("MT2CoordinatorListener"), false);
	if (!ListenerSocket) return false;
	ListenerSocket->SetReuseAddr(true);
	ListenerSocket->SetNonBlocking(true);
	if (!ListenerSocket->Bind(*Address) || !ListenerSocket->Listen(128))
	{
		UE_LOG(LogMT2ServerRuntime, Error, TEXT("Could not bind coordinator listener to %s:%d."),
			*Config.CoordinatorBindIp, Config.CoordinatorPort);
		DestroySocket(ListenerSocket);
		return false;
	}
	return true;
}

void UMT2ServerRuntimeSubsystem::StopNetworking()
{
	for (TUniquePtr<FCoordinatorPeer>& Peer : CoordinatorPeers)
	{
		if (Peer) DestroySocket(Peer->Socket);
	}
	CoordinatorPeers.Reset();
	DestroySocket(ListenerSocket);
	DestroySocket(CoordinatorSocket);
	RegisteredMapServers.Reset();
	TransferTickets.Reset();
	CoordinatorParties.Reset();
	AccountSessions.Reset();
	SessionByAccount.Reset();
	CharacterLeases.Reset();
	PendingRequestDeadlines.Reset();
}

void UMT2ServerRuntimeSubsystem::TickCoordinator(double NowSeconds)
{
	FRAMEPRO_NAMED_SCOPE("MT2.ServerRuntime.Coordinator");
	TRACE_CPUPROFILER_EVENT_SCOPE(MT2_ServerRuntime_Coordinator);
	TryTravelToConfiguredMap(GetWorld());
	AcceptCoordinatorPeers();
	PollCoordinatorPeers(NowSeconds);
	for (auto It = RegisteredMapServers.CreateIterator(); It; ++It)
	{
		if (NowSeconds - It.Value().LastHeartbeatSeconds > Config.ServerTimeoutSeconds)
		{
			UE_LOG(LogMT2ServerRuntime, Warning, TEXT("Map server timed out: %s"), *It.Key());
			It.RemoveCurrent();
		}
	}
	for (auto It = TransferTickets.CreateIterator(); It; ++It)
	{
		if (NowSeconds >= It.Value().ExpiresAtSeconds) It.RemoveCurrent();
	}
	for (auto It = AccountSessions.CreateIterator(); It; ++It)
	{
		if (NowSeconds >= It.Value().ExpiresAtSeconds)
		{
			SessionByAccount.Remove(It.Value().AccountId);
			It.RemoveCurrent();
		}
	}
	TArray<FString> ExpiredCharacters;
	for (auto It = CharacterLeases.CreateIterator(); It; ++It)
	{
		if (NowSeconds >= It.Value().ExpiresAtSeconds)
		{
			UE_LOG(LogMT2ServerRuntime, Display, TEXT("Character lease expired: %s"), *It.Key());
			ExpiredCharacters.Add(It.Key());
			It.RemoveCurrent();
		}
	}
	if (!ExpiredCharacters.IsEmpty())
	{
		TArray<FString> DisbandedParties;
		TArray<FString> UpdatedParties;
		for (TPair<FString, FCoordinatorParty>& Pair : CoordinatorParties)
		{
			const bool bLeaderRemoved = Pair.Value.Members.ContainsByPredicate(
				[&](const FMT2PartyMemberData& Member)
				{
					return Member.bLeader && ExpiredCharacters.Contains(Member.CharacterId);
				});
			const int32 Removed = Pair.Value.Members.RemoveAll(
				[&](const FMT2PartyMemberData& Member)
				{
					return ExpiredCharacters.Contains(Member.CharacterId);
				});
			if (Removed == 0) continue;
			if (Pair.Value.Members.Num() < 2)
			{
				DisbandedParties.Add(Pair.Key);
			}
			else
			{
				if (bLeaderRemoved)
				{
					for (FMT2PartyMemberData& Member : Pair.Value.Members) Member.bLeader = false;
					Pair.Value.Members[0].bLeader = true;
				}
				UpdatedParties.Add(Pair.Key);
			}
		}
		for (const FString& PartyId : UpdatedParties)
		{
			if (const FCoordinatorParty* Party = CoordinatorParties.Find(PartyId))
			{
				BroadcastPartySnapshot(*Party);
			}
		}
		for (const FString& PartyId : DisbandedParties)
		{
			CoordinatorParties.Remove(PartyId);
			BroadcastPartyDisband(PartyId);
		}
	}
	if (ClusterControl.IsActive() && NowSeconds >= ClusterControl.DeadlineSeconds)
	{
		if (ClusterControl.Error.IsEmpty())
		{
			ClusterControl.Error = TEXT("Timed out waiting for one or more server processes.");
		}
		ClusterControl.PendingInstances.Reset();
		CompleteCoordinatorControl();
	}
}

void UMT2ServerRuntimeSubsystem::TickMapClient(double NowSeconds)
{
	FRAMEPRO_NAMED_SCOPE("MT2.ServerRuntime.MapClient");
	TRACE_CPUPROFILER_EVENT_SCOPE(MT2_ServerRuntime_MapClient);
	if (UWorld* World = GetWorld())
	{
		if (UNetDriver* NetDriver = World->GetNetDriver();
			NetDriver && NetDriver->GetNetServerMaxTickRate() != Config.ServerTickRate)
		{
			NetDriver->SetNetServerMaxTickRate(Config.ServerTickRate);
			UE_LOG(LogMT2ServerRuntime, Display, TEXT("Server tick rate set to %d Hz."),
				Config.ServerTickRate);
		}
	}
	TryTravelToConfiguredMap(GetWorld());
	ExpirePendingRequests(NowSeconds);
	if (!CoordinatorSocket)
	{
		if (NowSeconds >= NextCoordinatorConnectSeconds) BeginCoordinatorConnection(NowSeconds);
		return;
	}
	if (bCoordinatorConnectPending)
	{
		const ESocketConnectionState State = CoordinatorSocket->GetConnectionState();
		if (State == SCS_Connected)
		{
			bCoordinatorConnectPending = false;
			TSharedRef<FJsonObject> Auth = MakeMessage(TEXT("auth"));
			Auth->SetStringField(TEXT("role"), Config.IsGateway() ? TEXT("gateway") : TEXT("map"));
			Auth->SetStringField(TEXT("instance_id"), Config.InstanceId);
			Auth->SetStringField(TEXT("token"), Config.CoordinatorToken);
			Auth->SetNumberField(TEXT("protocol_version"), CoordinatorProtocolVersion);
			Auth->SetStringField(TEXT("build_version"), GetRuntimeBuildVersion());
			Auth->SetStringField(TEXT("public_ip"), Config.PublicIp);
			Auth->SetNumberField(TEXT("game_port"), Config.GamePort);
			Auth->SetNumberField(TEXT("max_players"), Config.MaxPlayers);
			QueueMapJson(Auth);
		}
		else if (State == SCS_ConnectionError || NowSeconds - CoordinatorConnectStartedSeconds > 5.0)
		{
			DisconnectMapCoordinator(TEXT("connection failed"));
			return;
		}
	}
	PollMapCoordinatorConnection(NowSeconds);
	if ((Config.IsMapServer() || Config.IsGateway()) && bCoordinatorRegistered &&
		NowSeconds >= NextHeartbeatSeconds)
	{
		int32 PlayerCount = 0;
		TArray<TSharedPtr<FJsonValue>> OnlineCharacters;
		if (UWorld* World = GetWorld())
		{
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				++PlayerCount;
				if (const AMT2PlayerState* State = It->Get()->GetPlayerState<AMT2PlayerState>())
				{
					if (const UMT2PersistenceComponent* Persistence = State->GetPersistenceComponent();
						Persistence && !Persistence->GetEntityId().IsEmpty())
					{
						OnlineCharacters.Add(MakeShared<FJsonValueString>(Persistence->GetEntityId()));
					}
				}
			}
		}
		TSharedRef<FJsonObject> Heartbeat = MakeMessage(
			Config.IsGateway() ? TEXT("service_heartbeat") : TEXT("heartbeat"));
		Heartbeat->SetStringField(TEXT("instance_id"), Config.InstanceId);
		Heartbeat->SetNumberField(TEXT("players"), PlayerCount);
		if (Config.IsMapServer())
		{
			Heartbeat->SetBoolField(TEXT("ready"), IsConfiguredWorldReady() && !bLocalMaintenancePreparing);
			Heartbeat->SetBoolField(TEXT("draining"), bLocalMaintenancePreparing);
			Heartbeat->SetArrayField(TEXT("characters"), OnlineCharacters);
		}
		QueueMapJson(Heartbeat);
		NextHeartbeatSeconds = NowSeconds + Config.HeartbeatIntervalSeconds;
	}
}

void UMT2ServerRuntimeSubsystem::AcceptCoordinatorPeers()
{
	FRAMEPRO_NAMED_SCOPE("MT2.ServerRuntime.AcceptPeers");
	if (!ListenerSocket) return;
	bool bPending = false;
	while (ListenerSocket->HasPendingConnection(bPending) && bPending)
	{
		FSocket* Socket = ListenerSocket->Accept(TEXT("MT2CoordinatorPeer"));
		if (!Socket) break;
		Socket->SetNonBlocking(true);
		Socket->SetNoDelay(true);
		TUniquePtr<FCoordinatorPeer> Peer = MakeUnique<FCoordinatorPeer>();
		Peer->Socket = Socket;
		Peer->ConnectedAtSeconds = FPlatformTime::Seconds();
		CoordinatorPeers.Add(MoveTemp(Peer));
	}
}

void UMT2ServerRuntimeSubsystem::PollCoordinatorPeers(double NowSeconds)
{
	FRAMEPRO_NAMED_SCOPE("MT2.ServerRuntime.PollCoordinatorPeers");
	TRACE_CPUPROFILER_EVENT_SCOPE(MT2_ServerRuntime_PollCoordinatorPeers);
	for (int32 Index = CoordinatorPeers.Num() - 1; Index >= 0; --Index)
	{
		FCoordinatorPeer& Peer = *CoordinatorPeers[Index];
		const bool bAlive = PollSocket(Peer.Socket, Peer.ReceiveBuffer,
			[this, &Peer](const TSharedPtr<FJsonObject>& Message) { HandleCoordinatorPeerMessage(Peer, Message); });
		const bool bFlushed = FlushSocket(Peer.Socket, Peer.SendBuffer, Peer.SendOffset);
		if (!bAlive || !bFlushed || (!Peer.bAuthenticated && NowSeconds - Peer.ConnectedAtSeconds > 5.0) ||
			(Peer.bCloseRequested && Peer.SendBuffer.IsEmpty())) RemoveCoordinatorPeer(Index);
	}
}

void UMT2ServerRuntimeSubsystem::PollMapCoordinatorConnection(double NowSeconds)
{
	FRAMEPRO_NAMED_SCOPE("MT2.ServerRuntime.PollMapConnection");
	TRACE_CPUPROFILER_EVENT_SCOPE(MT2_ServerRuntime_PollMapConnection);
	if (!CoordinatorSocket || bCoordinatorConnectPending) return;
	const bool bAlive = PollSocket(CoordinatorSocket, CoordinatorReceiveBuffer,
		[this](const TSharedPtr<FJsonObject>& Message) { HandleMapCoordinatorMessage(Message); });
	if (!bAlive || !FlushSocket(CoordinatorSocket, CoordinatorSendBuffer, CoordinatorSendOffset))
	{
		DisconnectMapCoordinator(TEXT("connection closed"));
	}
}

void UMT2ServerRuntimeSubsystem::BeginCoordinatorConnection(double NowSeconds)
{
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	TSharedRef<FInternetAddr> Address = SocketSubsystem->CreateInternetAddr();
	bool bValidAddress = false;
	Address->SetIp(*Config.CoordinatorIp, bValidAddress);
	Address->SetPort(Config.CoordinatorPort);
	if (!bValidAddress)
	{
		UE_LOG(LogMT2ServerRuntime, Error, TEXT("Invalid coordinator address: %s"), *Config.CoordinatorIp);
		NextCoordinatorConnectSeconds = NowSeconds + 5.0;
		return;
	}

	CoordinatorSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("MT2CoordinatorClient"), false);
	if (!CoordinatorSocket)
	{
		NextCoordinatorConnectSeconds = NowSeconds + 5.0;
		return;
	}
	CoordinatorSocket->SetNonBlocking(true);
	CoordinatorSocket->SetNoDelay(true);
	const bool bConnectedImmediately = CoordinatorSocket->Connect(*Address);
	bCoordinatorConnectPending = !bConnectedImmediately;
	CoordinatorConnectStartedSeconds = NowSeconds;
	if (bConnectedImmediately)
	{
		TSharedRef<FJsonObject> Auth = MakeMessage(TEXT("auth"));
		Auth->SetStringField(TEXT("role"), Config.IsGateway() ? TEXT("gateway") : TEXT("map"));
		Auth->SetStringField(TEXT("instance_id"), Config.InstanceId);
		Auth->SetStringField(TEXT("token"), Config.CoordinatorToken);
		Auth->SetNumberField(TEXT("protocol_version"), CoordinatorProtocolVersion);
		Auth->SetStringField(TEXT("build_version"), GetRuntimeBuildVersion());
		Auth->SetStringField(TEXT("public_ip"), Config.PublicIp);
		Auth->SetNumberField(TEXT("game_port"), Config.GamePort);
		Auth->SetNumberField(TEXT("max_players"), Config.MaxPlayers);
		QueueMapJson(Auth);
	}
}

void UMT2ServerRuntimeSubsystem::DisconnectMapCoordinator(const FString& Reason)
{
	if (CoordinatorSocket) UE_LOG(LogMT2ServerRuntime, Warning, TEXT("Coordinator disconnected: %s."), *Reason);
	DestroySocket(CoordinatorSocket);
	CoordinatorReceiveBuffer.Reset();
	CoordinatorSendBuffer.Reset();
	CoordinatorSendOffset = 0;
	bCoordinatorConnectPending = false;
	bCoordinatorAuthenticated = false;
	bCoordinatorRegistered = false;
	NextCoordinatorConnectSeconds = FPlatformTime::Seconds() + 5.0;

	for (TPair<FString, TFunction<void(FMT2PersistenceLoadResult&&)>>& Pair : PendingLoads)
	{
		FMT2PersistenceLoadResult Result; Result.Error = TEXT("Coordinator connection lost."); Pair.Value(MoveTemp(Result));
	}
	for (TPair<FString, TFunction<void(FMT2PersistenceSaveResult&&)>>& Pair : PendingSaves)
	{
		FMT2PersistenceSaveResult Result; Result.Error = TEXT("Coordinator connection lost."); Pair.Value(MoveTemp(Result));
	}
	for (TPair<FString, TFunction<void(FMT2MapRouteResult&&)>>& Pair : PendingRoutes)
	{
		FMT2MapRouteResult Result; Result.Error = TEXT("Coordinator connection lost."); Pair.Value(MoveTemp(Result));
	}
	for (TPair<FString, TFunction<void(FMT2TransferTicketResult&&)>>& Pair : PendingTransferTickets)
	{
		FMT2TransferTicketResult Result; Result.Error = TEXT("Coordinator connection lost."); Pair.Value(MoveTemp(Result));
	}
	for (TPair<FString, TFunction<void(FMT2TransferClaimResult&&)>>& Pair : PendingTransferClaims)
	{
		FMT2TransferClaimResult Result; Result.Error = TEXT("Coordinator connection lost."); Pair.Value(MoveTemp(Result));
	}
	for (TPair<FString, TFunction<void(FMT2TransferCompleteResult&&)>>& Pair : PendingTransferCompletions)
	{
		FMT2TransferCompleteResult Result; Result.Error = TEXT("Coordinator connection lost."); Pair.Value(MoveTemp(Result));
	}
	for (TPair<FString, TFunction<void(FMT2AccountRegistrationResult&&)>>& Pair : PendingAccountRegistrations)
	{
		FMT2AccountRegistrationResult Result; Result.Error = TEXT("Coordinator connection lost."); Pair.Value(MoveTemp(Result));
	}
	for (TPair<FString, TFunction<void(FMT2AccountLoginResult&&)>>& Pair : PendingAccountLogins)
	{
		FMT2AccountLoginResult Result; Result.Error = TEXT("Coordinator connection lost."); Pair.Value(MoveTemp(Result));
	}
	for (TPair<FString, TFunction<void(FMT2CharacterCreateResult&&)>>& Pair : PendingCharacterCreations)
	{
		FMT2CharacterCreateResult Result; Result.Error = TEXT("Coordinator connection lost."); Pair.Value(MoveTemp(Result));
	}
	for (TPair<FString, TFunction<void(FMT2CharacterAdmissionResult&&)>>& Pair : PendingCharacterAdmissions)
	{
		FMT2CharacterAdmissionResult Result; Result.Error = TEXT("Coordinator connection lost."); Pair.Value(MoveTemp(Result));
	}
	for (TPair<FString, TFunction<void(bool, const FString&)>>& Pair : PendingAdminControls)
	{
		Pair.Value(false, TEXT("Coordinator connection lost."));
	}
	PendingLoads.Reset(); PendingSaves.Reset(); PendingRoutes.Reset();
	PendingTransferTickets.Reset(); PendingTransferClaims.Reset();
	PendingTransferCompletions.Reset(); PendingAccountRegistrations.Reset();
	PendingAccountLogins.Reset(); PendingCharacterCreations.Reset(); PendingCharacterAdmissions.Reset();
	PendingAdminControls.Reset();
	PendingRequestDeadlines.Reset();
}

bool UMT2ServerRuntimeSubsystem::PollSocket(
	FSocket* Socket, TArray<uint8>& ReceiveBuffer,
	TFunctionRef<void(const TSharedPtr<FJsonObject>&)> MessageHandler)
{
	FRAMEPRO_NAMED_SCOPE("MT2.ServerRuntime.PollSocket");
	if (!Socket || Socket->GetConnectionState() != SCS_Connected) return false;
	uint32 PendingBytes = 0;
	while (Socket->HasPendingData(PendingBytes) && PendingBytes > 0)
	{
		if (ReceiveBuffer.Num() + static_cast<int32>(PendingBytes) > MaxCoordinatorMessageBytes) return false;
		const int32 PreviousNum = ReceiveBuffer.Num();
		const int32 ToRead = FMath::Min<int32>(PendingBytes, 65536);
		ReceiveBuffer.AddUninitialized(ToRead);
		int32 Read = 0;
		if (!Socket->Recv(ReceiveBuffer.GetData() + PreviousNum, ToRead, Read) || Read <= 0)
		{
			ReceiveBuffer.SetNum(PreviousNum, EAllowShrinking::No);
			return Socket->GetConnectionState() == SCS_Connected;
		}
		ReceiveBuffer.SetNum(PreviousNum + Read, EAllowShrinking::No);
	}

	for (;;)
	{
		const int32 NewlineIndex = ReceiveBuffer.IndexOfByKey(static_cast<uint8>('\n'));
		if (NewlineIndex == INDEX_NONE) break;
		FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(ReceiveBuffer.GetData()), NewlineIndex);
		const FString Line(Converted.Length(), Converted.Get());
		ReceiveBuffer.RemoveAt(0, NewlineIndex + 1, EAllowShrinking::No);
		TSharedPtr<FJsonObject> Message;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Line);
		if (FJsonSerializer::Deserialize(Reader, Message) && Message.IsValid()) MessageHandler(Message);
		else return false;
	}
	return Socket->GetConnectionState() != SCS_ConnectionError;
}

bool UMT2ServerRuntimeSubsystem::FlushSocket(FSocket* Socket, TArray<uint8>& SendBuffer, int32& SendOffset)
{
	FRAMEPRO_NAMED_SCOPE("MT2.ServerRuntime.FlushSocket");
	if (!Socket) return false;
	while (SendOffset < SendBuffer.Num())
	{
		int32 Sent = 0;
		if (!Socket->Send(SendBuffer.GetData() + SendOffset, SendBuffer.Num() - SendOffset, Sent))
		{
			return Socket->GetConnectionState() == SCS_Connected;
		}
		if (Sent <= 0) break;
		SendOffset += Sent;
	}
	if (SendOffset >= SendBuffer.Num())
	{
		SendBuffer.Reset();
		SendOffset = 0;
	}
	return true;
}

bool UMT2ServerRuntimeSubsystem::QueueJson(
	TArray<uint8>& SendBuffer, const TSharedRef<FJsonObject>& Message) const
{
	FRAMEPRO_NAMED_SCOPE("MT2.ServerRuntime.SerializeJson");
	FString Json;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
	FJsonSerializer::Serialize(Message, Writer);
	FTCHARToUTF8 Converted(*Json);
	if (Converted.Length() + 1 > MaxCoordinatorMessageBytes ||
		SendBuffer.Num() + Converted.Length() + 1 > MaxCoordinatorSendBufferBytes)
	{
		UE_LOG(LogMT2ServerRuntime, Error, TEXT("Coordinator send buffer limit exceeded."));
		return false;
	}
	SendBuffer.Append(reinterpret_cast<const uint8*>(Converted.Get()), Converted.Length());
	SendBuffer.Add(static_cast<uint8>('\n'));
	return true;
}

void UMT2ServerRuntimeSubsystem::QueuePeerJson(
	FCoordinatorPeer& Peer, const TSharedRef<FJsonObject>& Message) const
{
	if (!QueueJson(Peer.SendBuffer, Message))
	{
		Peer.bCloseRequested = true;
	}
}

void UMT2ServerRuntimeSubsystem::QueueMapJson(const TSharedRef<FJsonObject>& Message)
{
	if (CoordinatorSocket && !QueueJson(CoordinatorSendBuffer, Message))
	{
		CoordinatorSocket->Shutdown(ESocketShutdownMode::ReadWrite);
	}
}

void UMT2ServerRuntimeSubsystem::PublishChat(
	EMT2Empire Empire, const FString& SenderName, const FString& Message,
	bool bBroadcastToAllServers)
{
	FString CleanSender = SenderName.TrimStartAndEnd().Left(24);
	FString CleanMessage = Message.TrimStartAndEnd().Left(256);
	CleanSender.ReplaceInline(TEXT("\r"), TEXT(" "));
	CleanSender.ReplaceInline(TEXT("\n"), TEXT(" "));
	CleanMessage.ReplaceInline(TEXT("\r"), TEXT(" "));
	CleanMessage.ReplaceInline(TEXT("\n"), TEXT(" "));
	if (CleanSender.IsEmpty() || CleanMessage.IsEmpty())
	{
		return;
	}

	if (bBroadcastToAllServers && Config.IsMapServer() && bCoordinatorAuthenticated)
	{
		TSharedRef<FJsonObject> Chat = MakeMessage(TEXT("global_chat_publish"));
		Chat->SetStringField(TEXT("sender"), CleanSender);
		Chat->SetStringField(TEXT("message"), CleanMessage);
		Chat->SetNumberField(TEXT("empire"), static_cast<uint8>(Empire));
		QueueMapJson(Chat);
		return;
	}

	// Normal chat remains inside this map-server instance. Global chat also falls back locally when
	// PIE is running without a coordinator or the coordinator is temporarily unavailable.
	OnChatReceived.Broadcast(Empire, CleanSender, CleanMessage, bBroadcastToAllServers);
}

void UMT2ServerRuntimeSubsystem::HandleCoordinatorPeerMessage(
	FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message)
{
	FRAMEPRO_NAMED_SCOPE("MT2.ServerRuntime.HandleCoordinatorMessage");
	TRACE_CPUPROFILER_EVENT_SCOPE(MT2_ServerRuntime_HandleCoordinatorMessage);
	const FString Type = GetStringField(Message, TEXT("type"));
	if (!Peer.bAuthenticated)
	{
		const FString InstanceId = GetStringField(Message, TEXT("instance_id"));
		const FString Role = GetStringField(Message, TEXT("role"));
		if (Type != TEXT("auth") || (Role != TEXT("map") && Role != TEXT("gateway")) ||
			InstanceId.IsEmpty() || GetIntField(Message, TEXT("protocol_version")) != CoordinatorProtocolVersion ||
			!MT2Authentication::ConstantTimeEquals(
				GetStringField(Message, TEXT("token")), Config.CoordinatorToken))
		{
			TSharedRef<FJsonObject> Failure = MakeMessage(TEXT("auth_failed"));
			Failure->SetStringField(TEXT("error"), TEXT("Coordinator authentication or protocol version rejected."));
			QueuePeerJson(Peer, Failure);
			Peer.bCloseRequested = true;
			return;
		}
		const FString PeerBuildVersion = GetStringField(Message, TEXT("build_version"));
		if (!PeerBuildVersion.Equals(GetRuntimeBuildVersion(), ESearchCase::CaseSensitive))
		{
			const FString Error = FString::Printf(
				TEXT("Server build version %s does not match coordinator version %s."),
				PeerBuildVersion.IsEmpty() ? TEXT("<missing>") : *PeerBuildVersion,
				*GetRuntimeBuildVersion());
			UE_LOG(LogMT2ServerRuntime, Error, TEXT("Rejected %s %s: %s"),
				*Role, *InstanceId, *Error);
			TSharedRef<FJsonObject> Failure = MakeMessage(TEXT("auth_failed"));
			Failure->SetStringField(TEXT("error"), Error);
			QueuePeerJson(Peer, Failure);
			Peer.bCloseRequested = true;
			return;
		}
		Peer.bAuthenticated = true;
		Peer.InstanceId = InstanceId;
		Peer.Role = Role;
		Peer.PublicIp = GetStringField(Message, TEXT("public_ip"));
		Peer.GamePort = GetIntField(Message, TEXT("game_port"));
		Peer.MaxPlayers = GetIntField(Message, TEXT("max_players"));
		Peer.BuildVersion = PeerBuildVersion;
		Peer.LastHeartbeatSeconds = FPlatformTime::Seconds();
		QueuePeerJson(Peer, MakeMessage(TEXT("auth_ok")));
		return;
	}

	if (Type == TEXT("admin_control")) HandleAdminControlRequest(Peer, Message);
	else if (Type == TEXT("control_ready")) HandleControlPeerReady(Peer, Message);
	else if (Type == TEXT("service_heartbeat"))
	{
		Peer.PlayerCount = FMath::Max(GetIntField(Message, TEXT("players")), 0);
		Peer.LastHeartbeatSeconds = FPlatformTime::Seconds();
	}
	else if (Peer.Role == TEXT("map") && Type == TEXT("register")) RegisterMapServer(Peer, Message);
	else if (Peer.Role == TEXT("map") && Type == TEXT("heartbeat")) UpdateMapServerHeartbeat(Peer, Message, FPlatformTime::Seconds());
	else if (Peer.Role == TEXT("map") && Type == TEXT("unregister")) RegisteredMapServers.Remove(Peer.InstanceId);
	else if (Peer.Role == TEXT("map") && Type == TEXT("persistence_load")) HandlePersistenceLoadRequest(Peer, Message);
	else if (Peer.Role == TEXT("map") && Type == TEXT("persistence_save")) HandlePersistenceSaveRequest(Peer, Message);
	else if (Peer.Role == TEXT("map") && Type == TEXT("route_request")) HandleRouteRequest(Peer, Message);
	else if (Peer.Role == TEXT("map") && Type == TEXT("transfer_issue")) HandleTransferIssueRequest(Peer, Message, FPlatformTime::Seconds());
	else if (Peer.Role == TEXT("map") && Type == TEXT("transfer_claim")) HandleTransferClaimRequest(Peer, Message, FPlatformTime::Seconds());
	else if (Peer.Role == TEXT("map") && Type == TEXT("transfer_complete")) HandleTransferCompleteRequest(Peer, Message);
	else if (Peer.Role == TEXT("map") && Type == TEXT("character_logout")) HandleCharacterLogout(Peer, Message);
	else if (Peer.Role == TEXT("map") && Type == TEXT("messenger_login")) HandleMessengerLogin(Peer, Message);
	else if (Peer.Role == TEXT("map") && Type == TEXT("messenger_request_add")) HandleMessengerRequestAdd(Peer, Message);
	else if (Peer.Role == TEXT("map") && Type == TEXT("messenger_answer")) HandleMessengerAnswerRequest(Peer, Message);
	else if (Peer.Role == TEXT("map") && Type == TEXT("messenger_remove")) HandleMessengerRemoveFriend(Peer, Message);
	else if (Peer.Role == TEXT("map") && Type == TEXT("messenger_send")) HandleMessengerSendMessage(Peer, Message);
	else if (Peer.Role == TEXT("map") && Type == TEXT("messenger_open")) HandleMessengerOpenConversation(Peer, Message);
	else if (Peer.Role == TEXT("map") && Type == TEXT("guild_login")) HandleGuildLogin(Peer, Message);
	else if (Peer.Role == TEXT("map") && Type == TEXT("guild_create")) HandleGuildCreate(Peer, Message);
	else if (Peer.Role == TEXT("map") && Type == TEXT("guild_invite")) HandleGuildInvite(Peer, Message, FPlatformTime::Seconds());
	else if (Peer.Role == TEXT("map") && Type == TEXT("guild_answer")) HandleGuildAnswer(Peer, Message, FPlatformTime::Seconds());
	else if (Peer.Role == TEXT("map") && Type == TEXT("guild_action")) HandleGuildAction(Peer, Message);
	else if (Peer.Role == TEXT("map") && Type == TEXT("guild_chat")) HandleGuildChat(Peer, Message);
	else if (Peer.Role == TEXT("map") && Type == TEXT("guild_mark_upload")) HandleGuildMarkUpload(Peer, Message);
	else if (Peer.Role == TEXT("map") && Type == TEXT("guild_mark_request")) HandleGuildMarkRequest(Peer, Message);
	else if (Peer.Role == TEXT("map") && Type == TEXT("party_update")) HandlePartyUpdate(Peer, Message);
	else if (Peer.Role == TEXT("map") && Type == TEXT("party_disband")) HandlePartyDisband(Peer, Message);
	else if (Peer.Role == TEXT("map") && Type == TEXT("global_chat_publish"))
	{
		const FString Sender = GetStringField(Message, TEXT("sender")).TrimStartAndEnd().Left(24);
		const FString ChatText = GetStringField(Message, TEXT("message")).TrimStartAndEnd().Left(256);
		const EMT2Empire Empire = static_cast<EMT2Empire>(FMath::Clamp(
			GetIntField(Message, TEXT("empire")), 0, 3));
		if (!Sender.IsEmpty() && !ChatText.IsEmpty())
		{
			TSharedRef<FJsonObject> Delivery = MakeMessage(TEXT("global_chat_deliver"));
			Delivery->SetStringField(TEXT("sender"), Sender);
			Delivery->SetStringField(TEXT("message"), ChatText);
			Delivery->SetNumberField(TEXT("empire"), static_cast<uint8>(Empire));
			for (const TUniquePtr<FCoordinatorPeer>& TargetPeer : CoordinatorPeers)
			{
				if (TargetPeer && TargetPeer->bAuthenticated && TargetPeer->Role == TEXT("map"))
				{
					QueuePeerJson(*TargetPeer, Delivery);
				}
			}
		}
	}
	else if (Peer.Role == TEXT("gateway") && Type == TEXT("account_register")) HandleAccountRegistrationRequest(Peer, Message);
	else if (Peer.Role == TEXT("gateway") && Type == TEXT("account_login")) HandleAccountLoginRequest(Peer, Message);
	else if (Peer.Role == TEXT("gateway") && Type == TEXT("character_create")) HandleCharacterCreationRequest(Peer, Message);
	else if (Peer.Role == TEXT("gateway") && Type == TEXT("character_admission"))
		HandleCharacterAdmissionRequest(Peer, Message, FPlatformTime::Seconds());
}

void UMT2ServerRuntimeSubsystem::HandleMapCoordinatorMessage(const TSharedPtr<FJsonObject>& Message)
{
	FRAMEPRO_NAMED_SCOPE("MT2.ServerRuntime.HandleMapMessage");
	TRACE_CPUPROFILER_EVENT_SCOPE(MT2_ServerRuntime_HandleMapMessage);
	const FString Type = GetStringField(Message, TEXT("type"));
	if (Type == TEXT("auth_failed"))
	{
		UE_LOG(LogMT2ServerRuntime, Error, TEXT("Coordinator authentication rejected: %s"),
			*GetStringField(Message, TEXT("error")));
		if (CoordinatorSocket) CoordinatorSocket->Shutdown(ESocketShutdownMode::ReadWrite);
		return;
	}
	if (Type == TEXT("register_failed"))
	{
		UE_LOG(LogMT2ServerRuntime, Error, TEXT("Coordinator registration rejected: %s"),
			*GetStringField(Message, TEXT("error")));
		if (CoordinatorSocket) CoordinatorSocket->Shutdown(ESocketShutdownMode::ReadWrite);
		return;
	}
	if (Type == TEXT("auth_ok"))
	{
		bCoordinatorAuthenticated = true;
		if (Config.IsGateway())
		{
			bCoordinatorRegistered = true;
			UE_LOG(LogMT2ServerRuntime, Display, TEXT("Gateway authenticated with coordinator."));
			return;
		}
		TSharedRef<FJsonObject> Register = MakeMessage(TEXT("register"));
		Register->SetStringField(TEXT("instance_id"), Config.InstanceId);
		Register->SetStringField(TEXT("map_id"), Config.MapId);
		Register->SetStringField(TEXT("map_path"), Config.MapPath);
		Register->SetNumberField(TEXT("channel"), Config.Channel);
		Register->SetStringField(TEXT("public_ip"), Config.PublicIp);
		Register->SetNumberField(TEXT("game_port"), Config.GamePort);
		Register->SetNumberField(TEXT("max_players"), Config.MaxPlayers);
		Register->SetBoolField(TEXT("ready"), IsConfiguredWorldReady());
		Register->SetStringField(TEXT("build_version"), GetRuntimeBuildVersion());
		QueueMapJson(Register);
		return;
	}
	if (Type == TEXT("registered"))
	{
		bCoordinatorRegistered = true;
		NextHeartbeatSeconds = 0.0;
		UE_LOG(LogMT2ServerRuntime, Display, TEXT("Map server registered with coordinator."));
		return;
	}
	if (Type == TEXT("global_chat_deliver"))
	{
		OnChatReceived.Broadcast(
			static_cast<EMT2Empire>(FMath::Clamp(GetIntField(Message, TEXT("empire")), 0, 3)),
			GetStringField(Message, TEXT("sender")), GetStringField(Message, TEXT("message")), true);
		return;
	}
	if (Type == TEXT("account_session_revoked"))
	{
		OnAccountSessionRevoked.Broadcast(
			Config.IsGateway() ? GetStringField(Message, TEXT("session_token"))
				: GetStringField(Message, TEXT("account_id")),
			GetStringField(Message, TEXT("reason")));
		return;
	}
	if (Type == TEXT("admin_control_result"))
	{
		const FString RequestId = GetStringField(Message, TEXT("request_id"));
		TFunction<void(bool, const FString&)> Completion;
		if (PendingAdminControls.RemoveAndCopyValue(RequestId, Completion))
		{
			PendingRequestDeadlines.Remove(RequestId);
			Completion(Message->GetBoolField(TEXT("success")), GetStringField(Message, TEXT("message")));
		}
		return;
	}
	if (Type == TEXT("control_persist"))
	{
		BeginLocalControl(EClusterControlAction::Persist, GetStringField(Message, TEXT("operation_id")));
		return;
	}
	if (Type == TEXT("control_prepare_exit"))
	{
		BeginLocalControl(
			Message->GetBoolField(TEXT("restart")) ? EClusterControlAction::Reboot
				: EClusterControlAction::Shutdown,
			GetStringField(Message, TEXT("operation_id")));
		return;
	}
	if (Type == TEXT("control_exit_commit"))
	{
		ScheduleLocalExit(Message->GetBoolField(TEXT("restart")));
		return;
	}
	if (Type == TEXT("summon"))
	{
		int32 DestChannel = 1;
		double X = 0.0, Y = 0.0, Z = 0.0;
		Message->TryGetNumberField(TEXT("dest_channel"), DestChannel);
		Message->TryGetNumberField(TEXT("x"), X);
		Message->TryGetNumberField(TEXT("y"), Y);
		Message->TryGetNumberField(TEXT("z"), Z);
		ExecuteLocalSummon(
			GetStringField(Message, TEXT("dest_instance")), GetStringField(Message, TEXT("dest_map")),
			DestChannel, FVector(X, Y, Z));
		return;
	}
	if (Type == TEXT("party_snapshot"))
	{
		const FString PartyId = GetStringField(Message, TEXT("party_id"));
		TArray<FMT2PartyMemberData> Members;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Message->TryGetArrayField(TEXT("members"), Values) && Values)
		{
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				FMT2PartyMemberData Member;
				if (Value.IsValid() && ReadPartyMember(Value->AsObject(), Member))
				{
					Members.Add(MoveTemp(Member));
				}
			}
		}
		ApplyPartySnapshotToWorld(PartyId, Members);
		return;
	}
	if (HandleMapMessengerMessage(Type, Message))
	{
		return;
	}
	if (HandleMapGuildMessage(Type, Message)) return;
	if (Type == TEXT("party_disband"))
	{
		ApplyPartyDisbandToWorld(GetStringField(Message, TEXT("party_id")));
		return;
	}

	const FString RequestId = GetStringField(Message, TEXT("request_id"));
	if (!RequestId.IsEmpty()) PendingRequestDeadlines.Remove(RequestId);
	if (Type == TEXT("persistence_load_result"))
	{
		TFunction<void(FMT2PersistenceLoadResult&&)> Completion;
		if (!PendingLoads.RemoveAndCopyValue(RequestId, Completion)) return;
		FMT2PersistenceLoadResult Result;
		Result.bSucceeded = Message->GetBoolField(TEXT("success"));
		Result.bFound = Message->GetBoolField(TEXT("found"));
		Result.Error = GetStringField(Message, TEXT("error"));
		if (Result.bFound)
		{
			Result.Record.EntityType = FName(*GetStringField(Message, TEXT("entity_type")));
			Result.Record.EntityId = GetStringField(Message, TEXT("entity_id"));
			Result.Record.OwnerId = GetStringField(Message, TEXT("owner_id"));
			Result.Record.SchemaVersion = GetIntField(Message, TEXT("schema_version"), 1);
			Result.Record.Revision = FCString::Atoi64(*GetStringField(Message, TEXT("revision")));
			Result.Record.bHasWorldPosition = Message->GetBoolField(TEXT("has_world_position"));
			Result.Record.PayloadJson = GetStringField(Message, TEXT("payload"));
		}
		Completion(MoveTemp(Result));
	}
	else if (Type == TEXT("persistence_save_result"))
	{
		TFunction<void(FMT2PersistenceSaveResult&&)> Completion;
		if (!PendingSaves.RemoveAndCopyValue(RequestId, Completion)) return;
		FMT2PersistenceSaveResult Result;
		Result.bSucceeded = Message->GetBoolField(TEXT("success"));
		Result.Revision = FCString::Atoi64(*GetStringField(Message, TEXT("revision")));
		Result.Error = GetStringField(Message, TEXT("error"));
		Completion(MoveTemp(Result));
	}
	else if (Type == TEXT("route_result"))
	{
		TFunction<void(FMT2MapRouteResult&&)> Completion;
		if (!PendingRoutes.RemoveAndCopyValue(RequestId, Completion)) return;
		FMT2MapRouteResult Result;
		Result.bSucceeded = Message->GetBoolField(TEXT("success"));
		Result.Error = GetStringField(Message, TEXT("error"));
		if (Result.bSucceeded) Result.Server = ReadServerDescriptor(Message);
		Completion(MoveTemp(Result));
	}
	else if (Type == TEXT("transfer_ticket_result"))
	{
		TFunction<void(FMT2TransferTicketResult&&)> Completion;
		if (!PendingTransferTickets.RemoveAndCopyValue(RequestId, Completion)) return;
		FMT2TransferTicketResult Result;
		Result.bSucceeded = Message->GetBoolField(TEXT("success"));
		Result.Ticket = GetStringField(Message, TEXT("ticket"));
		Result.Error = GetStringField(Message, TEXT("error"));
		if (Result.bSucceeded) Result.Server = ReadServerDescriptor(Message);
		Completion(MoveTemp(Result));
	}
	else if (Type == TEXT("transfer_claim_result"))
	{
		TFunction<void(FMT2TransferClaimResult&&)> Completion;
		if (!PendingTransferClaims.RemoveAndCopyValue(RequestId, Completion)) return;
		FMT2TransferClaimResult Result;
		Result.bSucceeded = Message->GetBoolField(TEXT("success"));
		Result.bForceTownSpawn = Message->GetBoolField(TEXT("force_town_spawn"));
		Result.CharacterId = GetStringField(Message, TEXT("character_id"));
		Result.AccountId = GetStringField(Message, TEXT("account_id"));
		Result.SourceInstanceId = GetStringField(Message, TEXT("source_instance_id"));
		Result.Error = GetStringField(Message, TEXT("error"));
		const TSharedPtr<FJsonObject>* CharacterObject = nullptr;
		if (Result.bSucceeded && Message->TryGetObjectField(TEXT("character"), CharacterObject) && CharacterObject)
		{
			ReadCharacterSummary(*CharacterObject, Result.Character);
		}
		Completion(MoveTemp(Result));
	}
	else if (Type == TEXT("transfer_complete_result"))
	{
		TFunction<void(FMT2TransferCompleteResult&&)> Completion;
		if (!PendingTransferCompletions.RemoveAndCopyValue(RequestId, Completion)) return;
		FMT2TransferCompleteResult Result;
		Result.bSucceeded = Message->GetBoolField(TEXT("success"));
		Result.Error = GetStringField(Message, TEXT("error"));
		Completion(MoveTemp(Result));
	}
	else if (Type == TEXT("account_register_result"))
	{
		TFunction<void(FMT2AccountRegistrationResult&&)> Completion;
		if (!PendingAccountRegistrations.RemoveAndCopyValue(RequestId, Completion)) return;
		FMT2AccountRegistrationResult Result;
		Result.bSucceeded = Message->GetBoolField(TEXT("success"));
		Result.AccountId = GetStringField(Message, TEXT("account_id"));
		Result.Error = GetStringField(Message, TEXT("error"));
		Completion(MoveTemp(Result));
	}
	else if (Type == TEXT("account_login_result"))
	{
		TFunction<void(FMT2AccountLoginResult&&)> Completion;
		if (!PendingAccountLogins.RemoveAndCopyValue(RequestId, Completion)) return;
		FMT2AccountLoginResult Result;
		Result.bSucceeded = Message->GetBoolField(TEXT("success"));
		Result.bReplacedExistingSession = Message->GetBoolField(TEXT("replaced_existing_session"));
		Result.AccountId = GetStringField(Message, TEXT("account_id"));
		Result.SessionToken = GetStringField(Message, TEXT("session_token"));
		Result.Error = GetStringField(Message, TEXT("error"));
		const TArray<TSharedPtr<FJsonValue>>* CharacterValues = nullptr;
		if (Result.bSucceeded && Message->TryGetArrayField(TEXT("characters"), CharacterValues) && CharacterValues)
		{
			for (const TSharedPtr<FJsonValue>& Value : *CharacterValues)
			{
				FMT2CharacterSummary Character;
				if (Value.IsValid() && ReadCharacterSummary(Value->AsObject(), Character))
				{
					Result.Characters.Add(MoveTemp(Character));
				}
			}
		}
		Completion(MoveTemp(Result));
	}
	else if (Type == TEXT("character_create_result"))
	{
		TFunction<void(FMT2CharacterCreateResult&&)> Completion;
		if (!PendingCharacterCreations.RemoveAndCopyValue(RequestId, Completion)) return;
		FMT2CharacterCreateResult Result;
		Result.bSucceeded = Message->GetBoolField(TEXT("success"));
		Result.Error = GetStringField(Message, TEXT("error"));
		const TSharedPtr<FJsonObject>* CharacterObject = nullptr;
		if (Result.bSucceeded && Message->TryGetObjectField(TEXT("character"), CharacterObject) && CharacterObject)
		{
			ReadCharacterSummary(*CharacterObject, Result.Character);
		}
		Completion(MoveTemp(Result));
	}
	else if (Type == TEXT("character_admission_result"))
	{
		TFunction<void(FMT2CharacterAdmissionResult&&)> Completion;
		if (!PendingCharacterAdmissions.RemoveAndCopyValue(RequestId, Completion)) return;
		FMT2CharacterAdmissionResult Result;
		Result.bSucceeded = Message->GetBoolField(TEXT("success"));
		Result.Ticket = GetStringField(Message, TEXT("ticket"));
		Result.Error = GetStringField(Message, TEXT("error"));
		if (Result.bSucceeded) Result.Server = ReadServerDescriptor(Message);
		Completion(MoveTemp(Result));
	}
}

void UMT2ServerRuntimeSubsystem::RemoveCoordinatorPeer(int32 PeerIndex)
{
	if (!CoordinatorPeers.IsValidIndex(PeerIndex)) return;
	FCoordinatorPeer& Peer = *CoordinatorPeers[PeerIndex];
	const FString RemovedInstanceId = Peer.InstanceId;
	if (!Peer.InstanceId.IsEmpty()) RegisteredMapServers.Remove(Peer.InstanceId);
	if (Peer.Role == TEXT("gateway"))
	{
		for (auto It = AccountSessions.CreateIterator(); It; ++It)
		{
			if (It.Value().GatewayInstanceId == Peer.InstanceId)
			{
				SessionByAccount.Remove(It.Value().AccountId);
				It.RemoveCurrent();
			}
		}
	}
	DestroySocket(Peer.Socket);
	CoordinatorPeers.RemoveAtSwap(PeerIndex);
	if (ClusterControl.IsActive() && ClusterControl.PendingInstances.Remove(RemovedInstanceId) > 0)
	{
		if (ClusterControl.Action == EClusterControlAction::Persist && ClusterControl.Error.IsEmpty())
		{
			ClusterControl.Error = FString::Printf(
				TEXT("Server %s disconnected before persistence completed."), *RemovedInstanceId);
		}
		if (ClusterControl.PendingInstances.IsEmpty()) CompleteCoordinatorControl();
	}
}

UMT2ServerRuntimeSubsystem::FCoordinatorPeer* UMT2ServerRuntimeSubsystem::FindPeerByInstanceId(
	const FString& InstanceId) const
{
	for (const TUniquePtr<FCoordinatorPeer>& Peer : CoordinatorPeers)
	{
		if (Peer && Peer->bAuthenticated && Peer->InstanceId == InstanceId) return Peer.Get();
	}
	return nullptr;
}

void UMT2ServerRuntimeSubsystem::SendToInstance(
	const FString& InstanceId, const TSharedRef<FJsonObject>& Message)
{
	if (FCoordinatorPeer* Peer = FindPeerByInstanceId(InstanceId)) QueuePeerJson(*Peer, Message);
}

void UMT2ServerRuntimeSubsystem::RegisterMapServer(
	FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message)
{
	if (GetStringField(Message, TEXT("instance_id")) != Peer.InstanceId ||
		RegisteredMapServers.Contains(Peer.InstanceId) ||
		GetStringField(Message, TEXT("build_version")) != Peer.BuildVersion ||
		Peer.BuildVersion != GetRuntimeBuildVersion())
	{
		TSharedRef<FJsonObject> Result = MakeMessage(TEXT("register_failed"));
		Result->SetStringField(TEXT("error"),
			TEXT("Invalid instance id, duplicate registration, or build version mismatch."));
		QueuePeerJson(Peer, Result);
		return;
	}

	FMT2MapServerDescriptor Server;
	Server.InstanceId = Peer.InstanceId;
	Server.MapId = GetStringField(Message, TEXT("map_id"));
	Server.MapPath = GetStringField(Message, TEXT("map_path"));
	Server.Channel = GetIntField(Message, TEXT("channel"), 1);
	Server.PublicIp = GetStringField(Message, TEXT("public_ip"));
	Server.GamePort = GetIntField(Message, TEXT("game_port"), 11001);
	Server.MaxPlayers = GetIntField(Message, TEXT("max_players"), 1000);
	Server.State = Message->GetBoolField(TEXT("ready"))
		? EMT2MapServerState::Ready : EMT2MapServerState::LoadingWorld;
	Server.BuildVersion = GetStringField(Message, TEXT("build_version"));
	Server.LastHeartbeatSeconds = FPlatformTime::Seconds();
	if (Server.MapId.IsEmpty() || Server.PublicIp.IsEmpty() || Server.Channel < 1 || Server.GamePort < 1)
	{
		TSharedRef<FJsonObject> Result = MakeMessage(TEXT("register_failed"));
		Result->SetStringField(TEXT("error"), TEXT("Invalid map registration."));
		QueuePeerJson(Peer, Result);
		return;
	}
	RegisteredMapServers.Add(Server.InstanceId, Server);
	QueuePeerJson(Peer, MakeMessage(TEXT("registered")));
	UE_LOG(LogMT2ServerRuntime, Display, TEXT("Registered map=%s channel=%d instance=%s endpoint=%s:%d."),
		*Server.MapId, Server.Channel, *Server.InstanceId, *Server.PublicIp, Server.GamePort);
}

void UMT2ServerRuntimeSubsystem::UpdateMapServerHeartbeat(
	FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message, double NowSeconds)
{
	if (FMT2MapServerDescriptor* Server = RegisteredMapServers.Find(Peer.InstanceId))
	{
		Server->PlayerCount = FMath::Max(GetIntField(Message, TEXT("players")), 0);
		Server->State = Message->GetBoolField(TEXT("draining"))
			? EMT2MapServerState::Draining
			: Message->GetBoolField(TEXT("ready"))
				? EMT2MapServerState::Ready : EMT2MapServerState::LoadingWorld;
		Server->LastHeartbeatSeconds = NowSeconds;
		TSet<FString> OnlineCharacterIds;
		const TArray<TSharedPtr<FJsonValue>>* CharacterValues = nullptr;
		if (Message->TryGetArrayField(TEXT("characters"), CharacterValues) && CharacterValues)
		{
			for (const TSharedPtr<FJsonValue>& Value : *CharacterValues)
			{
				if (Value.IsValid()) OnlineCharacterIds.Add(Value->AsString());
			}
		}
		for (TPair<FString, FCharacterLease>& Pair : CharacterLeases)
		{
			if (Pair.Value.MapInstanceId == Peer.InstanceId && OnlineCharacterIds.Contains(Pair.Key))
			{
				Pair.Value.ExpiresAtSeconds = NowSeconds + Config.ServerTimeoutSeconds;
			}
		}
	}
}

bool UMT2ServerRuntimeSubsystem::ChooseMapServer(
	const FString& MapId, int32 PreferredChannel, FMT2MapServerDescriptor& OutServer) const
{
	const FString RequestedMapId = NormalizeMapId(MapId);
	if (RequestedMapId.IsEmpty()) return false;
	const FMT2MapServerDescriptor* Best = nullptr;
	float BestLoad = TNumericLimits<float>::Max();
	for (const TPair<FString, FMT2MapServerDescriptor>& Pair : RegisteredMapServers)
	{
		const FMT2MapServerDescriptor& Server = Pair.Value;
		if ((NormalizeMapId(Server.MapId) != RequestedMapId &&
			NormalizeMapId(Server.MapPath) != RequestedMapId) ||
			(PreferredChannel > 0 && Server.Channel != PreferredChannel) ||
			Server.State != EMT2MapServerState::Ready ||
			Server.PlayerCount >= Server.MaxPlayers) continue;
		const float Load = static_cast<float>(Server.PlayerCount) / FMath::Max(Server.MaxPlayers, 1);
		if (!Best || Load < BestLoad || (FMath::IsNearlyEqual(Load, BestLoad) && Server.Channel < Best->Channel))
		{
			Best = &Server;
			BestLoad = Load;
		}
	}
	if (!Best) return false;
	OutServer = *Best;
	return true;
}

void UMT2ServerRuntimeSubsystem::HandlePersistenceLoadRequest(
	FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message)
{
	const FString RequestId = GetStringField(Message, TEXT("request_id"));
	const FName EntityType(*GetStringField(Message, TEXT("entity_type")));
	const FString EntityId = GetStringField(Message, TEXT("entity_id"));
	const FString InstanceId = Peer.InstanceId;
	UMT2PersistenceManager* Manager = GetGameInstance()->GetSubsystem<UMT2PersistenceManager>();
	if (!Manager) return;
	TWeakObjectPtr<UMT2ServerRuntimeSubsystem> WeakThis(this);
	Manager->LoadRecord(EntityType, EntityId,
		[WeakThis, InstanceId, RequestId](FMT2PersistenceLoadResult&& Result)
		{
			if (!WeakThis.IsValid()) return;
			TSharedRef<FJsonObject> Response = MakeMessage(TEXT("persistence_load_result"));
			Response->SetStringField(TEXT("request_id"), RequestId);
			Response->SetBoolField(TEXT("success"), Result.bSucceeded);
			Response->SetBoolField(TEXT("found"), Result.bFound);
			Response->SetStringField(TEXT("error"), Result.Error);
			if (Result.bFound)
			{
				Response->SetStringField(TEXT("entity_type"), Result.Record.EntityType.ToString());
				Response->SetStringField(TEXT("entity_id"), Result.Record.EntityId);
				Response->SetStringField(TEXT("owner_id"), Result.Record.OwnerId);
				Response->SetNumberField(TEXT("schema_version"), Result.Record.SchemaVersion);
				Response->SetStringField(TEXT("revision"), LexToString(Result.Record.Revision));
				Response->SetBoolField(TEXT("has_world_position"), Result.Record.bHasWorldPosition);
				Response->SetStringField(TEXT("payload"), Result.Record.PayloadJson);
			}
			WeakThis->SendToInstance(InstanceId, Response);
		});
}

void UMT2ServerRuntimeSubsystem::HandlePersistenceSaveRequest(
	FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message)
{
	const FString RequestId = GetStringField(Message, TEXT("request_id"));
	FMT2PersistentRecord Record;
	Record.EntityType = FName(*GetStringField(Message, TEXT("entity_type")));
	Record.EntityId = GetStringField(Message, TEXT("entity_id"));
	Record.OwnerId = GetStringField(Message, TEXT("owner_id"));
	Record.SchemaVersion = GetIntField(Message, TEXT("schema_version"), 1);
	Record.Revision = FCString::Atoi64(*GetStringField(Message, TEXT("revision")));
	Record.PayloadJson = GetStringField(Message, TEXT("payload"));
	const FString InstanceId = Peer.InstanceId;
	UMT2PersistenceManager* Manager = GetGameInstance()->GetSubsystem<UMT2PersistenceManager>();
	if (!Manager) return;
	TWeakObjectPtr<UMT2ServerRuntimeSubsystem> WeakThis(this);
	Manager->SaveRecord(Record,
		[WeakThis, InstanceId, RequestId](FMT2PersistenceSaveResult&& Result)
		{
			if (!WeakThis.IsValid()) return;
			TSharedRef<FJsonObject> Response = MakeMessage(TEXT("persistence_save_result"));
			Response->SetStringField(TEXT("request_id"), RequestId);
			Response->SetBoolField(TEXT("success"), Result.bSucceeded);
			Response->SetStringField(TEXT("revision"), LexToString(Result.Revision));
			Response->SetStringField(TEXT("error"), Result.Error);
			WeakThis->SendToInstance(InstanceId, Response);
		});
}

void UMT2ServerRuntimeSubsystem::HandleRouteRequest(
	FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message)
{
	TSharedRef<FJsonObject> Response = MakeMessage(TEXT("route_result"));
	Response->SetStringField(TEXT("request_id"), GetStringField(Message, TEXT("request_id")));
	FMT2MapServerDescriptor Server;
	if (ChooseMapServer(GetStringField(Message, TEXT("map_id")), GetIntField(Message, TEXT("channel")), Server))
	{
		Response->SetBoolField(TEXT("success"), true);
		WriteServerDescriptor(Server, Response);
	}
	else
	{
		Response->SetBoolField(TEXT("success"), false);
		Response->SetStringField(TEXT("error"), TEXT("No available server for requested map/channel."));
	}
	QueuePeerJson(Peer, Response);
}

void UMT2ServerRuntimeSubsystem::HandleTransferIssueRequest(
	FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message, double NowSeconds)
{
	TSharedRef<FJsonObject> Response = MakeMessage(TEXT("transfer_ticket_result"));
	Response->SetStringField(TEXT("request_id"), GetStringField(Message, TEXT("request_id")));
	FMT2MapServerDescriptor Destination;
	const FString CharacterId = GetStringField(Message, TEXT("character_id"));
	const FString AccountId = GetStringField(Message, TEXT("account_id"));
	const FCharacterLease* Lease = CharacterLeases.Find(CharacterId);
	const FString DestinationMapId = GetStringField(Message, TEXT("map_id"));
	const int32 PreferredChannel = GetIntField(Message, TEXT("channel"));
	const bool bHasDestination = ChooseMapServer(DestinationMapId, PreferredChannel, Destination) ||
		(PreferredChannel > 0 && ChooseMapServer(DestinationMapId, 0, Destination));
	if (CharacterId.IsEmpty() || AccountId.IsEmpty() || !Lease || Lease->AccountId != AccountId ||
		Lease->MapInstanceId != Peer.InstanceId || NowSeconds >= Lease->ExpiresAtSeconds || !bHasDestination)
	{
		UE_LOG(LogMT2ServerRuntime, Warning,
			TEXT("Transfer rejected: map='%s' preferred_channel=%d source='%s' character='%s' destination=%s."),
			*DestinationMapId, PreferredChannel, *Peer.InstanceId, *CharacterId,
			bHasDestination ? TEXT("available") : TEXT("unavailable"));
		Response->SetBoolField(TEXT("success"), false);
		Response->SetStringField(TEXT("error"), TEXT("Transfer destination unavailable or character identity invalid."));
		QueuePeerJson(Peer, Response);
		return;
	}

	FTransferTicket Ticket;
	Ticket.Ticket = MT2Authentication::GenerateSecureToken();
	Ticket.CharacterId = CharacterId;
	Ticket.AccountId = AccountId;
	Ticket.SourceInstanceId = Peer.InstanceId;
	Ticket.DestinationInstanceId = Destination.InstanceId;
	Ticket.DestinationMapId = Destination.MapId;
	Ticket.DestinationChannel = Destination.Channel;
	Ticket.bForceTownSpawn = Message->GetBoolField(TEXT("force_town_spawn"));
	Ticket.ExpiresAtSeconds = NowSeconds + Config.TransferTicketLifetimeSeconds;
	TransferTickets.Add(Ticket.Ticket, Ticket);
	UE_LOG(LogMT2ServerRuntime, Display,
		TEXT("Transfer issued: map='%s' channel=%d source='%s' destination='%s' character='%s'."),
		*Destination.MapId, Destination.Channel, *Peer.InstanceId, *Destination.InstanceId, *CharacterId);
	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("ticket"), Ticket.Ticket);
	WriteServerDescriptor(Destination, Response);
	QueuePeerJson(Peer, Response);
}

void UMT2ServerRuntimeSubsystem::HandleTransferClaimRequest(
	FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message, double NowSeconds)
{
	TSharedRef<FJsonObject> Response = MakeMessage(TEXT("transfer_claim_result"));
	Response->SetStringField(TEXT("request_id"), GetStringField(Message, TEXT("request_id")));
	const FString TicketValue = GetStringField(Message, TEXT("ticket"));
	FTransferTicket* Ticket = TransferTickets.Find(TicketValue);
	if (!Ticket || Ticket->DestinationInstanceId != Peer.InstanceId || NowSeconds >= Ticket->ExpiresAtSeconds ||
		Ticket->State != ETransferState::Issued)
	{
		Response->SetBoolField(TEXT("success"), false);
		Response->SetStringField(TEXT("error"), TEXT("Transfer ticket is invalid, expired, or belongs to another server."));
		QueuePeerJson(Peer, Response);
		return;
	}
	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("character_id"), Ticket->CharacterId);
	Response->SetStringField(TEXT("account_id"), Ticket->AccountId);
	Response->SetStringField(TEXT("source_instance_id"), Ticket->SourceInstanceId);
	Response->SetBoolField(TEXT("force_town_spawn"), Ticket->bForceTownSpawn);
	if (!Ticket->Character.CharacterId.IsEmpty())
	{
		Response->SetObjectField(TEXT("character"), WriteCharacterSummary(Ticket->Character));
	}
	Ticket->State = ETransferState::Claimed;
	FCharacterLease& Lease = CharacterLeases.FindOrAdd(Ticket->CharacterId);
	Lease.CharacterId = Ticket->CharacterId;
	Lease.AccountId = Ticket->AccountId;
	Lease.MapInstanceId = Ticket->DestinationInstanceId;
	Lease.ExpiresAtSeconds = NowSeconds + Config.ServerTimeoutSeconds;
	QueuePeerJson(Peer, Response);
}

void UMT2ServerRuntimeSubsystem::HandleTransferCompleteRequest(
	FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message)
{
	TSharedRef<FJsonObject> Response = MakeMessage(TEXT("transfer_complete_result"));
	Response->SetStringField(TEXT("request_id"), GetStringField(Message, TEXT("request_id")));
	const FString TicketValue = GetStringField(Message, TEXT("ticket"));
	FTransferTicket* Ticket = TransferTickets.Find(TicketValue);
	if (!Ticket || Ticket->DestinationInstanceId != Peer.InstanceId || Ticket->State != ETransferState::Claimed)
	{
		Response->SetBoolField(TEXT("success"), false);
		Response->SetStringField(TEXT("error"), TEXT("Transfer completion is invalid."));
		QueuePeerJson(Peer, Response);
		return;
	}
	const bool bSucceeded = Message->GetBoolField(TEXT("success"));
	if (!bSucceeded)
	{
		Ticket->State = ETransferState::Issued;
		if (FCharacterLease* Lease = CharacterLeases.Find(Ticket->CharacterId))
		{
			Lease->MapInstanceId = Ticket->SourceInstanceId;
			Lease->ExpiresAtSeconds = FPlatformTime::Seconds() + Config.ServerTimeoutSeconds;
		}
		Response->SetBoolField(TEXT("success"), true);
		QueuePeerJson(Peer, Response);
		return;
	}

	const FString InstanceId = Peer.InstanceId;
	const FString RequestId = GetStringField(Message, TEXT("request_id"));
	const FString CharacterId = Ticket->CharacterId;
	const FString DestinationMapId = Ticket->DestinationMapId;
	const int32 DestinationChannel = Ticket->DestinationChannel;
	Ticket->State = ETransferState::Completing;
	UMT2PersistenceManager* Manager = GetGameInstance()->GetSubsystem<UMT2PersistenceManager>();
	if (!Manager)
	{
		Ticket->State = ETransferState::Claimed;
		Response->SetBoolField(TEXT("success"), false);
		Response->SetStringField(TEXT("error"), TEXT("Persistence manager is unavailable."));
		QueuePeerJson(Peer, Response);
		return;
	}
	TWeakObjectPtr<UMT2ServerRuntimeSubsystem> WeakThis(this);
	Manager->UpdateCharacterLocation(
		CharacterId, DestinationMapId, DestinationChannel,
		[WeakThis, InstanceId, RequestId, TicketValue](bool bUpdated, FString&& Error)
		{
			if (!WeakThis.IsValid()) return;
			if (FTransferTicket* ActiveTicket = WeakThis->TransferTickets.Find(TicketValue))
			{
				if (bUpdated)
				{
					const FString CompletedCharacterId = ActiveTicket->CharacterId;
					const FString CompletedMapId = ActiveTicket->DestinationMapId;
					const int32 CompletedChannel = ActiveTicket->DestinationChannel;
					if (FCharacterLease* Lease = WeakThis->CharacterLeases.Find(ActiveTicket->CharacterId))
					{
						Lease->ExpiresAtSeconds = FPlatformTime::Seconds() + WeakThis->Config.ServerTimeoutSeconds;
					}
					for (TPair<FString, FCoordinatorParty>& Pair : WeakThis->CoordinatorParties)
					{
						if (FMT2PartyMemberData* Member = Pair.Value.Members.FindByPredicate(
							[&](const FMT2PartyMemberData& Data)
							{
								return Data.CharacterId == CompletedCharacterId;
							}))
						{
							Member->MapId = CompletedMapId;
							Member->Channel = CompletedChannel;
							Member->bHasWorldLocation = false;
							break;
						}
					}
					WeakThis->TransferTickets.Remove(TicketValue);
					WeakThis->BroadcastPartyForCharacter(CompletedCharacterId);
				}
				else ActiveTicket->State = ETransferState::Claimed;
			}
			TSharedRef<FJsonObject> Result = MakeMessage(TEXT("transfer_complete_result"));
			Result->SetStringField(TEXT("request_id"), RequestId);
			Result->SetBoolField(TEXT("success"), bUpdated);
			Result->SetStringField(TEXT("error"), Error);
			WeakThis->SendToInstance(InstanceId, Result);
		});
}

void UMT2ServerRuntimeSubsystem::HandleAccountRegistrationRequest(
	FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message)
{
	const FString InstanceId = Peer.InstanceId;
	const FString RequestId = GetStringField(Message, TEXT("request_id"));
	if (!Config.bAllowAccountRegistration)
	{
		TSharedRef<FJsonObject> Response = MakeMessage(TEXT("account_register_result"));
		Response->SetStringField(TEXT("request_id"), RequestId);
		Response->SetBoolField(TEXT("success"), false);
		Response->SetStringField(TEXT("error"), TEXT("Account registration is disabled."));
		QueuePeerJson(Peer, Response);
		return;
	}
	UMT2PersistenceManager* Manager = GetGameInstance()->GetSubsystem<UMT2PersistenceManager>();
	if (!Manager)
	{
		TSharedRef<FJsonObject> Response = MakeMessage(TEXT("account_register_result"));
		Response->SetStringField(TEXT("request_id"), RequestId);
		Response->SetBoolField(TEXT("success"), false);
		Response->SetStringField(TEXT("error"), TEXT("Persistence manager is unavailable."));
		QueuePeerJson(Peer, Response);
		return;
	}
	TWeakObjectPtr<UMT2ServerRuntimeSubsystem> WeakThis(this);
	Manager->RegisterAccount(
		GetStringField(Message, TEXT("username")), GetStringField(Message, TEXT("password")),
		[WeakThis, InstanceId, RequestId](FMT2AccountRegistrationResult&& Result)
		{
			if (!WeakThis.IsValid()) return;
			TSharedRef<FJsonObject> Response = MakeMessage(TEXT("account_register_result"));
			Response->SetStringField(TEXT("request_id"), RequestId);
			Response->SetBoolField(TEXT("success"), Result.bSucceeded);
			Response->SetStringField(TEXT("account_id"), Result.AccountId);
			Response->SetStringField(TEXT("error"), Result.Error);
			WeakThis->SendToInstance(InstanceId, Response);
		});
}

void UMT2ServerRuntimeSubsystem::HandleAccountLoginRequest(
	FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message)
{
	const FString InstanceId = Peer.InstanceId;
	const FString RequestId = GetStringField(Message, TEXT("request_id"));
	const FString ExistingSessionToken = GetStringField(Message, TEXT("existing_session_token"));
	UMT2PersistenceManager* Manager = GetGameInstance()->GetSubsystem<UMT2PersistenceManager>();
	if (!Manager)
	{
		TSharedRef<FJsonObject> Response = MakeMessage(TEXT("account_login_result"));
		Response->SetStringField(TEXT("request_id"), RequestId);
		Response->SetBoolField(TEXT("success"), false);
		Response->SetStringField(TEXT("error"), TEXT("Persistence manager is unavailable."));
		QueuePeerJson(Peer, Response);
		return;
	}
	TWeakObjectPtr<UMT2ServerRuntimeSubsystem> WeakThis(this);
	Manager->AuthenticateAccount(
		GetStringField(Message, TEXT("username")), GetStringField(Message, TEXT("password")),
		[WeakThis, InstanceId, RequestId, ExistingSessionToken](FMT2AccountLoginResult&& Result)
		{
			if (!WeakThis.IsValid()) return;
			if (!WeakThis->FindPeerByInstanceId(InstanceId)) return;
			if (Result.bSucceeded)
			{
				const FString* OldToken = WeakThis->SessionByAccount.Find(Result.AccountId);
				bool bHasOnlineCharacter = false;
				const double NowSeconds = FPlatformTime::Seconds();
				for (const TPair<FString, FCharacterLease>& Pair : WeakThis->CharacterLeases)
				{
					if (Pair.Value.AccountId == Result.AccountId && NowSeconds < Pair.Value.ExpiresAtSeconds)
					{
						bHasOnlineCharacter = true;
						break;
					}
				}
				Result.bReplacedExistingSession =
					(OldToken && *OldToken != ExistingSessionToken) || bHasOnlineCharacter;
				WeakThis->RevokeAccountSessions(Result.AccountId, ExistingSessionToken);
				Result.SessionToken = MT2Authentication::GenerateSecureToken();
				FAccountSession Session;
				Session.Token = Result.SessionToken;
				Session.AccountId = Result.AccountId;
				Session.GatewayInstanceId = InstanceId;
				Session.ExpiresAtSeconds = FPlatformTime::Seconds() + 86400.0;
				WeakThis->AccountSessions.Add(Session.Token, Session);
				WeakThis->SessionByAccount.Add(Session.AccountId, Session.Token);
			}
			TSharedRef<FJsonObject> Response = MakeMessage(TEXT("account_login_result"));
			Response->SetStringField(TEXT("request_id"), RequestId);
			Response->SetBoolField(TEXT("success"), Result.bSucceeded);
			Response->SetBoolField(TEXT("replaced_existing_session"), Result.bReplacedExistingSession);
			Response->SetStringField(TEXT("account_id"), Result.AccountId);
			Response->SetStringField(TEXT("session_token"), Result.SessionToken);
			Response->SetStringField(TEXT("error"), Result.bReplacedExistingSession
				? TEXT("Account already logged in; previous session disconnected.") : Result.Error);
			TArray<TSharedPtr<FJsonValue>> Characters;
			for (const FMT2CharacterSummary& Character : Result.Characters)
			{
				Characters.Add(MakeShared<FJsonValueObject>(WriteCharacterSummary(Character)));
			}
			Response->SetArrayField(TEXT("characters"), Characters);
			WeakThis->SendToInstance(InstanceId, Response);
		});
}

void UMT2ServerRuntimeSubsystem::HandleCharacterCreationRequest(
	FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message)
{
	const FString RequestId = GetStringField(Message, TEXT("request_id"));
	FAccountSession* Session = FindValidSession(
		GetStringField(Message, TEXT("session_token")), Peer.InstanceId);
	if (!Session)
	{
		TSharedRef<FJsonObject> Response = MakeMessage(TEXT("character_create_result"));
		Response->SetStringField(TEXT("request_id"), RequestId);
		Response->SetBoolField(TEXT("success"), false);
		Response->SetStringField(TEXT("error"), TEXT("Login session is invalid or expired."));
		QueuePeerJson(Peer, Response);
		return;
	}
	FMT2CharacterAppearance Appearance;
	Appearance.Race = static_cast<EMT2CharacterRace>(GetIntField(Message, TEXT("race")));
	Appearance.Sex = static_cast<EMT2CharacterSex>(GetIntField(Message, TEXT("sex")));
	Appearance.Style = static_cast<EMT2CharacterStyle>(GetIntField(Message, TEXT("style")));
	const EMT2Empire Empire = static_cast<EMT2Empire>(GetIntField(Message, TEXT("empire")));
	const FString InstanceId = Peer.InstanceId;
	const FString AccountId = Session->AccountId;
	UMT2PersistenceManager* Manager = GetGameInstance()->GetSubsystem<UMT2PersistenceManager>();
	if (!Manager) return;
	TWeakObjectPtr<UMT2ServerRuntimeSubsystem> WeakThis(this);
	Manager->CreateCharacter(
		AccountId, GetStringField(Message, TEXT("character_name")), Appearance,
		Empire, GetEmpireStartMap(Empire),
		[WeakThis, InstanceId, RequestId](FMT2CharacterCreateResult&& Result)
		{
			if (!WeakThis.IsValid()) return;
			TSharedRef<FJsonObject> Response = MakeMessage(TEXT("character_create_result"));
			Response->SetStringField(TEXT("request_id"), RequestId);
			Response->SetBoolField(TEXT("success"), Result.bSucceeded);
			Response->SetStringField(TEXT("error"), Result.Error);
			if (Result.bSucceeded) Response->SetObjectField(TEXT("character"), WriteCharacterSummary(Result.Character));
			WeakThis->SendToInstance(InstanceId, Response);
		});
}

void UMT2ServerRuntimeSubsystem::HandleCharacterAdmissionRequest(
	FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message, double NowSeconds)
{
	const FString RequestId = GetStringField(Message, TEXT("request_id"));
	FAccountSession* Session = FindValidSession(
		GetStringField(Message, TEXT("session_token")), Peer.InstanceId);
	if (!Session)
	{
		TSharedRef<FJsonObject> Response = MakeMessage(TEXT("character_admission_result"));
		Response->SetStringField(TEXT("request_id"), RequestId);
		Response->SetBoolField(TEXT("success"), false);
		Response->SetStringField(TEXT("error"), TEXT("Login session is invalid or expired."));
		QueuePeerJson(Peer, Response);
		return;
	}
	const FString AccountId = Session->AccountId;
	const FString SessionToken = Session->Token;
	const FString InstanceId = Peer.InstanceId;
	const FString CharacterId = GetStringField(Message, TEXT("character_id"));
	const int32 PreferredChannel = GetIntField(Message, TEXT("channel"));
	UMT2PersistenceManager* Manager = GetGameInstance()->GetSubsystem<UMT2PersistenceManager>();
	if (!Manager) return;
	TWeakObjectPtr<UMT2ServerRuntimeSubsystem> WeakThis(this);
	Manager->FindCharacter(AccountId, CharacterId,
		[WeakThis, InstanceId, RequestId, AccountId, SessionToken, PreferredChannel, NowSeconds]
		(FMT2CharacterCreateResult&& Found)
		{
			if (!WeakThis.IsValid()) return;
			TSharedRef<FJsonObject> Response = MakeMessage(TEXT("character_admission_result"));
			Response->SetStringField(TEXT("request_id"), RequestId);
			FMT2MapServerDescriptor Destination;
			if (Found.bSucceeded && WeakThis->HasActiveCharacterReservation(
				Found.Character.CharacterId, FPlatformTime::Seconds()))
			{
				Response->SetBoolField(TEXT("success"), false);
				Response->SetStringField(TEXT("error"), TEXT("Character is already online or joining a server."));
				WeakThis->SendToInstance(InstanceId, Response);
				return;
			}
			if (!Found.bSucceeded || !WeakThis->ChooseMapServer(
				Found.Character.MapId, PreferredChannel > 0 ? PreferredChannel : Found.Character.Channel, Destination))
			{
				Response->SetBoolField(TEXT("success"), false);
				Response->SetStringField(TEXT("error"), Found.bSucceeded
					? TEXT("Character map server is unavailable.") : Found.Error);
				WeakThis->SendToInstance(InstanceId, Response);
				return;
			}
			FTransferTicket Ticket;
			Ticket.Ticket = MT2Authentication::GenerateSecureToken();
			Ticket.CharacterId = Found.Character.CharacterId;
			Ticket.AccountId = AccountId;
			Ticket.SourceInstanceId = InstanceId;
			Ticket.DestinationInstanceId = Destination.InstanceId;
			Ticket.DestinationMapId = Destination.MapId;
			Ticket.DestinationChannel = Destination.Channel;
			Ticket.SessionToken = SessionToken;
			Ticket.Character = Found.Character;
			Ticket.ExpiresAtSeconds = NowSeconds + WeakThis->Config.TransferTicketLifetimeSeconds;
			WeakThis->TransferTickets.Add(Ticket.Ticket, Ticket);
			Response->SetBoolField(TEXT("success"), true);
			Response->SetStringField(TEXT("ticket"), Ticket.Ticket);
			WriteServerDescriptor(Destination, Response);
			WeakThis->SendToInstance(InstanceId, Response);
		});
}

void UMT2ServerRuntimeSubsystem::HandleCharacterLogout(
	FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message)
{
	const FString CharacterId = GetStringField(Message, TEXT("character_id"));
	const FString AccountId = GetStringField(Message, TEXT("account_id"));
	// Companions see the lamp go out before the session record disappears.
	MessengerDropPresence(CharacterId);
	if (FCharacterLease* Lease = CharacterLeases.Find(CharacterId);
		Lease && Lease->AccountId == AccountId && Lease->MapInstanceId == Peer.InstanceId)
	{
		CharacterLeases.Remove(CharacterId);
	}
}

void UMT2ServerRuntimeSubsystem::HandlePartyUpdate(
	FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message)
{
	const FString PartyId = GetStringField(Message, TEXT("party_id"));
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (PartyId.IsEmpty() || !Message->TryGetArrayField(TEXT("members"), Values) || !Values)
	{
		return;
	}

	FCoordinatorParty Updated;
	Updated.PartyId = PartyId;
	TSet<FString> CharacterIds;
	int32 LeaderCount = 0;
	for (const TSharedPtr<FJsonValue>& Value : *Values)
	{
		FMT2PartyMemberData Member;
		if (!Value.IsValid() || !ReadPartyMember(Value->AsObject(), Member)
			|| Member.CharacterId.IsEmpty() || CharacterIds.Contains(Member.CharacterId))
		{
			return;
		}
		CharacterIds.Add(Member.CharacterId);
		LeaderCount += Member.bLeader ? 1 : 0;
		Member.PlayerState = nullptr;
		Updated.Members.Add(MoveTemp(Member));
	}
	// A trusted map server may only update a party containing a character leased to that server.
	bool bOwnsMember = Updated.Members.ContainsByPredicate([&](const FMT2PartyMemberData& Member)
	{
		const FCharacterLease* Lease = CharacterLeases.Find(Member.CharacterId);
		return Lease && Lease->MapInstanceId == Peer.InstanceId;
	});
	// A member may be the only player from this party on the requesting map and therefore no longer
	// appear in a leave/kick update. In that case authorize against the previous snapshot.
	if (!bOwnsMember)
	{
		if (const FCoordinatorParty* Existing = CoordinatorParties.Find(PartyId))
		{
			bOwnsMember = Existing->Members.ContainsByPredicate(
				[&](const FMT2PartyMemberData& Member)
				{
					const FCharacterLease* Lease = CharacterLeases.Find(Member.CharacterId);
					return Lease && Lease->MapInstanceId == Peer.InstanceId;
				});
		}
	}
	if (!bOwnsMember) return;
	const int32 MaximumMembers = FMath::Clamp(
		UMT2GameplaySettings::Get().PartyMaximumMembers, 2, 8);
	if (Updated.Members.Num() < 2 || Updated.Members.Num() > MaximumMembers || LeaderCount != 1)
	{
		BroadcastPartyDisband(PartyId);
		CoordinatorParties.Remove(PartyId);
		return;
	}

	CoordinatorParties.Add(PartyId, Updated);
	BroadcastPartySnapshot(Updated);
}

void UMT2ServerRuntimeSubsystem::HandlePartyDisband(
	FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message)
{
	const FString PartyId = GetStringField(Message, TEXT("party_id"));
	const FCoordinatorParty* Party = CoordinatorParties.Find(PartyId);
	if (!Party) return;
	const bool bOwnsMember = Party->Members.ContainsByPredicate([&](const FMT2PartyMemberData& Member)
	{
		const FCharacterLease* Lease = CharacterLeases.Find(Member.CharacterId);
		return Lease && Lease->MapInstanceId == Peer.InstanceId;
	});
	if (!bOwnsMember) return;
	CoordinatorParties.Remove(PartyId);
	BroadcastPartyDisband(PartyId);
}

void UMT2ServerRuntimeSubsystem::BroadcastPartySnapshot(const FCoordinatorParty& Party)
{
	TSharedRef<FJsonObject> Message = MakeMessage(TEXT("party_snapshot"));
	Message->SetStringField(TEXT("party_id"), Party.PartyId);
	TArray<TSharedPtr<FJsonValue>> Values;
	Values.Reserve(Party.Members.Num());
	for (const FMT2PartyMemberData& Member : Party.Members)
	{
		Values.Add(MakeShared<FJsonValueObject>(WritePartyMember(Member)));
	}
	Message->SetArrayField(TEXT("members"), MoveTemp(Values));
	for (const TUniquePtr<FCoordinatorPeer>& Target : CoordinatorPeers)
	{
		if (Target && Target->bAuthenticated && Target->Role == TEXT("map"))
		{
			QueuePeerJson(*Target, Message);
		}
	}
}

void UMT2ServerRuntimeSubsystem::BroadcastPartyDisband(const FString& PartyId)
{
	if (PartyId.IsEmpty()) return;
	TSharedRef<FJsonObject> Message = MakeMessage(TEXT("party_disband"));
	Message->SetStringField(TEXT("party_id"), PartyId);
	for (const TUniquePtr<FCoordinatorPeer>& Target : CoordinatorPeers)
	{
		if (Target && Target->bAuthenticated && Target->Role == TEXT("map"))
		{
			QueuePeerJson(*Target, Message);
		}
	}
}

void UMT2ServerRuntimeSubsystem::BroadcastPartyForCharacter(const FString& CharacterId)
{
	for (const TPair<FString, FCoordinatorParty>& Pair : CoordinatorParties)
	{
		if (Pair.Value.Members.ContainsByPredicate([&](const FMT2PartyMemberData& Member)
		{
			return Member.CharacterId == CharacterId;
		}))
		{
			BroadcastPartySnapshot(Pair.Value);
			return;
		}
	}
}

void UMT2ServerRuntimeSubsystem::ApplyPartySnapshotToWorld(
	const FString& PartyId, const TArray<FMT2PartyMemberData>& Members)
{
	UWorld* World = GetWorld();
	if (!Config.IsMapServer() || !World || PartyId.IsEmpty() || Members.Num() < 2) return;

	TArray<AMT2PlayerState*> LocalMembers;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		AMT2PlayerState* State = It->Get()->GetPlayerState<AMT2PlayerState>();
		const UMT2PersistenceComponent* Persistence = State ? State->GetPersistenceComponent() : nullptr;
		const FString CharacterId = Persistence ? Persistence->GetEntityId() : FString();
		if (State && !CharacterId.IsEmpty() && Members.ContainsByPredicate(
			[&](const FMT2PartyMemberData& Member) { return Member.CharacterId == CharacterId; }))
		{
			LocalMembers.Add(State);
		}
	}

	AMT2Party* Proxy = nullptr;
	for (TActorIterator<AMT2Party> It(World); It; ++It)
	{
		if (It->GetPartyId() == PartyId)
		{
			Proxy = *It;
			break;
		}
	}
	if (LocalMembers.IsEmpty())
	{
		if (Proxy) Proxy->Disband(false);
		return;
	}
	if (!Proxy)
	{
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Proxy = World->SpawnActor<AMT2Party>(AMT2Party::StaticClass(), FTransform::Identity, Parameters);
	}
	if (Proxy)
	{
		for (AMT2PlayerState* State : LocalMembers)
		{
			if (AMT2Party* OldParty = State->GetParty(); OldParty && OldParty != Proxy)
			{
				OldParty->Disband(false);
			}
		}
		Proxy->ApplyCoordinatorSnapshot(PartyId, Members, LocalMembers);
	}
}

void UMT2ServerRuntimeSubsystem::ApplyPartyDisbandToWorld(const FString& PartyId)
{
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AMT2Party> It(World); It; ++It)
		{
			if (It->GetPartyId() == PartyId)
			{
				It->Disband(false);
				return;
			}
		}
	}
}

bool UMT2ServerRuntimeSubsystem::HasActiveCharacterReservation(
	const FString& CharacterId, double NowSeconds) const
{
	if (const FCharacterLease* Lease = CharacterLeases.Find(CharacterId);
		Lease && NowSeconds < Lease->ExpiresAtSeconds)
	{
		return true;
	}
	for (const TPair<FString, FTransferTicket>& Pair : TransferTickets)
	{
		if (Pair.Value.CharacterId == CharacterId && NowSeconds < Pair.Value.ExpiresAtSeconds)
		{
			return true;
		}
	}
	return false;
}

void UMT2ServerRuntimeSubsystem::RevokeAccountSessions(
	const FString& AccountId, const FString& PreservedSessionToken)
{
	const FString Reason = TEXT("Disconnected by another login instance.");
	if (const FString* OldToken = SessionByAccount.Find(AccountId))
	{
		if (*OldToken != PreservedSessionToken)
		{
			if (const FAccountSession* OldSession = AccountSessions.Find(*OldToken))
			{
				TSharedRef<FJsonObject> Revoke = MakeMessage(TEXT("account_session_revoked"));
				Revoke->SetStringField(TEXT("session_token"), *OldToken);
				Revoke->SetStringField(TEXT("account_id"), AccountId);
				Revoke->SetStringField(TEXT("reason"), Reason);
				SendToInstance(OldSession->GatewayInstanceId, Revoke);
			}
		}
		AccountSessions.Remove(*OldToken);
		SessionByAccount.Remove(AccountId);
	}

	for (auto It = CharacterLeases.CreateIterator(); It; ++It)
	{
		if (It.Value().AccountId == AccountId)
		{
			TSharedRef<FJsonObject> Revoke = MakeMessage(TEXT("account_session_revoked"));
			Revoke->SetStringField(TEXT("account_id"), AccountId);
			Revoke->SetStringField(TEXT("reason"), Reason);
			SendToInstance(It.Value().MapInstanceId, Revoke);
			It.RemoveCurrent();
		}
	}
	for (auto It = TransferTickets.CreateIterator(); It; ++It)
	{
		if (It.Value().AccountId == AccountId)
		{
			It.RemoveCurrent();
		}
	}
}

UMT2ServerRuntimeSubsystem::FAccountSession* UMT2ServerRuntimeSubsystem::FindValidSession(
	const FString& SessionToken, const FString& GatewayInstanceId)
{
	FAccountSession* Session = AccountSessions.Find(SessionToken);
	if (!Session || Session->GatewayInstanceId != GatewayInstanceId ||
		FPlatformTime::Seconds() >= Session->ExpiresAtSeconds)
	{
		return nullptr;
	}
	return Session;
}

void UMT2ServerRuntimeSubsystem::RequestPersistenceLoad(
	FName EntityType, const FString& EntityId,
	TFunction<void(FMT2PersistenceLoadResult&&)> Completion)
{
	if (!bCoordinatorAuthenticated)
	{
		FMT2PersistenceLoadResult Result; Result.Error = TEXT("Coordinator is not connected."); Completion(MoveTemp(Result)); return;
	}
	const FString RequestId = NextRequestId();
	PendingLoads.Add(RequestId, MoveTemp(Completion));
	TrackPendingRequest(RequestId);
	TSharedRef<FJsonObject> Message = MakeMessage(TEXT("persistence_load"));
	Message->SetStringField(TEXT("request_id"), RequestId);
	Message->SetStringField(TEXT("entity_type"), EntityType.ToString());
	Message->SetStringField(TEXT("entity_id"), EntityId);
	QueueMapJson(Message);
}

void UMT2ServerRuntimeSubsystem::RequestPersistenceSave(
	const FMT2PersistentRecord& Record,
	TFunction<void(FMT2PersistenceSaveResult&&)> Completion)
{
	if (!bCoordinatorAuthenticated)
	{
		FMT2PersistenceSaveResult Result; Result.Error = TEXT("Coordinator is not connected."); Completion(MoveTemp(Result)); return;
	}
	const FString RequestId = NextRequestId();
	PendingSaves.Add(RequestId, MoveTemp(Completion));
	TrackPendingRequest(RequestId);
	TSharedRef<FJsonObject> Message = MakeMessage(TEXT("persistence_save"));
	Message->SetStringField(TEXT("request_id"), RequestId);
	Message->SetStringField(TEXT("entity_type"), Record.EntityType.ToString());
	Message->SetStringField(TEXT("entity_id"), Record.EntityId);
	Message->SetStringField(TEXT("owner_id"), Record.OwnerId);
	Message->SetNumberField(TEXT("schema_version"), Record.SchemaVersion);
	Message->SetStringField(TEXT("revision"), LexToString(Record.Revision));
	Message->SetStringField(TEXT("payload"), Record.PayloadJson);
	QueueMapJson(Message);
}

void UMT2ServerRuntimeSubsystem::RequestMapRoute(
	const FString& MapId, int32 PreferredChannel,
	TFunction<void(FMT2MapRouteResult&&)> Completion)
{
	if (!bCoordinatorAuthenticated)
	{
		FMT2MapRouteResult Result; Result.Error = TEXT("Coordinator is not connected."); Completion(MoveTemp(Result)); return;
	}
	const FString RequestId = NextRequestId();
	PendingRoutes.Add(RequestId, MoveTemp(Completion));
	TrackPendingRequest(RequestId);
	TSharedRef<FJsonObject> Message = MakeMessage(TEXT("route_request"));
	Message->SetStringField(TEXT("request_id"), RequestId);
	Message->SetStringField(TEXT("map_id"), MapId);
	Message->SetNumberField(TEXT("channel"), PreferredChannel);
	QueueMapJson(Message);
}

void UMT2ServerRuntimeSubsystem::RequestTransferTicket(
	const FString& CharacterId, const FString& AccountId, const FString& DestinationMapId,
	int32 PreferredChannel, bool bForceTownSpawn,
	TFunction<void(FMT2TransferTicketResult&&)> Completion)
{
	if (!bCoordinatorAuthenticated)
	{
		FMT2TransferTicketResult Result; Result.Error = TEXT("Coordinator is not connected."); Completion(MoveTemp(Result)); return;
	}
	const FString RequestId = NextRequestId();
	PendingTransferTickets.Add(RequestId, MoveTemp(Completion));
	TrackPendingRequest(RequestId);
	TSharedRef<FJsonObject> Message = MakeMessage(TEXT("transfer_issue"));
	Message->SetStringField(TEXT("request_id"), RequestId);
	Message->SetStringField(TEXT("character_id"), CharacterId);
	Message->SetStringField(TEXT("account_id"), AccountId);
	Message->SetStringField(TEXT("map_id"), DestinationMapId);
	Message->SetNumberField(TEXT("channel"), PreferredChannel);
	Message->SetBoolField(TEXT("force_town_spawn"), bForceTownSpawn);
	QueueMapJson(Message);
}

void UMT2ServerRuntimeSubsystem::ClaimTransferTicket(
	const FString& Ticket, TFunction<void(FMT2TransferClaimResult&&)> Completion)
{
	if (!bCoordinatorAuthenticated)
	{
		FMT2TransferClaimResult Result; Result.Error = TEXT("Coordinator is not connected."); Completion(MoveTemp(Result)); return;
	}
	const FString RequestId = NextRequestId();
	PendingTransferClaims.Add(RequestId, MoveTemp(Completion));
	TrackPendingRequest(RequestId);
	TSharedRef<FJsonObject> Message = MakeMessage(TEXT("transfer_claim"));
	Message->SetStringField(TEXT("request_id"), RequestId);
	Message->SetStringField(TEXT("ticket"), Ticket);
	QueueMapJson(Message);
}

void UMT2ServerRuntimeSubsystem::CompleteTransferTicket(
	const FString& Ticket, bool bSucceeded,
	TFunction<void(FMT2TransferCompleteResult&&)> Completion)
{
	if (!Config.IsMapServer() || !bCoordinatorAuthenticated)
	{
		FMT2TransferCompleteResult Result;
		Result.Error = TEXT("Coordinator is not connected.");
		Completion(MoveTemp(Result));
		return;
	}
	const FString RequestId = NextRequestId();
	PendingTransferCompletions.Add(RequestId, MoveTemp(Completion));
	TrackPendingRequest(RequestId);
	TSharedRef<FJsonObject> Message = MakeMessage(TEXT("transfer_complete"));
	Message->SetStringField(TEXT("request_id"), RequestId);
	Message->SetStringField(TEXT("ticket"), Ticket);
	Message->SetBoolField(TEXT("success"), bSucceeded);
	QueueMapJson(Message);
}

void UMT2ServerRuntimeSubsystem::RequestAccountRegistration(
	const FString& Username, const FString& Password,
	TFunction<void(FMT2AccountRegistrationResult&&)> Completion)
{
	if (!Config.IsGateway() || !bCoordinatorAuthenticated)
	{
		FMT2AccountRegistrationResult Result;
		Result.Error = TEXT("Gateway is not connected to the coordinator.");
		Completion(MoveTemp(Result));
		return;
	}
	const FString RequestId = NextRequestId();
	PendingAccountRegistrations.Add(RequestId, MoveTemp(Completion));
	TrackPendingRequest(RequestId);
	TSharedRef<FJsonObject> Message = MakeMessage(TEXT("account_register"));
	Message->SetStringField(TEXT("request_id"), RequestId);
	Message->SetStringField(TEXT("username"), Username);
	Message->SetStringField(TEXT("password"), Password);
	QueueMapJson(Message);
}

bool UMT2ServerRuntimeSubsystem::ShouldThrottleLoginAttempt(
	const FString& RemoteIp, const FString& Username)
{
	const double Now = FPlatformTime::Seconds();
	for (auto It = LoginAttemptStamps.CreateIterator(); It; ++It)
	{
		if (Now - It.Value() > LoginAttemptCooldownSeconds * 4.0)
		{
			It.RemoveCurrent();
		}
	}

	TArray<FString, TInlineAllocator<2>> Keys;
	if (!RemoteIp.IsEmpty())
	{
		// Strip the ephemeral port so reconnects from the same machine share one bucket.
		// IPv4 "1.2.3.4:7777" has exactly one colon; bracketed IPv6 ends in "]:port".
		FString Host = RemoteIp;
		int32 ColonIndex;
		if (Host.FindLastChar(TEXT(':'), ColonIndex))
		{
			const bool bSingleColon = ColonIndex == Host.Find(TEXT(":"));
			const bool bBracketedV6 = ColonIndex > 0 && Host[ColonIndex - 1] == TEXT(']');
			if (bSingleColon || bBracketedV6)
			{
				Host.LeftInline(ColonIndex);
			}
		}
		Keys.Add(TEXT("ip:") + Host);
	}
	if (!Username.TrimStartAndEnd().IsEmpty())
	{
		Keys.Add(TEXT("user:") + Username.TrimStartAndEnd().ToLower());
	}

	for (const FString& Key : Keys)
	{
		if (const double* LastAttempt = LoginAttemptStamps.Find(Key))
		{
			if (Now - *LastAttempt < LoginAttemptCooldownSeconds)
			{
				return true;
			}
		}
	}
	for (const FString& Key : Keys)
	{
		LoginAttemptStamps.Add(Key, Now);
	}
	return false;
}

void UMT2ServerRuntimeSubsystem::RequestAccountLogin(
	const FString& Username, const FString& Password, const FString& ExistingSessionToken,
	TFunction<void(FMT2AccountLoginResult&&)> Completion)
{
	if (!Config.IsGateway() || !bCoordinatorAuthenticated)
	{
		FMT2AccountLoginResult Result;
		Result.Error = TEXT("Gateway is not connected to the coordinator.");
		Completion(MoveTemp(Result));
		return;
	}
	const FString RequestId = NextRequestId();
	PendingAccountLogins.Add(RequestId, MoveTemp(Completion));
	TrackPendingRequest(RequestId);
	TSharedRef<FJsonObject> Message = MakeMessage(TEXT("account_login"));
	Message->SetStringField(TEXT("request_id"), RequestId);
	Message->SetStringField(TEXT("username"), Username);
	Message->SetStringField(TEXT("password"), Password);
	Message->SetStringField(TEXT("existing_session_token"), ExistingSessionToken);
	QueueMapJson(Message);
}

void UMT2ServerRuntimeSubsystem::NotifyCharacterLogout(
	const FString& CharacterId, const FString& AccountId)
{
	if (!Config.IsMapServer() || !bCoordinatorAuthenticated || CharacterId.IsEmpty() || AccountId.IsEmpty())
	{
		return;
	}
	TSharedRef<FJsonObject> Message = MakeMessage(TEXT("character_logout"));
	Message->SetStringField(TEXT("character_id"), CharacterId);
	Message->SetStringField(TEXT("account_id"), AccountId);
	QueueMapJson(Message);
}

void UMT2ServerRuntimeSubsystem::PublishPartySnapshot(
	const FString& PartyId, const TArray<FMT2PartyMemberData>& Members)
{
	if (!Config.IsMapServer() || !bCoordinatorAuthenticated || PartyId.IsEmpty() || Members.Num() < 2)
	{
		return;
	}
	TSharedRef<FJsonObject> Message = MakeMessage(TEXT("party_update"));
	Message->SetStringField(TEXT("party_id"), PartyId);
	TArray<TSharedPtr<FJsonValue>> Values;
	Values.Reserve(Members.Num());
	for (const FMT2PartyMemberData& Member : Members)
	{
		Values.Add(MakeShared<FJsonValueObject>(WritePartyMember(Member)));
	}
	Message->SetArrayField(TEXT("members"), MoveTemp(Values));
	QueueMapJson(Message);
}

void UMT2ServerRuntimeSubsystem::PublishPartyDisband(const FString& PartyId)
{
	if (!Config.IsMapServer() || !bCoordinatorAuthenticated || PartyId.IsEmpty()) return;
	TSharedRef<FJsonObject> Message = MakeMessage(TEXT("party_disband"));
	Message->SetStringField(TEXT("party_id"), PartyId);
	QueueMapJson(Message);
}

void UMT2ServerRuntimeSubsystem::RequestClusterPersist(
	TFunction<void(bool, const FString&)> Completion)
{
	if (!Config.IsMapServer() || !bCoordinatorAuthenticated)
	{
		Completion(false, TEXT("Coordinator is not connected."));
		return;
	}
	const FString RequestId = NextRequestId();
	PendingAdminControls.Add(RequestId, MoveTemp(Completion));
	TrackPendingRequest(RequestId);
	TSharedRef<FJsonObject> Message = MakeMessage(TEXT("admin_control"));
	Message->SetStringField(TEXT("request_id"), RequestId);
	Message->SetStringField(TEXT("action"), TEXT("persist"));
	QueueMapJson(Message);
}

void UMT2ServerRuntimeSubsystem::RequestClusterShutdown(
	bool bRestart, TFunction<void(bool, const FString&)> Completion)
{
	if (!Config.IsMapServer() || !bCoordinatorAuthenticated)
	{
		Completion(false, TEXT("Coordinator is not connected."));
		return;
	}
	const FString RequestId = NextRequestId();
	PendingAdminControls.Add(RequestId, MoveTemp(Completion));
	TrackPendingRequest(RequestId);
	TSharedRef<FJsonObject> Message = MakeMessage(TEXT("admin_control"));
	Message->SetStringField(TEXT("request_id"), RequestId);
	Message->SetStringField(TEXT("action"), bRestart ? TEXT("reboot") : TEXT("shutdown"));
	QueueMapJson(Message);
}

void UMT2ServerRuntimeSubsystem::RequestClusterServerList(
	TFunction<void(bool, const FString&)> Completion)
{
	if (!Config.IsMapServer() || !bCoordinatorAuthenticated)
	{
		Completion(false, TEXT("Coordinator is not connected."));
		return;
	}
	const FString RequestId = NextRequestId();
	PendingAdminControls.Add(RequestId, MoveTemp(Completion));
	TrackPendingRequest(RequestId);
	TSharedRef<FJsonObject> Message = MakeMessage(TEXT("admin_control"));
	Message->SetStringField(TEXT("request_id"), RequestId);
	Message->SetStringField(TEXT("action"), TEXT("servers"));
	QueueMapJson(Message);
}

void UMT2ServerRuntimeSubsystem::ExecuteLocalSummon(
	const FString& DestInstanceId, const FString& DestMapId, int32 DestChannel, const FVector& Location)
{
	UWorld* World = GetWorld();
	if (!Config.IsMapServer() || !World)
	{
		return;
	}
	const bool bIsDestination = Config.InstanceId == DestInstanceId;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		AMT2PlayerController* Controller = Cast<AMT2PlayerController>(It->Get());
		APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
		if (!Pawn)
		{
			continue;
		}
		// Random point within a 5m (500uu) disc around the caller, so summoned players don't stack.
		const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
		const float Radius = FMath::FRandRange(0.0f, 500.0f);
		const FVector Target = Location + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.0f);

		// Move the pawn to the target first. On the destination server that IS the teleport; on a
		// remote server the map transfer captures this position synchronously in the persistence save,
		// so the player arrives next to the caller on the destination map.
		Pawn->SetActorLocation(Target, false, nullptr, ETeleportType::TeleportPhysics);
		if (!bIsDestination)
		{
			Controller->RequestMapTransferFromServer(DestMapId, DestChannel, false);
		}
	}
}

void UMT2ServerRuntimeSubsystem::RequestSummonAllPlayers(
	const FVector& WorldLocation, TFunction<void(bool, const FString&)> Completion)
{
	if (!Config.IsMapServer() || !bCoordinatorAuthenticated)
	{
		Completion(false, TEXT("Coordinator is not connected."));
		return;
	}
	const FString RequestId = NextRequestId();
	PendingAdminControls.Add(RequestId, MoveTemp(Completion));
	TrackPendingRequest(RequestId);
	TSharedRef<FJsonObject> Message = MakeMessage(TEXT("admin_control"));
	Message->SetStringField(TEXT("request_id"), RequestId);
	Message->SetStringField(TEXT("action"), TEXT("summon"));
	Message->SetStringField(TEXT("dest_instance"), Config.InstanceId);
	Message->SetStringField(TEXT("dest_map"), Config.MapId);
	Message->SetNumberField(TEXT("dest_channel"), Config.Channel);
	Message->SetNumberField(TEXT("x"), WorldLocation.X);
	Message->SetNumberField(TEXT("y"), WorldLocation.Y);
	Message->SetNumberField(TEXT("z"), WorldLocation.Z);
	QueueMapJson(Message);
}

void UMT2ServerRuntimeSubsystem::HandleAdminControlRequest(
	FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message)
{
	const FString RequestId = GetStringField(Message, TEXT("request_id"));
	const FString ActionName = GetStringField(Message, TEXT("action")).ToLower();
	if (Peer.Role != TEXT("map") || RequestId.IsEmpty())
	{
		SendAdminControlResult(Peer.InstanceId, RequestId, false, TEXT("Administrative request rejected."));
		return;
	}
	if (ActionName == TEXT("servers"))
	{
		SendAdminControlResult(Peer.InstanceId, RequestId, true, BuildServerList());
		return;
	}
	if (ActionName == TEXT("summon"))
	{
		// Fan a "summon" out to every authenticated map server so it can teleport/transfer its own
		// players to the destination. Fire-and-forget: each map acts locally, no per-map ack.
		const FString DestInstance = GetStringField(Message, TEXT("dest_instance"));
		const FString DestMap = GetStringField(Message, TEXT("dest_map"));
		int32 DestChannel = 1;
		double X = 0.0, Y = 0.0, Z = 0.0;
		Message->TryGetNumberField(TEXT("dest_channel"), DestChannel);
		Message->TryGetNumberField(TEXT("x"), X);
		Message->TryGetNumberField(TEXT("y"), Y);
		Message->TryGetNumberField(TEXT("z"), Z);
		int32 MapCount = 0;
		for (const TUniquePtr<FCoordinatorPeer>& TargetPeer : CoordinatorPeers)
		{
			if (!TargetPeer || !TargetPeer->bAuthenticated || TargetPeer->Role != TEXT("map"))
			{
				continue;
			}
			TSharedRef<FJsonObject> Summon = MakeMessage(TEXT("summon"));
			Summon->SetStringField(TEXT("dest_instance"), DestInstance);
			Summon->SetStringField(TEXT("dest_map"), DestMap);
			Summon->SetNumberField(TEXT("dest_channel"), DestChannel);
			Summon->SetNumberField(TEXT("x"), X);
			Summon->SetNumberField(TEXT("y"), Y);
			Summon->SetNumberField(TEXT("z"), Z);
			QueuePeerJson(*TargetPeer, Summon);
			++MapCount;
		}
		SendAdminControlResult(Peer.InstanceId, RequestId, true,
			FString::Printf(TEXT("Summoning all players from %d map server(s)."), MapCount));
		return;
	}
	const EClusterControlAction Action = ActionName == TEXT("persist")
		? EClusterControlAction::Persist
		: ActionName == TEXT("shutdown") ? EClusterControlAction::Shutdown
		: ActionName == TEXT("reboot") ? EClusterControlAction::Reboot
		: EClusterControlAction::None;
	if (Action == EClusterControlAction::None)
	{
		SendAdminControlResult(Peer.InstanceId, RequestId, false, TEXT("Unknown cluster action."));
		return;
	}
	if (ClusterControl.IsActive())
	{
		SendAdminControlResult(Peer.InstanceId, RequestId, false,
			TEXT("Another cluster maintenance operation is already running."));
		return;
	}
	if (Action != EClusterControlAction::Persist)
	{
		SendAdminControlResult(Peer.InstanceId, RequestId, true,
			Action == EClusterControlAction::Reboot
				? TEXT("Cluster reboot started.") : TEXT("Cluster shutdown started."));
	}
	BeginCoordinatorControl(Action, Peer.InstanceId, RequestId);
}

void UMT2ServerRuntimeSubsystem::BeginCoordinatorControl(
	EClusterControlAction Action, const FString& RequesterInstanceId, const FString& RequestId)
{
	ClusterControl.Action = Action;
	ClusterControl.OperationId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	ClusterControl.RequesterInstanceId = RequesterInstanceId;
	ClusterControl.RequesterRequestId = RequestId;
	ClusterControl.DeadlineSeconds = FPlatformTime::Seconds() + 30.0;

	const bool bExiting = Action == EClusterControlAction::Shutdown || Action == EClusterControlAction::Reboot;
	if (bExiting) DestroySocket(ListenerSocket);
	for (const TUniquePtr<FCoordinatorPeer>& Peer : CoordinatorPeers)
	{
		if (!Peer || !Peer->bAuthenticated || (Action == EClusterControlAction::Persist && Peer->Role != TEXT("map")))
		{
			continue;
		}
		ClusterControl.PendingInstances.Add(Peer->InstanceId);
		TSharedRef<FJsonObject> Control = MakeMessage(
			bExiting ? TEXT("control_prepare_exit") : TEXT("control_persist"));
		Control->SetStringField(TEXT("operation_id"), ClusterControl.OperationId);
		if (bExiting) Control->SetBoolField(TEXT("restart"), Action == EClusterControlAction::Reboot);
		QueuePeerJson(*Peer, Control);
	}
	if (ClusterControl.PendingInstances.IsEmpty()) CompleteCoordinatorControl();
}

void UMT2ServerRuntimeSubsystem::HandleControlPeerReady(
	FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message)
{
	if (!ClusterControl.IsActive() || ClusterControl.bFinalizing ||
		GetStringField(Message, TEXT("operation_id")) != ClusterControl.OperationId ||
		!ClusterControl.PendingInstances.Contains(Peer.InstanceId))
	{
		return;
	}
	if (!Message->GetBoolField(TEXT("success")) && ClusterControl.Error.IsEmpty())
	{
		ClusterControl.Error = GetStringField(Message, TEXT("error"));
		if (ClusterControl.Error.IsEmpty())
		{
			ClusterControl.Error = FString::Printf(TEXT("Server %s failed maintenance preparation."), *Peer.InstanceId);
		}
	}
	ClusterControl.PendingInstances.Remove(Peer.InstanceId);
	if (ClusterControl.PendingInstances.IsEmpty()) CompleteCoordinatorControl();
}

void UMT2ServerRuntimeSubsystem::CompleteCoordinatorControl()
{
	if (!ClusterControl.IsActive() || ClusterControl.bFinalizing) return;
	ClusterControl.bFinalizing = true;
	UMT2PersistenceManager* Manager = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMT2PersistenceManager>() : nullptr;
	if (!Manager)
	{
		if (ClusterControl.Error.IsEmpty()) ClusterControl.Error = TEXT("Persistence manager is unavailable.");
	}
	auto Finish = [this](bool bDatabaseFlushed, const FString& DatabaseError)
	{
		if (!bDatabaseFlushed && ClusterControl.Error.IsEmpty()) ClusterControl.Error = DatabaseError;
		const bool bSucceeded = ClusterControl.Error.IsEmpty();
		const EClusterControlAction Action = ClusterControl.Action;
		if (Action == EClusterControlAction::Persist)
		{
			SendAdminControlResult(
				ClusterControl.RequesterInstanceId, ClusterControl.RequesterRequestId, bSucceeded,
				bSucceeded ? TEXT("All map servers saved successfully.") : ClusterControl.Error);
			ClusterControl.Reset();
			return;
		}

		for (const TUniquePtr<FCoordinatorPeer>& Peer : CoordinatorPeers)
		{
			if (!Peer || !Peer->bAuthenticated) continue;
			TSharedRef<FJsonObject> Commit = MakeMessage(TEXT("control_exit_commit"));
			Commit->SetBoolField(TEXT("restart"), Action == EClusterControlAction::Reboot);
			QueuePeerJson(*Peer, Commit);
			FlushSocket(Peer->Socket, Peer->SendBuffer, Peer->SendOffset);
		}
		ScheduleLocalExit(Action == EClusterControlAction::Reboot);
	};
	if (Manager)
	{
		Manager->FlushDatabase(
			[Finish = MoveTemp(Finish)](bool bSucceeded, FString&& Error) mutable
			{
				Finish(bSucceeded, Error);
			});
	}
	else
	{
		Finish(false, ClusterControl.Error);
	}
}

void UMT2ServerRuntimeSubsystem::SendAdminControlResult(
	const FString& InstanceId, const FString& RequestId, bool bSucceeded, const FString& MessageText)
{
	TSharedRef<FJsonObject> Result = MakeMessage(TEXT("admin_control_result"));
	Result->SetStringField(TEXT("request_id"), RequestId);
	Result->SetBoolField(TEXT("success"), bSucceeded);
	Result->SetStringField(TEXT("message"), MessageText);
	SendToInstance(InstanceId, Result);
}

FString UMT2ServerRuntimeSubsystem::BuildServerList() const
{
	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("Coordinator | %s:%d | active peers: %d"),
		*Config.CoordinatorBindIp, Config.CoordinatorPort, CoordinatorPeers.Num()));
	for (const TUniquePtr<FCoordinatorPeer>& Peer : CoordinatorPeers)
	{
		if (!Peer || !Peer->bAuthenticated || Peer->Role != TEXT("gateway")) continue;
		Lines.Add(FString::Printf(TEXT("Gateway | %s | %s:%d | players %d/%d"),
			*Peer->InstanceId, *Peer->PublicIp, Peer->GamePort, Peer->PlayerCount, Peer->MaxPlayers));
	}
	TArray<FMT2MapServerDescriptor> Maps;
	RegisteredMapServers.GenerateValueArray(Maps);
	Maps.Sort([](const FMT2MapServerDescriptor& A, const FMT2MapServerDescriptor& B)
	{
		return A.MapId == B.MapId ? A.Channel < B.Channel : A.MapId < B.MapId;
	});
	for (const FMT2MapServerDescriptor& Server : Maps)
	{
		Lines.Add(FString::Printf(TEXT("Map | %s | %s CH%d | %s:%d | players %d/%d | %s"),
			*Server.InstanceId, *Server.MapId, Server.Channel, *Server.PublicIp, Server.GamePort,
			Server.PlayerCount, Server.MaxPlayers,
			Server.State == EMT2MapServerState::Ready ? TEXT("ready") :
			Server.State == EMT2MapServerState::Draining ? TEXT("draining") : TEXT("loading")));
	}
	return FString::Join(Lines, TEXT("\n"));
}

void UMT2ServerRuntimeSubsystem::BeginLocalControl(
	EClusterControlAction Action, const FString& OperationId)
{
	if (OperationId.IsEmpty()) return;
	if (Action == EClusterControlAction::Persist)
	{
		ForceSaveAllPlayers([this, Action, OperationId](bool bSucceeded, const FString& Error)
		{
			SendLocalControlReady(Action, OperationId, bSucceeded, Error);
		});
		return;
	}
	if (bLocalMaintenancePreparing) return;
	bLocalMaintenancePreparing = true;
	const FString Message = TEXT("Server is shutting down.");
	NotifyAllPlayersForMaintenance(Message, false);
	ForceSaveAllPlayers([this, Action, OperationId, Message](bool bSucceeded, const FString& Error)
	{
		NotifyAllPlayersForMaintenance(Message, true);
		if (UWorld* World = GetWorld())
		{
			FTimerHandle DisconnectGraceTimer;
			World->GetTimerManager().SetTimer(
				DisconnectGraceTimer,
				FTimerDelegate::CreateWeakLambda(this,
					[this, Action, OperationId, bSucceeded, Error]()
					{
						SendLocalControlReady(Action, OperationId, bSucceeded, Error);
					}),
				0.5f, false);
		}
		else SendLocalControlReady(Action, OperationId, bSucceeded, Error);
	});
}

void UMT2ServerRuntimeSubsystem::ForceSaveAllPlayers(
	TFunction<void(bool, const FString&)> Completion)
{
	TArray<UMT2PersistenceComponent*> Components;
	if (UWorld* World = GetWorld())
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const AMT2PlayerState* State = It->Get()->GetPlayerState<AMT2PlayerState>();
			UMT2PersistenceComponent* Persistence = State ? State->GetPersistenceComponent() : nullptr;
			if (Persistence && !Persistence->GetEntityId().IsEmpty()) Components.Add(Persistence);
		}
	}
	if (Components.IsEmpty())
	{
		Completion(true, FString());
		return;
	}
	const TSharedRef<int32> Remaining = MakeShared<int32>(Components.Num());
	const TSharedRef<bool> AllSucceeded = MakeShared<bool>(true);
	const TSharedRef<FString> FirstError = MakeShared<FString>();
	const TSharedRef<TFunction<void(bool, const FString&)>> FinalCompletion =
		MakeShared<TFunction<void(bool, const FString&)>>(MoveTemp(Completion));
	for (UMT2PersistenceComponent* Persistence : Components)
	{
		Persistence->Flush([Remaining, AllSucceeded, FirstError, FinalCompletion](
			bool bSucceeded, const FString& Error)
		{
			if (!bSucceeded)
			{
				*AllSucceeded = false;
				if (FirstError->IsEmpty()) *FirstError = Error;
			}
			--*Remaining;
			if (*Remaining == 0 && *FinalCompletion)
			{
				(*FinalCompletion)(*AllSucceeded, *FirstError);
			}
		});
	}
}

void UMT2ServerRuntimeSubsystem::NotifyAllPlayersForMaintenance(
	const FString& Message, bool bDisconnect)
{
	if (UWorld* World = GetWorld())
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			if (AMT2PlayerController* Controller = Cast<AMT2PlayerController>(It->Get()))
			{
				if (bDisconnect) Controller->DisconnectForServerMaintenance(Message);
				else Controller->PrepareForServerMaintenance(Message);
			}
		}
	}
}

void UMT2ServerRuntimeSubsystem::SendLocalControlReady(
	EClusterControlAction Action, const FString& OperationId, bool bSucceeded, const FString& Error)
{
	if (!bCoordinatorAuthenticated) return;
	TSharedRef<FJsonObject> Ready = MakeMessage(TEXT("control_ready"));
	Ready->SetStringField(TEXT("operation_id"), OperationId);
	Ready->SetBoolField(TEXT("success"), bSucceeded);
	Ready->SetStringField(TEXT("error"), Error);
	Ready->SetBoolField(TEXT("restart"), Action == EClusterControlAction::Reboot);
	QueueMapJson(Ready);
}

void UMT2ServerRuntimeSubsystem::ScheduleLocalExit(bool bRestart)
{
	if (bLocalExitScheduled) return;
	bLocalExitScheduled = true;
	auto ExitProcess = [this, bRestart]()
	{
		if (bRestart && !RelaunchCurrentProcess())
		{
			UE_LOG(LogMT2ServerRuntime, Error, TEXT("Could not launch replacement process during reboot."));
		}
		FPlatformMisc::RequestExit(false);
	};
	if (UWorld* World = GetWorld())
	{
		FTimerHandle Timer;
		World->GetTimerManager().SetTimer(
			Timer, FTimerDelegate::CreateWeakLambda(this, MoveTemp(ExitProcess)), 0.5f, false);
	}
	else ExitProcess();
}

bool UMT2ServerRuntimeSubsystem::RelaunchCurrentProcess() const
{
	FString Original = FCommandLine::GetOriginal();
	Original.TrimStartAndEndInline();
	if (Original.StartsWith(TEXT("\"")))
	{
		const int32 ClosingQuote = Original.Find(TEXT("\""), ESearchCase::CaseSensitive,
			ESearchDir::FromStart, 1);
		if (ClosingQuote != INDEX_NONE) Original.RightChopInline(ClosingQuote + 1);
	}
	else
	{
		int32 FirstSpace = INDEX_NONE;
		if (Original.FindChar(TEXT(' '), FirstSpace)) Original.RightChopInline(FirstSpace + 1);
	}
	Original.TrimStartInline();
	const FString Parameters = FString::Printf(TEXT("-WaitForParentPid=%u %s"),
		FPlatformProcess::GetCurrentProcessId(), *Original);
	uint32 ChildProcessId = 0;
	FProcHandle Handle = FPlatformProcess::CreateProc(
		FPlatformProcess::ExecutablePath(), *Parameters, true, true, true,
		&ChildProcessId, 0, *FPaths::ProjectDir(), nullptr);
	if (!Handle.IsValid()) return false;
	FPlatformProcess::CloseProc(Handle);
	return true;
}

void UMT2ServerRuntimeSubsystem::RequestCharacterCreation(
	const FString& SessionToken, const FString& CharacterName,
	const FMT2CharacterAppearance& Appearance, EMT2Empire Empire,
	TFunction<void(FMT2CharacterCreateResult&&)> Completion)
{
	if (!Config.IsGateway() || !bCoordinatorAuthenticated || SessionToken.IsEmpty())
	{
		FMT2CharacterCreateResult Result;
		Result.Error = TEXT("Gateway login session is unavailable.");
		Completion(MoveTemp(Result));
		return;
	}
	const FString RequestId = NextRequestId();
	PendingCharacterCreations.Add(RequestId, MoveTemp(Completion));
	TrackPendingRequest(RequestId);
	TSharedRef<FJsonObject> Message = MakeMessage(TEXT("character_create"));
	Message->SetStringField(TEXT("request_id"), RequestId);
	Message->SetStringField(TEXT("session_token"), SessionToken);
	Message->SetStringField(TEXT("character_name"), CharacterName);
	Message->SetNumberField(TEXT("race"), static_cast<int32>(Appearance.Race));
	Message->SetNumberField(TEXT("sex"), static_cast<int32>(Appearance.Sex));
	Message->SetNumberField(TEXT("style"), static_cast<int32>(Appearance.Style));
	Message->SetNumberField(TEXT("empire"), static_cast<int32>(Empire));
	QueueMapJson(Message);
}

void UMT2ServerRuntimeSubsystem::RequestCharacterAdmission(
	const FString& SessionToken, const FString& CharacterId, int32 PreferredChannel,
	TFunction<void(FMT2CharacterAdmissionResult&&)> Completion)
{
	if (!Config.IsGateway() || !bCoordinatorAuthenticated || SessionToken.IsEmpty() || CharacterId.IsEmpty())
	{
		FMT2CharacterAdmissionResult Result;
		Result.Error = TEXT("Gateway login session or character is invalid.");
		Completion(MoveTemp(Result));
		return;
	}
	const FString RequestId = NextRequestId();
	PendingCharacterAdmissions.Add(RequestId, MoveTemp(Completion));
	TrackPendingRequest(RequestId);
	TSharedRef<FJsonObject> Message = MakeMessage(TEXT("character_admission"));
	Message->SetStringField(TEXT("request_id"), RequestId);
	Message->SetStringField(TEXT("session_token"), SessionToken);
	Message->SetStringField(TEXT("character_id"), CharacterId);
	Message->SetNumberField(TEXT("channel"), FMath::Max(PreferredChannel, 0));
	QueueMapJson(Message);
}

FString UMT2ServerRuntimeSubsystem::NextRequestId()
{
	return FString::Printf(TEXT("%s-%llu"), *Config.InstanceId, ++RequestSequence);
}

void UMT2ServerRuntimeSubsystem::TrackPendingRequest(const FString& RequestId)
{
	PendingRequestDeadlines.Add(
		RequestId, FPlatformTime::Seconds() + Config.RequestTimeoutSeconds);
}

void UMT2ServerRuntimeSubsystem::ExpirePendingRequests(double NowSeconds)
{
	TArray<FString> ExpiredIds;
	for (const TPair<FString, double>& Pair : PendingRequestDeadlines)
	{
		if (NowSeconds >= Pair.Value) ExpiredIds.Add(Pair.Key);
	}
	for (const FString& RequestId : ExpiredIds)
	{
		PendingRequestDeadlines.Remove(RequestId);
		const FString Error = TEXT("Coordinator request timed out.");
		TFunction<void(FMT2PersistenceLoadResult&&)> Load;
		if (PendingLoads.RemoveAndCopyValue(RequestId, Load))
		{
			FMT2PersistenceLoadResult Result; Result.Error = Error; Load(MoveTemp(Result)); continue;
		}
		TFunction<void(FMT2PersistenceSaveResult&&)> Save;
		if (PendingSaves.RemoveAndCopyValue(RequestId, Save))
		{
			FMT2PersistenceSaveResult Result; Result.Error = Error; Save(MoveTemp(Result)); continue;
		}
		TFunction<void(FMT2MapRouteResult&&)> Route;
		if (PendingRoutes.RemoveAndCopyValue(RequestId, Route))
		{
			FMT2MapRouteResult Result; Result.Error = Error; Route(MoveTemp(Result)); continue;
		}
		TFunction<void(FMT2TransferTicketResult&&)> Ticket;
		if (PendingTransferTickets.RemoveAndCopyValue(RequestId, Ticket))
		{
			FMT2TransferTicketResult Result; Result.Error = Error; Ticket(MoveTemp(Result)); continue;
		}
		TFunction<void(FMT2TransferClaimResult&&)> Claim;
		if (PendingTransferClaims.RemoveAndCopyValue(RequestId, Claim))
		{
			FMT2TransferClaimResult Result; Result.Error = Error; Claim(MoveTemp(Result)); continue;
		}
		TFunction<void(FMT2TransferCompleteResult&&)> Complete;
		if (PendingTransferCompletions.RemoveAndCopyValue(RequestId, Complete))
		{
			FMT2TransferCompleteResult Result; Result.Error = Error; Complete(MoveTemp(Result)); continue;
		}
		TFunction<void(FMT2AccountRegistrationResult&&)> Registration;
		if (PendingAccountRegistrations.RemoveAndCopyValue(RequestId, Registration))
		{
			FMT2AccountRegistrationResult Result; Result.Error = Error; Registration(MoveTemp(Result)); continue;
		}
		TFunction<void(FMT2AccountLoginResult&&)> Login;
		if (PendingAccountLogins.RemoveAndCopyValue(RequestId, Login))
		{
			FMT2AccountLoginResult Result; Result.Error = Error; Login(MoveTemp(Result)); continue;
		}
		TFunction<void(FMT2CharacterCreateResult&&)> Creation;
		if (PendingCharacterCreations.RemoveAndCopyValue(RequestId, Creation))
		{
			FMT2CharacterCreateResult Result; Result.Error = Error; Creation(MoveTemp(Result)); continue;
		}
		TFunction<void(FMT2CharacterAdmissionResult&&)> Admission;
		if (PendingCharacterAdmissions.RemoveAndCopyValue(RequestId, Admission))
		{
			FMT2CharacterAdmissionResult Result; Result.Error = Error; Admission(MoveTemp(Result));
			continue;
		}
		TFunction<void(bool, const FString&)> AdminControl;
		if (PendingAdminControls.RemoveAndCopyValue(RequestId, AdminControl))
		{
			AdminControl(false, Error);
		}
	}
}

// -------------------------------------------------------------------------------------------------
// Messenger
//
// The old MessengerManager kept the friend graph in SQL and presence in memory, and pushed login and
// logout events to every companion. The same split holds here, except the pieces live in different
// processes: SQLite and routing sit on the coordinator, the player-facing component on a map server.
// -------------------------------------------------------------------------------------------------

FMT2PersistenceBackend* UMT2ServerRuntimeSubsystem::GetMessengerStore() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	UMT2PersistenceManager* Manager =
		GameInstance ? GameInstance->GetSubsystem<UMT2PersistenceManager>() : nullptr;
	return Manager ? Manager->GetLocalBackend() : nullptr;
}

bool UMT2ServerRuntimeSubsystem::SendToCharacter(
	const FString& CharacterId, const TSharedRef<FJsonObject>& Message)
{
	const FMessengerPresence* Presence = MessengerPresences.Find(CharacterId);
	if (!Presence)
	{
		return false;
	}
	for (const TUniquePtr<FCoordinatorPeer>& Peer : CoordinatorPeers)
	{
		if (Peer && Peer->bAuthenticated && Peer->InstanceId == Presence->MapInstanceId)
		{
			Message->SetStringField(TEXT("character_id"), CharacterId);
			QueuePeerJson(*Peer, Message);
			return true;
		}
	}
	return false;
}

void UMT2ServerRuntimeSubsystem::SendMessengerSnapshot(const FString& CharacterId)
{
	FMT2PersistenceBackend* Store = GetMessengerStore();
	if (!Store)
	{
		return;
	}
	TArray<FMT2FriendEntry> Friends = Store->LoadFriends(CharacterId);

	TArray<TSharedPtr<FJsonValue>> FriendValues;
	FriendValues.Reserve(Friends.Num());
	for (FMT2FriendEntry& Friend : Friends)
	{
		// The stored map is only where they last saved; a live presence overrides it.
		if (const FMessengerPresence* Presence = MessengerPresences.Find(Friend.CharacterId))
		{
			Friend.bOnline = true;
			Friend.MapId = Presence->MapId;
			Friend.Channel = Presence->Channel;
			if (Presence->Level > 0)
			{
				Friend.Level = Presence->Level;
			}
		}
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("character_id"), Friend.CharacterId);
		Object->SetStringField(TEXT("name"), Friend.CharacterName);
		Object->SetBoolField(TEXT("online"), Friend.bOnline);
		Object->SetStringField(TEXT("map_id"), Friend.MapId);
		Object->SetNumberField(TEXT("channel"), Friend.Channel);
		Object->SetNumberField(TEXT("level"), Friend.Level);
		Object->SetNumberField(TEXT("unread"), Friend.UnreadCount);
		FriendValues.Add(MakeShared<FJsonValueObject>(Object));
	}

	TSharedRef<FJsonObject> Snapshot = MakeMessage(TEXT("messenger_snapshot"));
	Snapshot->SetArrayField(TEXT("friends"), FriendValues);
	SendToCharacter(CharacterId, Snapshot);
}

void UMT2ServerRuntimeSubsystem::BroadcastMessengerPresence(const FString& CharacterId, bool bOnline)
{
	FMT2PersistenceBackend* Store = GetMessengerStore();
	if (!Store)
	{
		return;
	}
	// A friendship is symmetric, so this character's own list names everyone who needs telling.
	const FMessengerPresence* Presence = MessengerPresences.Find(CharacterId);
	for (const FMT2FriendEntry& Friend : Store->LoadFriends(CharacterId))
	{
		if (!MessengerPresences.Contains(Friend.CharacterId))
		{
			continue; // that companion is offline; they will get the state in their login snapshot
		}
		TSharedRef<FJsonObject> Update = MakeMessage(TEXT("messenger_presence"));
		Update->SetStringField(TEXT("companion_id"), CharacterId);
		Update->SetStringField(TEXT("companion_name"), Presence ? Presence->CharacterName : FString());
		Update->SetBoolField(TEXT("online"), bOnline);
		Update->SetStringField(TEXT("map_id"), Presence ? Presence->MapId : FString());
		Update->SetNumberField(TEXT("channel"), Presence ? Presence->Channel : 0);
		Update->SetNumberField(TEXT("level"), Presence ? Presence->Level : 0);
		SendToCharacter(Friend.CharacterId, Update);
	}
}

void UMT2ServerRuntimeSubsystem::MessengerDropPresence(const FString& CharacterId)
{
	if (CharacterId.IsEmpty() || !MessengerPresences.Contains(CharacterId))
	{
		return;
	}
	// Announce while the entry still exists, so companions get the name with the logout.
	FMT2GuildSnapshot Guild;
	const int32 GuildId = GetMessengerStore() && GetMessengerStore()->LoadGuildForCharacter(CharacterId, Guild)
		? Guild.GuildId : 0;
	BroadcastMessengerPresence(CharacterId, false);
	MessengerPresences.Remove(CharacterId);
	if (GuildId > 0) BroadcastGuildSnapshot(GuildId);
	MessengerPendingRequests.Remove(CharacterId);
}

void UMT2ServerRuntimeSubsystem::HandleMessengerLogin(
	FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message)
{
	const FString CharacterId = GetStringField(Message, TEXT("character_id"));
	if (CharacterId.IsEmpty())
	{
		return;
	}
	FMessengerPresence& Presence = MessengerPresences.FindOrAdd(CharacterId);
	Presence.CharacterId = CharacterId;
	Presence.CharacterName = GetStringField(Message, TEXT("name"));
	Presence.MapInstanceId = Peer.InstanceId;
	Presence.MapId = GetStringField(Message, TEXT("map_id"));
	Presence.Channel = FMath::Max(GetIntField(Message, TEXT("channel")), 1);
	Presence.Level = GetIntField(Message, TEXT("level"));

	SendMessengerSnapshot(CharacterId);
	BroadcastMessengerPresence(CharacterId, true);
	SendGuildSnapshot(CharacterId);
	FMT2GuildSnapshot LoginGuild;
	if (GetMessengerStore() && GetMessengerStore()->LoadGuildForCharacter(CharacterId, LoginGuild))
		BroadcastGuildSnapshot(LoginGuild.GuildId);

	// Anything that arrived while they were away, plus anything a previous session never showed.
	if (FMT2PersistenceBackend* Store = GetMessengerStore())
	{
		const TArray<FMT2PrivateMessage> Backlog = Store->LoadUndeliveredMessages(CharacterId);
		TArray<int64> DeliveredIds;
		DeliveredIds.Reserve(Backlog.Num());
		for (const FMT2PrivateMessage& Stored : Backlog)
		{
			TSharedRef<FJsonObject> Delivery = MakeMessage(TEXT("messenger_message"));
			Delivery->SetNumberField(TEXT("message_id"), static_cast<double>(Stored.MessageId));
			Delivery->SetStringField(TEXT("sender_id"), Stored.SenderCharacterId);
			Delivery->SetStringField(TEXT("sender_name"), Stored.SenderName);
			Delivery->SetStringField(TEXT("body"), Stored.Body);
			Delivery->SetNumberField(TEXT("sent_unix"), static_cast<double>(Stored.SentUnixTime));
			Delivery->SetBoolField(TEXT("was_offline"), Stored.bWasOffline);
			if (SendToCharacter(CharacterId, Delivery))
			{
				DeliveredIds.Add(Stored.MessageId);
			}
		}
		Store->MarkMessagesDelivered(DeliveredIds);
	}
}

void UMT2ServerRuntimeSubsystem::HandleMessengerRequestAdd(
	FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message)
{
	const FString CharacterId = GetStringField(Message, TEXT("character_id"));
	const FString TargetName = GetStringField(Message, TEXT("target_name")).TrimStartAndEnd();
	FMT2PersistenceBackend* Store = GetMessengerStore();
	if (CharacterId.IsEmpty() || TargetName.IsEmpty() || !Store)
	{
		return;
	}

	auto Refuse = [this, &CharacterId](EMT2MessengerResult Result)
	{
		TSharedRef<FJsonObject> Reply = MakeMessage(TEXT("messenger_result"));
		Reply->SetStringField(TEXT("action"), TEXT("add"));
		Reply->SetNumberField(TEXT("result"), static_cast<uint8>(Result));
		SendToCharacter(CharacterId, Reply);
	};

	FString TargetId;
	FString ResolvedName;
	if (!Store->FindCharacterIdByName(TargetName, TargetId, ResolvedName))
	{
		Refuse(EMT2MessengerResult::UnknownCharacter);
		return;
	}
	if (TargetId == CharacterId)
	{
		Refuse(EMT2MessengerResult::CannotAddSelf);
		return;
	}
	if (Store->AreFriends(CharacterId, TargetId))
	{
		Refuse(EMT2MessengerResult::AlreadyFriends);
		return;
	}
	if (Store->LoadFriends(CharacterId).Num() >= MT2Messenger::MaxFriends)
	{
		Refuse(EMT2MessengerResult::ListFull);
		return;
	}

	// The request is offered only to a character who is online, exactly like the old
	// RequestToAdd -> "messenger_auth <name>" packet, which needed a live target to receive it.
	const FMessengerPresence* SelfPresence = MessengerPresences.Find(CharacterId);
	TSharedRef<FJsonObject> Ask = MakeMessage(TEXT("messenger_request"));
	Ask->SetStringField(TEXT("from_id"), CharacterId);
	Ask->SetStringField(TEXT("from_name"), SelfPresence ? SelfPresence->CharacterName : FString());
	if (!SendToCharacter(TargetId, Ask))
	{
		Refuse(EMT2MessengerResult::UnknownCharacter);
		return;
	}
	MessengerPendingRequests.AddUnique(TargetId, CharacterId);
	Refuse(EMT2MessengerResult::Success);
}

void UMT2ServerRuntimeSubsystem::HandleMessengerAnswerRequest(
	FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message)
{
	const FString CharacterId = GetStringField(Message, TEXT("character_id"));
	const FString RequesterId = GetStringField(Message, TEXT("requester_id"));
	const bool bAccept = Message->GetBoolField(TEXT("accept"));
	FMT2PersistenceBackend* Store = GetMessengerStore();
	if (CharacterId.IsEmpty() || RequesterId.IsEmpty() || !Store)
	{
		return;
	}
	// An answer is only honoured against a request we actually issued (old AuthToAdd checks the same
	// thing through its CRC set), so a client cannot add itself to someone else's list.
	TArray<FString> Requests;
	MessengerPendingRequests.MultiFind(CharacterId, Requests);
	if (!Requests.Contains(RequesterId))
	{
		return;
	}
	MessengerPendingRequests.RemoveSingle(CharacterId, RequesterId);
	if (!bAccept)
	{
		return;
	}

	FString Error;
	if (!Store->AddFriendPair(CharacterId, RequesterId, Error))
	{
		UE_LOG(LogMT2ServerRuntime, Warning, TEXT("Messenger: could not add friend pair: %s"), *Error);
		return;
	}
	// Both sides get a fresh list, so the new companion appears with the right lamp immediately.
	SendMessengerSnapshot(CharacterId);
	SendMessengerSnapshot(RequesterId);
}

void UMT2ServerRuntimeSubsystem::HandleMessengerRemoveFriend(
	FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message)
{
	const FString CharacterId = GetStringField(Message, TEXT("character_id"));
	const FString CompanionId = GetStringField(Message, TEXT("companion_id"));
	FMT2PersistenceBackend* Store = GetMessengerStore();
	if (CharacterId.IsEmpty() || CompanionId.IsEmpty() || !Store)
	{
		return;
	}
	FString Error;
	// Removal drops both directions: the old RemoveFromList did the same, since a one-sided
	// friendship would leave the other player with a companion who never lights up.
	if (!Store->RemoveFriendPair(CharacterId, CompanionId, Error))
	{
		UE_LOG(LogMT2ServerRuntime, Warning, TEXT("Messenger: could not remove friend: %s"), *Error);
		return;
	}
	SendMessengerSnapshot(CharacterId);
	SendMessengerSnapshot(CompanionId);
}

void UMT2ServerRuntimeSubsystem::HandleMessengerSendMessage(
	FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message)
{
	const FString CharacterId = GetStringField(Message, TEXT("character_id"));
	const FString RecipientId = GetStringField(Message, TEXT("recipient_id"));
	const FString SenderName = GetStringField(Message, TEXT("sender_name"));
	FString Body = GetStringField(Message, TEXT("body")).TrimStartAndEnd();
	Body.ReplaceInline(TEXT("\r"), TEXT(" "));
	Body.ReplaceInline(TEXT("\n"), TEXT(" "));
	Body = Body.Left(MT2Messenger::MaxMessageLength);

	FMT2PersistenceBackend* Store = GetMessengerStore();
	if (CharacterId.IsEmpty() || RecipientId.IsEmpty() || Body.IsEmpty() || !Store)
	{
		return;
	}
	// A whisper reaches anyone who is online, as it did in the old client - you do not have to be on
	// someone's list to talk to them. Only *offline* delivery is reserved for friends: without that,
	// a stranger could fill an absent player's message store.
	const bool bRecipientOnline = MessengerPresences.Contains(RecipientId);
	if (!bRecipientOnline && !Store->AreFriends(CharacterId, RecipientId))
	{
		TSharedRef<FJsonObject> Reply = MakeMessage(TEXT("messenger_result"));
		Reply->SetStringField(TEXT("action"), TEXT("send"));
		Reply->SetNumberField(TEXT("result"), static_cast<uint8>(EMT2MessengerResult::NotFriends));
		SendToCharacter(CharacterId, Reply);
		return;
	}

	FMT2PrivateMessage Stored;
	Stored.SenderCharacterId = CharacterId;
	Stored.SenderName = SenderName;
	Stored.RecipientCharacterId = RecipientId;
	Stored.Body = Body;
	Stored.SentUnixTime = FDateTime::UtcNow().ToUnixTimestamp();

	FString Error;
	// Stored first, then delivered: a message the recipient never sees because their server dropped
	// between the two is still in the store for their next login, which is the whole point of
	// persisting them.
	Stored.MessageId = Store->StorePrivateMessage(Stored, Error);
	if (Stored.MessageId == 0)
	{
		UE_LOG(LogMT2ServerRuntime, Warning, TEXT("Messenger: could not store message: %s"), *Error);
		return;
	}

	TSharedRef<FJsonObject> Delivery = MakeMessage(TEXT("messenger_message"));
	Delivery->SetNumberField(TEXT("message_id"), static_cast<double>(Stored.MessageId));
	Delivery->SetStringField(TEXT("sender_id"), CharacterId);
	Delivery->SetStringField(TEXT("sender_name"), SenderName);
	Delivery->SetStringField(TEXT("body"), Body);
	Delivery->SetNumberField(TEXT("sent_unix"), static_cast<double>(Stored.SentUnixTime));
	Delivery->SetBoolField(TEXT("was_offline"), false);
	if (SendToCharacter(RecipientId, Delivery))
	{
		Store->MarkMessagesDelivered({Stored.MessageId});
	}

	// The sender sees their own line too, so both halves of the conversation read the same.
	TSharedRef<FJsonObject> Echo = MakeMessage(TEXT("messenger_message_sent"));
	Echo->SetNumberField(TEXT("message_id"), static_cast<double>(Stored.MessageId));
	Echo->SetStringField(TEXT("recipient_id"), RecipientId);
	Echo->SetStringField(TEXT("body"), Body);
	Echo->SetNumberField(TEXT("sent_unix"), static_cast<double>(Stored.SentUnixTime));
	SendToCharacter(CharacterId, Echo);
}

void UMT2ServerRuntimeSubsystem::HandleMessengerOpenConversation(
	FCoordinatorPeer& Peer, const TSharedPtr<FJsonObject>& Message)
{
	const FString CharacterId = GetStringField(Message, TEXT("character_id"));
	const FString CompanionId = GetStringField(Message, TEXT("companion_id"));
	FMT2PersistenceBackend* Store = GetMessengerStore();
	if (CharacterId.IsEmpty() || CompanionId.IsEmpty() || !Store)
	{
		return;
	}
	// Opening a conversation is what marks it read, matching how the unread badge clears.
	Store->MarkMessagesRead(CharacterId, CompanionId);

	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FMT2PrivateMessage& Stored :
		Store->LoadConversation(CharacterId, CompanionId, MT2Messenger::MaxConversationHistory))
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("message_id"), static_cast<double>(Stored.MessageId));
		Object->SetStringField(TEXT("sender_id"), Stored.SenderCharacterId);
		Object->SetStringField(TEXT("sender_name"), Stored.SenderName);
		Object->SetStringField(TEXT("body"), Stored.Body);
		Object->SetNumberField(TEXT("sent_unix"), static_cast<double>(Stored.SentUnixTime));
		Values.Add(MakeShared<FJsonValueObject>(Object));
	}
	TSharedRef<FJsonObject> Reply = MakeMessage(TEXT("messenger_conversation"));
	Reply->SetStringField(TEXT("companion_id"), CompanionId);
	Reply->SetArrayField(TEXT("messages"), Values);
	SendToCharacter(CharacterId, Reply);

	// The unread badge just changed, so the list is stale.
	SendMessengerSnapshot(CharacterId);
}

// ---- map-server side: publishing ----------------------------------------------------------------

void UMT2ServerRuntimeSubsystem::PublishMessengerLogin(
	const FString& CharacterId, const FString& CharacterName, int32 Level)
{
	if (!Config.IsMapServer() || !bCoordinatorAuthenticated || CharacterId.IsEmpty())
	{
		return;
	}
	TSharedRef<FJsonObject> Message = MakeMessage(TEXT("messenger_login"));
	Message->SetStringField(TEXT("character_id"), CharacterId);
	Message->SetStringField(TEXT("name"), CharacterName);
	Message->SetStringField(TEXT("map_id"), Config.MapId);
	Message->SetNumberField(TEXT("channel"), Config.Channel);
	Message->SetNumberField(TEXT("level"), Level);
	QueueMapJson(Message);
}

void UMT2ServerRuntimeSubsystem::PublishMessengerRequestAdd(
	const FString& CharacterId, const FString& TargetName)
{
	if (!Config.IsMapServer() || !bCoordinatorAuthenticated || CharacterId.IsEmpty())
	{
		return;
	}
	TSharedRef<FJsonObject> Message = MakeMessage(TEXT("messenger_request_add"));
	Message->SetStringField(TEXT("character_id"), CharacterId);
	Message->SetStringField(TEXT("target_name"), TargetName);
	QueueMapJson(Message);
}

void UMT2ServerRuntimeSubsystem::PublishMessengerAnswerRequest(
	const FString& CharacterId, const FString& RequesterId, bool bAccept)
{
	if (!Config.IsMapServer() || !bCoordinatorAuthenticated || CharacterId.IsEmpty())
	{
		return;
	}
	TSharedRef<FJsonObject> Message = MakeMessage(TEXT("messenger_answer"));
	Message->SetStringField(TEXT("character_id"), CharacterId);
	Message->SetStringField(TEXT("requester_id"), RequesterId);
	Message->SetBoolField(TEXT("accept"), bAccept);
	QueueMapJson(Message);
}

void UMT2ServerRuntimeSubsystem::PublishMessengerRemoveFriend(
	const FString& CharacterId, const FString& CompanionId)
{
	if (!Config.IsMapServer() || !bCoordinatorAuthenticated || CharacterId.IsEmpty())
	{
		return;
	}
	TSharedRef<FJsonObject> Message = MakeMessage(TEXT("messenger_remove"));
	Message->SetStringField(TEXT("character_id"), CharacterId);
	Message->SetStringField(TEXT("companion_id"), CompanionId);
	QueueMapJson(Message);
}

void UMT2ServerRuntimeSubsystem::PublishMessengerSendMessage(
	const FString& CharacterId, const FString& SenderName,
	const FString& RecipientId, const FString& Body)
{
	if (!Config.IsMapServer() || !bCoordinatorAuthenticated || CharacterId.IsEmpty())
	{
		return;
	}
	TSharedRef<FJsonObject> Message = MakeMessage(TEXT("messenger_send"));
	Message->SetStringField(TEXT("character_id"), CharacterId);
	Message->SetStringField(TEXT("sender_name"), SenderName);
	Message->SetStringField(TEXT("recipient_id"), RecipientId);
	Message->SetStringField(TEXT("body"), Body);
	QueueMapJson(Message);
}

void UMT2ServerRuntimeSubsystem::PublishMessengerOpenConversation(
	const FString& CharacterId, const FString& CompanionId)
{
	if (!Config.IsMapServer() || !bCoordinatorAuthenticated || CharacterId.IsEmpty())
	{
		return;
	}
	TSharedRef<FJsonObject> Message = MakeMessage(TEXT("messenger_open"));
	Message->SetStringField(TEXT("character_id"), CharacterId);
	Message->SetStringField(TEXT("companion_id"), CompanionId);
	QueueMapJson(Message);
}

UMT2MessengerComponent* UMT2ServerRuntimeSubsystem::FindLocalMessenger(const FString& CharacterId) const
{
	UWorld* World = GetWorld();
	if (!World || CharacterId.IsEmpty())
	{
		return nullptr;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		AMT2PlayerState* State = It->Get() ? It->Get()->GetPlayerState<AMT2PlayerState>() : nullptr;
		const UMT2PersistenceComponent* Persistence = State ? State->GetPersistenceComponent() : nullptr;
		if (Persistence && Persistence->GetEntityId() == CharacterId)
		{
			return State->FindComponentByClass<UMT2MessengerComponent>();
		}
	}
	return nullptr;
}

bool UMT2ServerRuntimeSubsystem::HandleMapMessengerMessage(
	const FString& Type, const TSharedPtr<FJsonObject>& Message)
{
	if (!Type.StartsWith(TEXT("messenger_")))
	{
		return false;
	}
	// Every messenger push names the character it is for; it is only meaningful while that player is
	// still on this server (they may have travelled between the push and its arrival).
	UMT2MessengerComponent* Messenger =
		FindLocalMessenger(GetStringField(Message, TEXT("character_id")));
	if (!Messenger)
	{
		return true;
	}

	if (Type == TEXT("messenger_snapshot"))
	{
		TArray<FMT2FriendEntry> Friends;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Message->TryGetArrayField(TEXT("friends"), Values) && Values)
		{
			Friends.Reserve(Values->Num());
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
				if (!Object.IsValid())
				{
					continue;
				}
				FMT2FriendEntry Entry;
				Entry.CharacterId = GetStringField(Object, TEXT("character_id"));
				Entry.CharacterName = GetStringField(Object, TEXT("name"));
				Entry.bOnline = Object->GetBoolField(TEXT("online"));
				Entry.MapId = GetStringField(Object, TEXT("map_id"));
				Entry.Channel = GetIntField(Object, TEXT("channel"));
				Entry.Level = GetIntField(Object, TEXT("level"));
				Entry.UnreadCount = GetIntField(Object, TEXT("unread"));
				Friends.Add(MoveTemp(Entry));
			}
		}
		Messenger->ApplyFriendSnapshot(MoveTemp(Friends));
		return true;
	}
	if (Type == TEXT("messenger_presence"))
	{
		Messenger->ApplyPresenceUpdate(
			GetStringField(Message, TEXT("companion_id")),
			GetStringField(Message, TEXT("companion_name")),
			Message->GetBoolField(TEXT("online")),
			GetStringField(Message, TEXT("map_id")),
			GetIntField(Message, TEXT("channel")),
			GetIntField(Message, TEXT("level")));
		return true;
	}
	if (Type == TEXT("messenger_request"))
	{
		Messenger->ApplyIncomingRequest(
			GetStringField(Message, TEXT("from_id")), GetStringField(Message, TEXT("from_name")));
		return true;
	}
	if (Type == TEXT("messenger_message") || Type == TEXT("messenger_message_sent"))
	{
		const bool bSentByMe = Type == TEXT("messenger_message_sent");
		FMT2PrivateMessage Delivered;
		Delivered.MessageId = static_cast<int64>(Message->GetNumberField(TEXT("message_id")));
		Delivered.Body = GetStringField(Message, TEXT("body"));
		Delivered.SentUnixTime = static_cast<int64>(Message->GetNumberField(TEXT("sent_unix")));
		if (bSentByMe)
		{
			// The echo of the player's own line, so both halves of a conversation read alike.
			Delivered.SenderCharacterId = GetStringField(Message, TEXT("character_id"));
			Delivered.RecipientCharacterId = GetStringField(Message, TEXT("recipient_id"));
			Delivered.bRead = true;
		}
		else
		{
			Delivered.SenderCharacterId = GetStringField(Message, TEXT("sender_id"));
			Delivered.SenderName = GetStringField(Message, TEXT("sender_name"));
			Delivered.RecipientCharacterId = GetStringField(Message, TEXT("character_id"));
			Delivered.bWasOffline = Message->GetBoolField(TEXT("was_offline"));
		}
		Messenger->ApplyIncomingMessage(Delivered);
		return true;
	}
	if (Type == TEXT("messenger_conversation"))
	{
		TArray<FMT2PrivateMessage> Messages;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Message->TryGetArrayField(TEXT("messages"), Values) && Values)
		{
			Messages.Reserve(Values->Num());
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
				if (!Object.IsValid())
				{
					continue;
				}
				FMT2PrivateMessage Entry;
				Entry.MessageId = static_cast<int64>(Object->GetNumberField(TEXT("message_id")));
				Entry.SenderCharacterId = GetStringField(Object, TEXT("sender_id"));
				Entry.SenderName = GetStringField(Object, TEXT("sender_name"));
				Entry.Body = GetStringField(Object, TEXT("body"));
				Entry.SentUnixTime = static_cast<int64>(Object->GetNumberField(TEXT("sent_unix")));
				Entry.bRead = true;
				Messages.Add(MoveTemp(Entry));
			}
		}
		Messenger->ApplyConversation(GetStringField(Message, TEXT("companion_id")), MoveTemp(Messages));
		return true;
	}
	if (Type == TEXT("messenger_result"))
	{
		Messenger->ApplyActionResult(
			FName(*GetStringField(Message, TEXT("action"))),
			static_cast<EMT2MessengerResult>(GetIntField(Message, TEXT("result"))));
		return true;
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// Guilds
// -------------------------------------------------------------------------------------------------

TSharedRef<FJsonObject> UMT2ServerRuntimeSubsystem::WriteGuildSnapshot(const FMT2GuildSnapshot& Guild)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetNumberField(TEXT("guild_id"), Guild.GuildId);
	Object->SetStringField(TEXT("name"), Guild.Name);
	Object->SetStringField(TEXT("leader_id"), Guild.LeaderCharacterId);
	Object->SetNumberField(TEXT("level"), Guild.Level);
	Object->SetStringField(TEXT("experience"), LexToString(Guild.Experience));
	Object->SetStringField(TEXT("yang"), LexToString(Guild.Yang));
	Object->SetNumberField(TEXT("mark_revision"), static_cast<double>(Guild.MarkRevision));
	TArray<TSharedPtr<FJsonValue>> Ranks;
	for (const FMT2GuildRank& Rank : Guild.Ranks)
	{
		TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
		Value->SetNumberField(TEXT("rank"), Rank.Rank);
		Value->SetStringField(TEXT("name"), Rank.Name);
		Value->SetNumberField(TEXT("permissions"), Rank.Permissions);
		Ranks.Add(MakeShared<FJsonValueObject>(Value));
	}
	Object->SetArrayField(TEXT("ranks"), Ranks);
	TArray<TSharedPtr<FJsonValue>> Members;
	for (const FMT2GuildMember& Member : Guild.Members)
	{
		TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
		Value->SetStringField(TEXT("character_id"), Member.CharacterId);
		Value->SetStringField(TEXT("name"), Member.CharacterName);
		Value->SetNumberField(TEXT("rank"), Member.Rank);
		Value->SetNumberField(TEXT("level"), Member.Level);
		Value->SetStringField(TEXT("contribution"), LexToString(Member.ContributedExperience));
		Value->SetBoolField(TEXT("online"), Member.bOnline);
		Value->SetStringField(TEXT("map_id"), Member.MapId);
		Value->SetNumberField(TEXT("channel"), Member.Channel);
		Members.Add(MakeShared<FJsonValueObject>(Value));
	}
	Object->SetArrayField(TEXT("members"), Members);
	return Object;
}

bool UMT2ServerRuntimeSubsystem::ReadGuildSnapshot(
	const TSharedPtr<FJsonObject>& Object, FMT2GuildSnapshot& OutGuild)
{
	if (!Object.IsValid()) return false;
	OutGuild = {};
	OutGuild.GuildId = GetIntField(Object, TEXT("guild_id"));
	OutGuild.Name = GetStringField(Object, TEXT("name"));
	OutGuild.LeaderCharacterId = GetStringField(Object, TEXT("leader_id"));
	OutGuild.Level = GetIntField(Object, TEXT("level"), 1);
	OutGuild.Experience = GetInt64Field(Object, TEXT("experience"));
	OutGuild.Yang = GetInt64Field(Object, TEXT("yang"));
	OutGuild.MarkRevision = static_cast<int64>(Object->GetNumberField(TEXT("mark_revision")));
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (Object->TryGetArrayField(TEXT("ranks"), Values) && Values)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			const TSharedPtr<FJsonObject> Entry = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!Entry.IsValid()) continue;
			FMT2GuildRank& Rank = OutGuild.Ranks.AddDefaulted_GetRef();
			Rank.Rank = GetIntField(Entry, TEXT("rank"));
			Rank.Name = GetStringField(Entry, TEXT("name"));
			Rank.Permissions = GetIntField(Entry, TEXT("permissions"));
		}
	}
	Values = nullptr;
	if (Object->TryGetArrayField(TEXT("members"), Values) && Values)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			const TSharedPtr<FJsonObject> Entry = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!Entry.IsValid()) continue;
			FMT2GuildMember& Member = OutGuild.Members.AddDefaulted_GetRef();
			Member.CharacterId = GetStringField(Entry, TEXT("character_id"));
			Member.CharacterName = GetStringField(Entry, TEXT("name"));
			Member.Rank = GetIntField(Entry, TEXT("rank"), 15);
			Member.Level = GetIntField(Entry, TEXT("level"), 1);
			Member.ContributedExperience = GetInt64Field(Entry, TEXT("contribution"));
			Member.bOnline = Entry->GetBoolField(TEXT("online"));
			Member.MapId = GetStringField(Entry, TEXT("map_id"));
			Member.Channel = GetIntField(Entry, TEXT("channel"));
		}
	}
	return OutGuild.IsValid();
}

void UMT2ServerRuntimeSubsystem::SendGuildResult(
	const FString& CharacterId, const FString& Action, EMT2GuildResult Result)
{
	TSharedRef<FJsonObject> Reply = MakeMessage(TEXT("guild_result"));
	Reply->SetStringField(TEXT("action"), Action);
	Reply->SetNumberField(TEXT("result"), static_cast<uint8>(Result));
	SendToCharacter(CharacterId, Reply);
}

void UMT2ServerRuntimeSubsystem::SendGuildSnapshot(const FString& CharacterId)
{
	FMT2PersistenceBackend* Store = GetMessengerStore();
	if (!Store || CharacterId.IsEmpty()) return;
	FMT2GuildSnapshot Guild;
	if (!Store->LoadGuildForCharacter(CharacterId, Guild))
	{
		TSharedRef<FJsonObject> Clear = MakeMessage(TEXT("guild_snapshot"));
		Clear->SetObjectField(TEXT("guild"), MakeShared<FJsonObject>());
		SendToCharacter(CharacterId, Clear);
		return;
	}
	const FString MarkPath = FPaths::Combine(Config.DatabaseRoot, UMT2PathSettings::Path(TEXT("GuildMarkSubdirectory")), FString::Printf(TEXT("%d.png"), Guild.GuildId));
	if (IFileManager::Get().FileExists(*MarkPath)) Guild.MarkRevision = IFileManager::Get().GetTimeStamp(*MarkPath).ToUnixTimestamp();
	for (FMT2GuildMember& Member : Guild.Members)
	{
		if (const FMessengerPresence* Presence = MessengerPresences.Find(Member.CharacterId))
		{
			Member.bOnline = true;
			Member.MapId = Presence->MapId;
			Member.Channel = Presence->Channel;
			Member.Level = Presence->Level;
		}
	}
	TSharedRef<FJsonObject> Reply = MakeMessage(TEXT("guild_snapshot"));
	Reply->SetObjectField(TEXT("guild"), WriteGuildSnapshot(Guild));
	SendToCharacter(CharacterId, Reply);
}

void UMT2ServerRuntimeSubsystem::BroadcastGuildSnapshot(int32 GuildId)
{
	FMT2GuildSnapshot Guild;
	if (FMT2PersistenceBackend* Store = GetMessengerStore(); Store && Store->LoadGuild(GuildId, Guild))
	{
		for (const FMT2GuildMember& Member : Guild.Members) SendGuildSnapshot(Member.CharacterId);
	}
}

void UMT2ServerRuntimeSubsystem::HandleGuildLogin(
	FCoordinatorPeer&, const TSharedPtr<FJsonObject>& Message)
{
	SendGuildSnapshot(GetStringField(Message, TEXT("character_id")));
}

void UMT2ServerRuntimeSubsystem::HandleGuildCreate(
	FCoordinatorPeer&, const TSharedPtr<FJsonObject>& Message)
{
	const FString CharacterId = GetStringField(Message, TEXT("character_id"));
	FMT2GuildSnapshot Guild;
	const EMT2GuildResult Result = GetMessengerStore()
		? GetMessengerStore()->CreateGuild(CharacterId, GetStringField(Message, TEXT("name")), Guild)
		: EMT2GuildResult::Unavailable;
	SendGuildResult(CharacterId, TEXT("create"), Result);
	if (Result == EMT2GuildResult::Success) BroadcastGuildSnapshot(Guild.GuildId);
}

void UMT2ServerRuntimeSubsystem::HandleGuildInvite(
	FCoordinatorPeer&, const TSharedPtr<FJsonObject>& Message, double NowSeconds)
{
	const FString CharacterId = GetStringField(Message, TEXT("character_id"));
	const FString TargetId = GetStringField(Message, TEXT("target_id"));
	FMT2GuildSnapshot Guild;
	if (CharacterId == TargetId) { SendGuildResult(CharacterId, TEXT("invite"), EMT2GuildResult::CannotTargetSelf); return; }
	if (!GetMessengerStore() || !GetMessengerStore()->LoadGuildForCharacter(CharacterId, Guild))
		{ SendGuildResult(CharacterId, TEXT("invite"), EMT2GuildResult::NotInGuild); return; }
	const FMT2GuildMember* Sender = Guild.Members.FindByPredicate(
		[&](const FMT2GuildMember& M) { return M.CharacterId == CharacterId; });
	const FMT2GuildRank* Rank = Sender ? Guild.Ranks.FindByPredicate(
		[&](const FMT2GuildRank& R) { return R.Rank == Sender->Rank; }) : nullptr;
	if (!Rank || !(Rank->Permissions & static_cast<int32>(EMT2GuildPermission::InviteMembers)))
		{ SendGuildResult(CharacterId, TEXT("invite"), EMT2GuildResult::PermissionDenied); return; }
	FMT2GuildSnapshot Existing;
	if (GetMessengerStore()->LoadGuildForCharacter(TargetId, Existing))
		{ SendGuildResult(CharacterId, TEXT("invite"), EMT2GuildResult::AlreadyInGuild); return; }
	if (Guild.Members.Num() >= MT2Guild::MaximumMembers)
		{ SendGuildResult(CharacterId, TEXT("invite"), EMT2GuildResult::GuildFull); return; }
	if (!MessengerPresences.Contains(TargetId))
		{ SendGuildResult(CharacterId, TEXT("invite"), EMT2GuildResult::UnknownCharacter); return; }
	if (const FPendingGuildInvite* ExistingInvite = PendingGuildInvites.Find(TargetId);
		ExistingInvite && ExistingInvite->ExpiresAtSeconds > NowSeconds)
		{ SendGuildResult(CharacterId, TEXT("invite"), EMT2GuildResult::InvitePending); return; }
	PendingGuildInvites.Add(TargetId, {CharacterId, Guild.GuildId, NowSeconds + MT2Guild::InviteLifetimeSeconds});
	TSharedRef<FJsonObject> Invite = MakeMessage(TEXT("guild_invite_push"));
	Invite->SetStringField(TEXT("inviter_id"), CharacterId);
	Invite->SetStringField(TEXT("inviter_name"), Sender ? Sender->CharacterName : FString());
	Invite->SetStringField(TEXT("guild_name"), Guild.Name);
	SendToCharacter(TargetId, Invite);
	SendGuildResult(CharacterId, TEXT("invite"), EMT2GuildResult::Success);
}

void UMT2ServerRuntimeSubsystem::HandleGuildAnswer(
	FCoordinatorPeer&, const TSharedPtr<FJsonObject>& Message, double NowSeconds)
{
	const FString CharacterId = GetStringField(Message, TEXT("character_id"));
	const FString InviterId = GetStringField(Message, TEXT("inviter_id"));
	FPendingGuildInvite Invite;
	if (!PendingGuildInvites.RemoveAndCopyValue(CharacterId, Invite) || Invite.InviterId != InviterId ||
		Invite.ExpiresAtSeconds < NowSeconds)
		{ SendGuildResult(CharacterId, TEXT("answer"), EMT2GuildResult::InviteExpired); return; }
	if (!Message->GetBoolField(TEXT("accept"))) return;
	const EMT2GuildResult Result = GetMessengerStore()
		? GetMessengerStore()->AddGuildMember(Invite.GuildId, CharacterId, 15)
		: EMT2GuildResult::Unavailable;
	SendGuildResult(CharacterId, TEXT("answer"), Result);
	SendGuildResult(InviterId, TEXT("invite_accept"), Result);
	if (Result == EMT2GuildResult::Success) BroadcastGuildSnapshot(Invite.GuildId);
}

void UMT2ServerRuntimeSubsystem::HandleGuildAction(
	FCoordinatorPeer&, const TSharedPtr<FJsonObject>& Message)
{
	const FString CharacterId = GetStringField(Message, TEXT("character_id"));
	const FString Action = GetStringField(Message, TEXT("action"));
	const FString TargetId = GetStringField(Message, TEXT("target_id"));
	FMT2GuildSnapshot Guild;
	FMT2PersistenceBackend* Store = GetMessengerStore();
	if (!Store || !Store->LoadGuildForCharacter(CharacterId, Guild))
		{ SendGuildResult(CharacterId, Action, EMT2GuildResult::NotInGuild); return; }
	const bool bLeader = Guild.LeaderCharacterId == CharacterId;
	const FMT2GuildMember* Caller = Guild.Members.FindByPredicate(
		[&](const FMT2GuildMember& M) { return M.CharacterId == CharacterId; });
	const FMT2GuildRank* CallerRank = Caller ? Guild.Ranks.FindByPredicate(
		[&](const FMT2GuildRank& R) { return R.Rank == Caller->Rank; }) : nullptr;
	EMT2GuildResult Result = EMT2GuildResult::PermissionDenied;
	if (Action == TEXT("leave"))
	{
		Result = bLeader ? EMT2GuildResult::LeaderCannotLeave : Store->RemoveGuildMember(Guild.GuildId, CharacterId);
	}
	else if (Action == TEXT("disband") && bLeader) Result = Store->DisbandGuild(Guild.GuildId);
	else if (Action == TEXT("remove") && TargetId != Guild.LeaderCharacterId && CallerRank &&
		(CallerRank->Permissions & static_cast<int32>(EMT2GuildPermission::RemoveMembers)))
		Result = Store->RemoveGuildMember(Guild.GuildId, TargetId);
	else if (Action == TEXT("member_rank") && bLeader && TargetId != Guild.LeaderCharacterId)
		Result = Store->SetGuildMemberRank(Guild.GuildId, TargetId, GetIntField(Message, TEXT("rank")));
	else if (Action == TEXT("rank") && bLeader)
		Result = Store->SetGuildRank(Guild.GuildId, GetIntField(Message, TEXT("rank")),
			GetStringField(Message, TEXT("text")), GetIntField(Message, TEXT("flags")));
	SendGuildResult(CharacterId, Action, Result);
	if (Result == EMT2GuildResult::Success)
	{
		if (!TargetId.IsEmpty()) SendGuildSnapshot(TargetId);
		if (Action == TEXT("disband"))
		{
			for (const FMT2GuildMember& Member : Guild.Members) SendGuildSnapshot(Member.CharacterId);
		}
		else BroadcastGuildSnapshot(Guild.GuildId);
	}
}

void UMT2ServerRuntimeSubsystem::HandleGuildChat(
	FCoordinatorPeer&, const TSharedPtr<FJsonObject>& Message)
{
	const FString CharacterId = GetStringField(Message, TEXT("character_id"));
	FString Body = GetStringField(Message, TEXT("message")).TrimStartAndEnd().Left(256);
	FMT2GuildSnapshot Guild;
	if (Body.IsEmpty() || !GetMessengerStore() || !GetMessengerStore()->LoadGuildForCharacter(CharacterId, Guild)) return;
	TSharedRef<FJsonObject> Delivery = MakeMessage(TEXT("guild_chat_push"));
	Delivery->SetStringField(TEXT("sender"), GetStringField(Message, TEXT("sender")));
	Delivery->SetStringField(TEXT("message"), Body);
	for (const FMT2GuildMember& Member : Guild.Members) SendToCharacter(Member.CharacterId, Delivery);
}

void UMT2ServerRuntimeSubsystem::HandleGuildMarkUpload(
	FCoordinatorPeer&, const TSharedPtr<FJsonObject>& Message)
{
	const FString CharacterId = GetStringField(Message, TEXT("character_id"));
	FMT2GuildSnapshot Guild;
	FMT2PersistenceBackend* Store = GetMessengerStore();
	if (!Store || !Store->LoadGuildForCharacter(CharacterId, Guild) || Guild.LeaderCharacterId != CharacterId)
	{
		SendGuildResult(CharacterId, TEXT("mark"), EMT2GuildResult::PermissionDenied);
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (const double* Previous = GuildMarkUploadStamps.Find(Guild.GuildId); Previous && Now - *Previous < 30.0)
	{
		SendGuildResult(CharacterId, TEXT("mark"), EMT2GuildResult::MarkUploadTooSoon);
		return;
	}
	TArray<uint8> PngData;
	if (!FBase64::Decode(GetStringField(Message, TEXT("png")), PngData) || PngData.IsEmpty() || PngData.Num() > 64 * 1024)
	{
		SendGuildResult(CharacterId, TEXT("mark"), EMT2GuildResult::InvalidMark);
		return;
	}
	FImage Image;
	IImageWrapperModule& Wrapper = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	if (!Wrapper.DecompressImage(PngData.GetData(), PngData.Num(), Image) || Image.SizeX != 64 || Image.SizeY != 48)
	{
		SendGuildResult(CharacterId, TEXT("mark"), EMT2GuildResult::InvalidMark);
		return;
	}
	const FString Directory = FPaths::Combine(Config.DatabaseRoot, UMT2PathSettings::Path(TEXT("GuildMarkSubdirectory")));
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString MarkPath = FPaths::Combine(Directory, FString::Printf(TEXT("%d.png"), Guild.GuildId));
	if (!FFileHelper::SaveArrayToFile(PngData, *MarkPath))
	{
		SendGuildResult(CharacterId, TEXT("mark"), EMT2GuildResult::Unavailable);
		return;
	}
	GuildMarkUploadStamps.Add(Guild.GuildId, Now);
	const int64 Revision = IFileManager::Get().GetTimeStamp(*MarkPath).ToUnixTimestamp();
	for (const TPair<FString, FMessengerPresence>& Presence : MessengerPresences)
	{
		TSharedRef<FJsonObject> Push = MakeMessage(TEXT("guild_mark_data"));
		Push->SetNumberField(TEXT("guild_id"), Guild.GuildId);
		Push->SetNumberField(TEXT("revision"), static_cast<double>(Revision));
		Push->SetStringField(TEXT("png"), FBase64::Encode(PngData));
		SendToCharacter(Presence.Key, Push);
	}
	SendGuildResult(CharacterId, TEXT("mark"), EMT2GuildResult::Success);
	BroadcastGuildSnapshot(Guild.GuildId);
}

void UMT2ServerRuntimeSubsystem::HandleGuildMarkRequest(
	FCoordinatorPeer&, const TSharedPtr<FJsonObject>& Message)
{
	const FString CharacterId = GetStringField(Message, TEXT("character_id"));
	const int32 GuildId = GetIntField(Message, TEXT("guild_id"));
	if (GuildId <= 0 || CharacterId.IsEmpty()) return;
	const FString MarkPath = FPaths::Combine(Config.DatabaseRoot, UMT2PathSettings::Path(TEXT("GuildMarkSubdirectory")), FString::Printf(TEXT("%d.png"), GuildId));
	TArray<uint8> PngData;
	if (!FFileHelper::LoadFileToArray(PngData, *MarkPath) || PngData.Num() > 64 * 1024) return;
	TSharedRef<FJsonObject> Reply = MakeMessage(TEXT("guild_mark_data"));
	Reply->SetNumberField(TEXT("guild_id"), GuildId);
	Reply->SetNumberField(TEXT("revision"), IFileManager::Get().GetTimeStamp(*MarkPath).ToUnixTimestamp());
	Reply->SetStringField(TEXT("png"), FBase64::Encode(PngData));
	SendToCharacter(CharacterId, Reply);
}

void UMT2ServerRuntimeSubsystem::PublishGuildLogin(const FString& CharacterId)
{
	if (!Config.IsMapServer() || !bCoordinatorAuthenticated || CharacterId.IsEmpty()) return;
	TSharedRef<FJsonObject> Message = MakeMessage(TEXT("guild_login"));
	Message->SetStringField(TEXT("character_id"), CharacterId); QueueMapJson(Message);
}

void UMT2ServerRuntimeSubsystem::PublishGuildCreate(const FString& CharacterId, const FString& GuildName)
{
	if (!Config.IsMapServer() || !bCoordinatorAuthenticated) return;
	TSharedRef<FJsonObject> Message = MakeMessage(TEXT("guild_create"));
	Message->SetStringField(TEXT("character_id"), CharacterId);
	Message->SetStringField(TEXT("name"), GuildName); QueueMapJson(Message);
}

void UMT2ServerRuntimeSubsystem::PublishGuildInvite(const FString& CharacterId, const FString& TargetCharacterId)
{
	if (!Config.IsMapServer() || !bCoordinatorAuthenticated) return;
	TSharedRef<FJsonObject> Message = MakeMessage(TEXT("guild_invite"));
	Message->SetStringField(TEXT("character_id"), CharacterId);
	Message->SetStringField(TEXT("target_id"), TargetCharacterId); QueueMapJson(Message);
}

void UMT2ServerRuntimeSubsystem::PublishGuildAnswer(
	const FString& CharacterId, const FString& InviterId, bool bAccept)
{
	if (!Config.IsMapServer() || !bCoordinatorAuthenticated) return;
	TSharedRef<FJsonObject> Message = MakeMessage(TEXT("guild_answer"));
	Message->SetStringField(TEXT("character_id"), CharacterId);
	Message->SetStringField(TEXT("inviter_id"), InviterId);
	Message->SetBoolField(TEXT("accept"), bAccept); QueueMapJson(Message);
}

void UMT2ServerRuntimeSubsystem::PublishGuildAction(
	const FString& CharacterId, FName Action, const FString& TargetId,
	int32 Rank, const FString& Text, int32 Flags)
{
	if (!Config.IsMapServer() || !bCoordinatorAuthenticated) return;
	TSharedRef<FJsonObject> Message = MakeMessage(TEXT("guild_action"));
	Message->SetStringField(TEXT("character_id"), CharacterId);
	Message->SetStringField(TEXT("action"), Action.ToString());
	Message->SetStringField(TEXT("target_id"), TargetId);
	Message->SetNumberField(TEXT("rank"), Rank);
	Message->SetStringField(TEXT("text"), Text);
	Message->SetNumberField(TEXT("flags"), Flags); QueueMapJson(Message);
}

void UMT2ServerRuntimeSubsystem::PublishGuildChat(
	const FString& CharacterId, const FString& SenderName, const FString& MessageText)
{
	if (!Config.IsMapServer() || !bCoordinatorAuthenticated) return;
	TSharedRef<FJsonObject> Message = MakeMessage(TEXT("guild_chat"));
	Message->SetStringField(TEXT("character_id"), CharacterId);
	Message->SetStringField(TEXT("sender"), SenderName);
	Message->SetStringField(TEXT("message"), MessageText); QueueMapJson(Message);
}

void UMT2ServerRuntimeSubsystem::PublishGuildMarkUpload(const FString& CharacterId, const TArray<uint8>& PngData)
{
	if (!Config.IsMapServer() || !bCoordinatorAuthenticated || CharacterId.IsEmpty() || PngData.Num() > 64 * 1024) return;
	TSharedRef<FJsonObject> Message = MakeMessage(TEXT("guild_mark_upload"));
	Message->SetStringField(TEXT("character_id"), CharacterId);
	Message->SetStringField(TEXT("png"), FBase64::Encode(PngData));
	QueueMapJson(Message);
}

void UMT2ServerRuntimeSubsystem::PublishGuildMarkRequest(const FString& CharacterId, int32 GuildId, int64 KnownRevision)
{
	if (!Config.IsMapServer() || !bCoordinatorAuthenticated || CharacterId.IsEmpty() || GuildId <= 0) return;
	TSharedRef<FJsonObject> Message = MakeMessage(TEXT("guild_mark_request"));
	Message->SetStringField(TEXT("character_id"), CharacterId);
	Message->SetNumberField(TEXT("guild_id"), GuildId);
	Message->SetNumberField(TEXT("known_revision"), static_cast<double>(KnownRevision));
	QueueMapJson(Message);
}

UMT2GuildComponent* UMT2ServerRuntimeSubsystem::FindLocalGuild(const FString& CharacterId) const
{
	if (!GetWorld()) return nullptr;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		AMT2PlayerState* State = It->Get() ? It->Get()->GetPlayerState<AMT2PlayerState>() : nullptr;
		const UMT2PersistenceComponent* Persistence = State ? State->GetPersistenceComponent() : nullptr;
		if (Persistence && Persistence->GetEntityId() == CharacterId) return State->FindComponentByClass<UMT2GuildComponent>();
	}
	return nullptr;
}

bool UMT2ServerRuntimeSubsystem::HandleMapGuildMessage(
	const FString& Type, const TSharedPtr<FJsonObject>& Message)
{
	if (!Type.StartsWith(TEXT("guild_"))) return false;
	UMT2GuildComponent* Guild = FindLocalGuild(GetStringField(Message, TEXT("character_id")));
	if (!Guild) return true;
	if (Type == TEXT("guild_snapshot"))
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		FMT2GuildSnapshot Snapshot;
		if (Message->TryGetObjectField(TEXT("guild"), Object) && Object) ReadGuildSnapshot(*Object, Snapshot);
		Guild->ApplySnapshot(MoveTemp(Snapshot));
	}
	else if (Type == TEXT("guild_invite_push")) Guild->ApplyInvite(
		GetStringField(Message, TEXT("inviter_id")), GetStringField(Message, TEXT("inviter_name")),
		GetStringField(Message, TEXT("guild_name")));
	else if (Type == TEXT("guild_result")) Guild->ApplyResult(
		FName(*GetStringField(Message, TEXT("action"))), static_cast<EMT2GuildResult>(GetIntField(Message, TEXT("result"))));
	else if (Type == TEXT("guild_chat_push")) Guild->ApplyChat(
		GetStringField(Message, TEXT("sender")), GetStringField(Message, TEXT("message")));
	else if (Type == TEXT("guild_mark_data"))
	{
		TArray<uint8> PngData;
		if (FBase64::Decode(GetStringField(Message, TEXT("png")), PngData))
			Guild->ApplyGuildMark(GetIntField(Message, TEXT("guild_id")),
				static_cast<int64>(Message->GetNumberField(TEXT("revision"))), PngData);
	}
	return true;
}
