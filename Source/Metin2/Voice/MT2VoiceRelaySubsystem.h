/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Containers/Queue.h"
#include "CoreMinimal.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Subsystems/WorldSubsystem.h"
#include "MT2VoiceRelaySubsystem.generated.h"

class FSocket;
class FUdpSocketReceiver;

// Server half of the self-hosted proximity voice chat: a bare UDP relay. Clients register their
// endpoint with a Hello heartbeat; every Audio datagram from a registered speaker is forwarded
// VERBATIM (no decode, no re-encode, no encryption) to every player whose pawn the player spatial
// grid finds within voice range of the speaker. The relay never touches the audio itself, so its
// per-packet cost is one grid query plus N sendto's.
UCLASS()
class METIN2_API UMT2VoiceRelaySubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	struct FClientEndpoint
	{
		FIPv4Endpoint Endpoint;
		double LastHeardTime = 0.0;
	};

	struct FReceivedDatagram
	{
		FIPv4Endpoint Sender;
		TArray<uint8> Data;
	};

	void OpenSocket();
	void CloseSocket();
	void ProcessDatagram(const FReceivedDatagram& Datagram);
	class APawn* FindPawnByPlayerId(int32 PlayerId) const;

	FSocket* Socket = nullptr;
	FUdpSocketReceiver* Receiver = nullptr;
	// Filled on the receiver thread, drained on the game thread (pawn/grid access needs it).
	TQueue<FReceivedDatagram, EQueueMode::Mpsc> PendingDatagrams;

	TMap<int32, FClientEndpoint> EndpointsByPlayerId;
	double NextPruneTime = 0.0;
};
