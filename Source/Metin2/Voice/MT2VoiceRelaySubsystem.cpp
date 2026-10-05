/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Voice/MT2VoiceRelaySubsystem.h"

#include "Common/UdpSocketBuilder.h"
#include "Common/UdpSocketReceiver.h"
#include "Config/MT2GameplaySettings.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Server/MT2ServerRuntimeSubsystem.h"
#include "Voice/MT2VoiceTypes.h"
#include "World/MT2PlayerSpatialGridSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogMT2Voice, Log, All);

void UMT2VoiceRelaySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// The player grid is what routes packets; make sure it exists before we do.
	Collection.InitializeDependency<UMT2PlayerSpatialGridSubsystem>();
}

void UMT2VoiceRelaySubsystem::Deinitialize()
{
	CloseSocket();
	Super::Deinitialize();
}

TStatId UMT2VoiceRelaySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMT2VoiceRelaySubsystem, STATGROUP_Tickables);
}

void UMT2VoiceRelaySubsystem::OpenSocket()
{
	// Game port + offset, so every server process on a machine gets a distinct voice port.
	const UWorld* World = GetWorld();
	const int32 GamePort = World ? World->URL.Port : 0;
	int32 Port = GamePort > 0
		? GamePort + MT2Voice::VoicePortOffset
		: UMT2GameplaySettings::Get().VoiceChatPort;
	FParse::Value(FCommandLine::Get(), TEXT("voice_port="), Port);
	Port = FMath::Clamp(Port, 1, 65535);
	Socket = FUdpSocketBuilder(TEXT("MT2VoiceRelay"))
		.AsNonBlocking()
		.AsReusable()
		.BoundToAddress(FIPv4Address::Any)
		.BoundToPort(Port)
		.WithReceiveBufferSize(256 * 1024)
		.WithSendBufferSize(256 * 1024)
		.Build();
	if (!Socket)
	{
		UE_LOG(LogMT2Voice, Warning,
			TEXT("[VoiceRelay] Could not bind UDP port %d; proximity voice is disabled on this server."), Port);
		return;
	}

	Receiver = new FUdpSocketReceiver(Socket, FTimespan::FromMilliseconds(10), TEXT("MT2VoiceRelayReceiver"));
	Receiver->OnDataReceived().BindLambda(
		[this](const FArrayReaderPtr& Reader, const FIPv4Endpoint& Sender)
		{
			// Receiver thread: just queue the bytes; all game state is touched on the game thread.
			FReceivedDatagram Datagram;
			Datagram.Sender = Sender;
			Datagram.Data.Append(Reader->GetData(), Reader->Num());
			PendingDatagrams.Enqueue(MoveTemp(Datagram));
		});
	Receiver->Start();
	UE_LOG(LogMT2Voice, Display, TEXT("[VoiceRelay] Listening on UDP port %d."), Port);
}

void UMT2VoiceRelaySubsystem::CloseSocket()
{
	if (Receiver)
	{
		Receiver->Stop();
		delete Receiver;
		Receiver = nullptr;
	}
	if (Socket)
	{
		Socket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		Socket = nullptr;
	}
	PendingDatagrams.Empty();
	EndpointsByPlayerId.Reset();
}

void UMT2VoiceRelaySubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	// The relay runs wherever the authoritative world lives: dedicated server, listen server, or
	// standalone (harmless there - a lone player has nobody in range).
	if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_Client)
	{
		return;
	}
	const UMT2ServerRuntimeSubsystem* Runtime = World->GetGameInstance()
		? World->GetGameInstance()->GetSubsystem<UMT2ServerRuntimeSubsystem>() : nullptr;
	if (Runtime && (Runtime->IsGateway() || Runtime->IsCoordinator()))
	{
		// Gateway and coordinator worlds do not host spatial gameplay or player pawns.
		CloseSocket();
		return;
	}
	if (!Socket)
	{
		OpenSocket();
		if (!Socket)
		{
			return;
		}
	}

	FReceivedDatagram Datagram;
	while (PendingDatagrams.Dequeue(Datagram))
	{
		ProcessDatagram(Datagram);
	}

	// Drop endpoints that stopped heartbeating (disconnected clients).
	const double Now = World->GetRealTimeSeconds();
	if (Now >= NextPruneTime)
	{
		NextPruneTime = Now + 5.0;
		for (auto It = EndpointsByPlayerId.CreateIterator(); It; ++It)
		{
			if (Now - It->Value.LastHeardTime > 10.0)
			{
				It.RemoveCurrent();
			}
		}
	}
}

void UMT2VoiceRelaySubsystem::ProcessDatagram(const FReceivedDatagram& Datagram)
{
	MT2Voice::FPacket Packet;
	if (!MT2Voice::FPacket::Deserialize(Datagram.Data.GetData(), Datagram.Data.Num(), Packet))
	{
		return;
	}
	UWorld* World = GetWorld();
	const double Now = World->GetRealTimeSeconds();

	if (Packet.Type == MT2Voice::EPacketType::Hello)
	{
		FClientEndpoint& Entry = EndpointsByPlayerId.FindOrAdd(Packet.SpeakerId);
		Entry.Endpoint = Datagram.Sender;
		Entry.LastHeardTime = Now;
		return;
	}
	if (Packet.Type != MT2Voice::EPacketType::Audio)
	{
		return;
	}

	// Audio is only accepted from the endpoint that the speaker id registered with - no crypto by
	// design, but this keeps one client from casually talking as another.
	FClientEndpoint* Speaker = EndpointsByPlayerId.Find(Packet.SpeakerId);
	if (!Speaker || Speaker->Endpoint != Datagram.Sender)
	{
		return;
	}
	Speaker->LastHeardTime = Now;

	const APawn* SpeakerPawn = FindPawnByPlayerId(Packet.SpeakerId);
	if (!SpeakerPawn)
	{
		return;
	}

	UMT2PlayerSpatialGridSubsystem* Grid = World->GetSubsystem<UMT2PlayerSpatialGridSubsystem>();
	if (!Grid)
	{
		return;
	}

	// The grid narrows the fan-out to the speaker's neighborhood, so voice traffic scales with
	// local density instead of server population.
	TArray<APawn*> NearbyPawns;
	Grid->GetPlayerPawnsInRadius(
		SpeakerPawn->GetActorLocation(), UMT2GameplaySettings::Get().VoiceChatRange, NearbyPawns);

	for (const APawn* Pawn : NearbyPawns)
	{
		const APlayerState* State = Pawn ? Pawn->GetPlayerState() : nullptr;
		if (!State || State->GetPlayerId() == Packet.SpeakerId)
		{
			continue;
		}
		const FClientEndpoint* Listener = EndpointsByPlayerId.Find(State->GetPlayerId());
		if (!Listener)
		{
			continue;
		}
		// Forward the original datagram untouched.
		int32 BytesSent = 0;
		Socket->SendTo(
			Datagram.Data.GetData(), Datagram.Data.Num(), BytesSent, *Listener->Endpoint.ToInternetAddr());
	}
}

APawn* UMT2VoiceRelaySubsystem::FindPawnByPlayerId(int32 PlayerId) const
{
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!GameState)
	{
		return nullptr;
	}
	for (APlayerState* State : GameState->PlayerArray)
	{
		if (State && State->GetPlayerId() == PlayerId)
		{
			return State->GetPawn();
		}
	}
	return nullptr;
}
