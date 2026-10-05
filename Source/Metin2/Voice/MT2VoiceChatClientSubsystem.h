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
#include "UObject/StrongObjectPtr.h"
#include "Voice/MT2VoiceTypes.h"
#include "MT2VoiceChatClientSubsystem.generated.h"

class FSocket;
class FUdpSocketReceiver;
class IVoiceCapture;
class IVoiceDecoder;
class IVoiceEncoder;
class UAudioComponent;
class USoundWaveProcedural;

// Client half of the self-hosted proximity voice chat. Push-to-talk (V) drives the microphone:
// captured PCM is Opus-encoded and sent as sequence-numbered UDP datagrams to the server relay,
// which forwards them to nearby players. Incoming speaker streams each get a small jitter buffer
// (prebuffer + reorder); a packet that never arrives is simply skipped, so the procedural sound
// wave underruns and plays SILENCE for that span - no stretching, no repeats. Streams are not
// synchronised across clients; latency is accepted by design. Playback is spatialised on the
// speaker's pawn so voices sit in the world and fade with distance.
UCLASS()
class METIN2_API UMT2VoiceChatClientSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	// Push-to-talk. Bound to the V key in AMT2PlayerController.
	UFUNCTION(BlueprintCallable, Category = "Voice")
	void StartTalking();

	UFUNCTION(BlueprintCallable, Category = "Voice")
	void StopTalking();

	UFUNCTION(BlueprintPure, Category = "Voice")
	bool IsTalking() const { return bTalking; }

	// True while that player's voice is audible here: the local player while transmitting, or a
	// remote speaker whose packets arrived within the last half second. Drives the nameplate icon.
	UFUNCTION(BlueprintPure, Category = "Voice")
	bool IsPlayerSpeaking(int32 PlayerId) const;

	// Drops the lazily-created capture/encoder so the next talk recreates them against the
	// currently configured input device (FMT2AudioUserSettings::GetVoiceInputDevice).
	void ResetCaptureDevice();

private:
	void ApplyVoicePlaybackVolume();
	// One remote speaker's incoming stream.
	struct FSpeakerStream
	{
		TSharedPtr<IVoiceDecoder> Decoder;
		TStrongObjectPtr<USoundWaveProcedural> Wave;
		TWeakObjectPtr<UAudioComponent> Audio;
		// Reorder buffer: packets waiting for their turn, keyed by sequence.
		TMap<uint32, MT2Voice::FPacket> PendingBySeq;
		uint32 NextSequence = 0;
		bool bStarted = false;
		// True while collecting the initial jitter buffer before playout begins (and again after a
		// drought, so each talk burst starts smooth).
		bool bPrebuffering = true;
		double GapWaitStartTime = 0.0;
		double LastPacketTime = 0.0;
	};

	struct FReceivedDatagram
	{
		TArray<uint8> Data;
	};

	bool EnsureNetworkReady();
	bool ResolveRelayEndpoint();
	void SendPacket(const MT2Voice::FPacket& Packet);
	void TickCapture();
	void TickReceive();
	void TickSpeakerStream(int32 SpeakerId, FSpeakerStream& Stream, double Now);
	void EnsurePlayback(int32 SpeakerId, FSpeakerStream& Stream);
	class APawn* FindPawnByPlayerId(int32 PlayerId) const;
	int32 GetLocalPlayerId() const;
	void CloseSocket();

	FSocket* Socket = nullptr;
	FUdpSocketReceiver* Receiver = nullptr;
	TQueue<FReceivedDatagram, EQueueMode::Mpsc> PendingDatagrams;
	FIPv4Endpoint RelayEndpoint;
	bool bRelayResolved = false;
	double NextHelloTime = 0.0;

	// Capture / encode (created lazily on first talk).
	TSharedPtr<IVoiceCapture> VoiceCapture;
	TSharedPtr<IVoiceEncoder> VoiceEncoder;
	TArray<uint8> CaptureAccumulator;
	bool bTalking = false;
	bool bCaptureUnavailableLogged = false;
	uint32 OutgoingSequence = 0;

	TMap<int32, FSpeakerStream> SpeakerStreams;
};
