/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Voice/MT2VoiceChatClientSubsystem.h"

#include "Audio/MT2AudioUserSettings.h"
#include "Common/UdpSocketBuilder.h"
#include "Common/UdpSocketReceiver.h"
#include "Components/AudioComponent.h"
#include "Config/MT2GameplaySettings.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Interfaces/VoiceCapture.h"
#include "Interfaces/VoiceCodec.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundWaveProcedural.h"
#include "Modules/ModuleManager.h"
#include "VoiceModule.h"

DEFINE_LOG_CATEGORY_STATIC(LogMT2VoiceClient, Log, All);

namespace
{
	// One 20ms Opus frame at our capture format; encode input must stay frame-aligned.
	constexpr int32 OpusFrameBytes = MT2Voice::SampleRate / 50 * MT2Voice::NumChannels * MT2Voice::BytesPerSample;
	// UE's Opus wrapper requires six frames of unused capacity beyond the current write cursor,
	// even when the packet itself contains fewer frames. An exactly-sized output buffer decodes to 0.
	constexpr int32 OpusDecoderSafetyBytes = 6 * OpusFrameBytes;
	// Fade playback in/out slightly so a stream (re)start never clicks.
	constexpr float PlaybackFadeSeconds = 0.03f;
	// A pending gap younger than this may still be filled by a late packet; older ones are dropped.
	constexpr double SpeakerIdleTimeoutSeconds = 10.0;
}

void UMT2VoiceChatClientSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FMT2AudioUserSettings::OnChanged.AddWeakLambda(this, [this] { ApplyVoicePlaybackVolume(); });
}

void UMT2VoiceChatClientSubsystem::Deinitialize()
{
	FMT2AudioUserSettings::OnChanged.RemoveAll(this);
	StopTalking();
	VoiceCapture.Reset();
	VoiceEncoder.Reset();
	for (TPair<int32, FSpeakerStream>& Pair : SpeakerStreams)
	{
		if (UAudioComponent* Audio = Pair.Value.Audio.Get())
		{
			Audio->Stop();
		}
	}
	SpeakerStreams.Reset();
	CloseSocket();
	Super::Deinitialize();
}

TStatId UMT2VoiceChatClientSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMT2VoiceChatClientSubsystem, STATGROUP_Tickables);
}

void UMT2VoiceChatClientSubsystem::CloseSocket()
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
	bRelayResolved = false;
}

bool UMT2VoiceChatClientSubsystem::ResolveRelayEndpoint()
{
	if (bRelayResolved)
	{
		return true;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	// The voice relay lives on the same host as the game server, on its own UDP port. A connected
	// client reads the host from its net connection; a listen server or standalone player IS the
	// host, so loopback.
	FString Host = TEXT("127.0.0.1");
	// The relay listens on the server's game port + offset; derive it from the same connection
	// the game traffic uses (or this world's own listen port when we ARE the host).
	int32 VoicePortOffset = MT2Voice::VoicePortOffset;
	FParse::Value(FCommandLine::Get(), TEXT("voice_port_offset="), VoicePortOffset);
	VoicePortOffset = FMath::Clamp(VoicePortOffset, -65534, 65534);
	int32 RelayPort = UMT2GameplaySettings::Get().VoiceChatPort;
	if (const UNetDriver* Driver = World->GetNetDriver(); Driver && Driver->ServerConnection)
	{
		Host = Driver->ServerConnection->URL.Host;
		if (Driver->ServerConnection->URL.Port > 0)
		{
			RelayPort = Driver->ServerConnection->URL.Port + VoicePortOffset;
		}
	}
	else if (World->URL.Port > 0)
	{
		RelayPort = World->URL.Port + VoicePortOffset;
	}
	if (RelayPort < 1 || RelayPort > 65535)
	{
		return false;
	}

	FIPv4Address Address;
	if (!FIPv4Address::Parse(Host, Address))
	{
		// Hostname rather than IP: one blocking DNS lookup at connect time is acceptable.
		ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		const FAddressInfoResult Result =
			Sockets->GetAddressInfo(*Host, nullptr, EAddressInfoFlags::Default, NAME_None);
		if (Result.ReturnCode != SE_NO_ERROR || Result.Results.Num() == 0)
		{
			return false;
		}
		uint32 ResolvedIp = 0;
		Result.Results[0].Address->GetIp(ResolvedIp);
		if (ResolvedIp == 0)
		{
			return false;
		}
		Address = FIPv4Address(ResolvedIp);
	}
	RelayEndpoint = FIPv4Endpoint(Address, static_cast<uint16>(RelayPort));
	bRelayResolved = true;
	return true;
}

bool UMT2VoiceChatClientSubsystem::EnsureNetworkReady()
{
	if (Socket)
	{
		return true;
	}
	if (!ResolveRelayEndpoint())
	{
		return false;
	}
	Socket = FUdpSocketBuilder(TEXT("MT2VoiceClient"))
		.AsNonBlocking()
		.BoundToAddress(FIPv4Address::Any)
		.BoundToPort(0)   // ephemeral; the relay learns it from our Hello
		.WithReceiveBufferSize(256 * 1024)
		.Build();
	if (!Socket)
	{
		return false;
	}
	Receiver = new FUdpSocketReceiver(Socket, FTimespan::FromMilliseconds(10), TEXT("MT2VoiceClientReceiver"));
	Receiver->OnDataReceived().BindLambda(
		[this](const FArrayReaderPtr& Reader, const FIPv4Endpoint& Sender)
		{
			FReceivedDatagram Datagram;
			Datagram.Data.Append(Reader->GetData(), Reader->Num());
			PendingDatagrams.Enqueue(MoveTemp(Datagram));
		});
	Receiver->Start();
	UE_LOG(LogMT2VoiceClient, Display, TEXT("[Voice] Client ready; relay at %s."),
		*RelayEndpoint.ToString());
	return true;
}

int32 UMT2VoiceChatClientSubsystem::GetLocalPlayerId() const
{
	const APlayerController* Controller = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	const APlayerState* State = Controller ? Controller->PlayerState : nullptr;
	return State ? State->GetPlayerId() : INDEX_NONE;
}

void UMT2VoiceChatClientSubsystem::SendPacket(const MT2Voice::FPacket& Packet)
{
	if (!Socket)
	{
		return;
	}
	TArray<uint8> Bytes;
	Packet.Serialize(Bytes);
	int32 BytesSent = 0;
	Socket->SendTo(Bytes.GetData(), Bytes.Num(), BytesSent, *RelayEndpoint.ToInternetAddr());
}

void UMT2VoiceChatClientSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const int32 LocalPlayerId = GetLocalPlayerId();
	if (LocalPlayerId == INDEX_NONE || !EnsureNetworkReady())
	{
		return;
	}

	// Heartbeat keeps our endpoint registered at the relay (and re-registers after any NAT rebind).
	const double Now = World->GetRealTimeSeconds();
	if (Now >= NextHelloTime)
	{
		NextHelloTime = Now + 2.0;
		MT2Voice::FPacket Hello;
		Hello.Type = MT2Voice::EPacketType::Hello;
		Hello.SpeakerId = LocalPlayerId;
		SendPacket(Hello);
	}

	if (bTalking)
	{
		TickCapture();
	}
	TickReceive();

	for (auto It = SpeakerStreams.CreateIterator(); It; ++It)
	{
		TickSpeakerStream(It->Key, It->Value, Now);
		if (Now - It->Value.LastPacketTime > SpeakerIdleTimeoutSeconds)
		{
			if (UAudioComponent* Audio = It->Value.Audio.Get())
			{
				Audio->FadeOut(PlaybackFadeSeconds, 0.0f);
			}
			It.RemoveCurrent();
		}
	}
}

bool UMT2VoiceChatClientSubsystem::IsPlayerSpeaking(int32 PlayerId) const
{
	if (PlayerId == INDEX_NONE)
	{
		return false;
	}
	if (PlayerId == GetLocalPlayerId())
	{
		return bTalking;
	}
	const FSpeakerStream* Stream = SpeakerStreams.Find(PlayerId);
	const UWorld* World = GetWorld();
	return Stream && World && (World->GetRealTimeSeconds() - Stream->LastPacketTime) < 0.5;
}

void UMT2VoiceChatClientSubsystem::StartTalking()
{
	if (bTalking)
	{
		return;
	}
	if (!VoiceCapture.IsValid())
	{
		// IsAvailable() only reports an ALREADY-loaded module and nothing else loads "Voice", so it
		// must be loaded explicitly here - gating on IsAvailable() made this a silent no-op.
		FVoiceModule* VoiceModule = FModuleManager::LoadModulePtr<FVoiceModule>(TEXT("Voice"));
		if (!VoiceModule)
		{
			UE_LOG(LogMT2VoiceClient, Warning, TEXT("[Voice] The engine 'Voice' module failed to load."));
			return;
		}
		VoiceCapture = VoiceModule->CreateVoiceCapture(
			FMT2AudioUserSettings::GetVoiceInputDevice(), MT2Voice::SampleRate, MT2Voice::NumChannels);
		VoiceEncoder = VoiceModule->CreateVoiceEncoder(
			MT2Voice::SampleRate, MT2Voice::NumChannels, EAudioEncodeHint::VoiceEncode_Voice);
		if (!VoiceCapture.IsValid() || !VoiceEncoder.IsValid())
		{
			if (!bCaptureUnavailableLogged)
			{
				bCaptureUnavailableLogged = true;
				UE_LOG(LogMT2VoiceClient, Warning,
					TEXT("[Voice] Microphone capture unavailable (capture=%s encoder=%s). Check a mic is connected and DefaultEngine.ini has [Voice] bEnabled=true."),
					VoiceCapture.IsValid() ? TEXT("ok") : TEXT("failed"),
					VoiceEncoder.IsValid() ? TEXT("ok") : TEXT("failed"));
			}
			VoiceCapture.Reset();
			VoiceEncoder.Reset();
			return;
		}
		if (!VoiceEncoder->SetBitrate(MT2Voice::EncoderBitRate))
		{
			UE_LOG(LogMT2VoiceClient, Warning, TEXT("[Voice] Could not set Opus bitrate to %d."),
				MT2Voice::EncoderBitRate);
		}
		if (!VoiceEncoder->SetComplexity(MT2Voice::EncoderComplexity))
		{
			UE_LOG(LogMT2VoiceClient, Warning, TEXT("[Voice] Could not set Opus complexity to %d."),
				MT2Voice::EncoderComplexity);
		}
		UE_LOG(LogMT2VoiceClient, Display, TEXT("[Voice] Microphone capture initialized (%d Hz)."),
			MT2Voice::SampleRate);
	}
	CaptureAccumulator.Reset();
	VoiceCapture->Start();
	bTalking = true;
	UE_LOG(LogMT2VoiceClient, Verbose, TEXT("[Voice] Transmitting."));
}

void UMT2VoiceChatClientSubsystem::ResetCaptureDevice()
{
	StopTalking();
	VoiceCapture.Reset();
	VoiceEncoder.Reset();
	bCaptureUnavailableLogged = false;
}

void UMT2VoiceChatClientSubsystem::ApplyVoicePlaybackVolume()
{
	const float Volume = FMT2AudioUserSettings::GetVoiceVolume();
	for (TPair<int32, FSpeakerStream>& Pair : SpeakerStreams)
	{
		if (UAudioComponent* Audio = Pair.Value.Audio.Get())
		{
			Audio->SetVolumeMultiplier(Volume);
		}
	}
}

void UMT2VoiceChatClientSubsystem::StopTalking()
{
	if (!bTalking)
	{
		return;
	}
	bTalking = false;
	if (VoiceCapture.IsValid())
	{
		VoiceCapture->Stop();
	}
	CaptureAccumulator.Reset();
}

void UMT2VoiceChatClientSubsystem::TickCapture()
{
	if (!VoiceCapture.IsValid() || !VoiceEncoder.IsValid())
	{
		return;
	}

	uint32 AvailableBytes = 0;
	const EVoiceCaptureState::Type CaptureState = VoiceCapture->GetCaptureState(AvailableBytes);
	if (CaptureState == EVoiceCaptureState::Ok && AvailableBytes > 0)
	{
		const int32 Offset = CaptureAccumulator.Num();
		CaptureAccumulator.SetNumUninitialized(Offset + AvailableBytes);
		uint64 SampleCounter = 0;
		uint32 BytesRead = 0;
		VoiceCapture->GetVoiceData(
			CaptureAccumulator.GetData() + Offset, AvailableBytes, BytesRead, SampleCounter);
		CaptureAccumulator.SetNum(Offset + BytesRead);
	}

	// Ship whatever full Opus frames we have, up to one packet's worth per send, so a lost datagram
	// never costs more than ~100ms of speech.
	while (CaptureAccumulator.Num() >= OpusFrameBytes)
	{
		const int32 FrameCount = FMath::Min(
			CaptureAccumulator.Num() / OpusFrameBytes, MT2Voice::MaxRawBytesPerPacket / OpusFrameBytes);
		const int32 RawBytes = FrameCount * OpusFrameBytes;

		uint8 Compressed[MT2Voice::MaxCompressedPayload];
		uint32 CompressedSize = MT2Voice::MaxCompressedPayload;
		VoiceEncoder->Encode(CaptureAccumulator.GetData(), RawBytes, Compressed, CompressedSize);
		CaptureAccumulator.RemoveAt(0, RawBytes, EAllowShrinking::No);
		if (CompressedSize == 0)
		{
			continue;
		}

		MT2Voice::FPacket Packet;
		Packet.Type = MT2Voice::EPacketType::Audio;
		Packet.SpeakerId = GetLocalPlayerId();
		Packet.Sequence = OutgoingSequence++;
		Packet.RawSize = static_cast<uint16>(RawBytes);
		Packet.Payload.Append(Compressed, CompressedSize);
		SendPacket(Packet);
	}
}

void UMT2VoiceChatClientSubsystem::TickReceive()
{
	const double Now = GetWorld()->GetRealTimeSeconds();
	FReceivedDatagram Datagram;
	while (PendingDatagrams.Dequeue(Datagram))
	{
		MT2Voice::FPacket Packet;
		if (!MT2Voice::FPacket::Deserialize(Datagram.Data.GetData(), Datagram.Data.Num(), Packet) ||
			Packet.Type != MT2Voice::EPacketType::Audio ||
			Packet.RawSize == 0 || Packet.RawSize > MT2Voice::MaxRawBytesPerPacket)
		{
			continue;
		}

		FSpeakerStream& Stream = SpeakerStreams.FindOrAdd(Packet.SpeakerId);
		Stream.LastPacketTime = Now;
		if (!Stream.bStarted)
		{
			Stream.bStarted = true;
			Stream.NextSequence = Packet.Sequence;
		}
		// Packets from before the playout cursor arrived too late - their span already played as
		// silence; dropping them (rather than inserting) is what keeps the stream glitch-free.
		if (Packet.Sequence < Stream.NextSequence)
		{
			continue;
		}
		Stream.PendingBySeq.Add(Packet.Sequence, MoveTemp(Packet));
	}
}

void UMT2VoiceChatClientSubsystem::TickSpeakerStream(int32 SpeakerId, FSpeakerStream& Stream, double Now)
{
	const UMT2GameplaySettings& Settings = UMT2GameplaySettings::Get();

	if (Stream.PendingBySeq.IsEmpty())
	{
		// Drought (speaker released the key, or a long loss): re-arm the prebuffer so the next
		// burst starts smooth instead of stuttering packet-by-packet.
		if (Now - Stream.LastPacketTime > Settings.VoiceJitterBufferMs / 1000.0 * 2.0)
		{
			Stream.bPrebuffering = true;
		}
		return;
	}

	// Jitter buffer: hold playout until enough audio is banked to ride out delivery wobble.
	if (Stream.bPrebuffering)
	{
		int32 BankedBytes = 0;
		for (const TPair<uint32, MT2Voice::FPacket>& Pair : Stream.PendingBySeq)
		{
			BankedBytes += Pair.Value.RawSize;
		}
		const int32 NeededBytes = MT2Voice::BytesPerSecond * Settings.VoiceJitterBufferMs / 1000;
		if (BankedBytes < NeededBytes)
		{
			return;
		}
		Stream.bPrebuffering = false;
	}

	if (!Stream.Decoder.IsValid())
	{
		FVoiceModule* VoiceModule = FModuleManager::LoadModulePtr<FVoiceModule>(TEXT("Voice"));
		Stream.Decoder = VoiceModule
			? VoiceModule->CreateVoiceDecoder(MT2Voice::SampleRate, MT2Voice::NumChannels) : nullptr;
		if (!Stream.Decoder.IsValid())
		{
			Stream.PendingBySeq.Reset();
			return;
		}
	}

	// Feed every packet that is next in line. A missing sequence stalls the cursor for up to
	// VoiceGapWaitMs (late packets may still slot in); after that the cursor jumps to the oldest
	// pending packet and the lost span simply never gets queued - the procedural wave underruns and
	// plays silence for exactly that long. Nothing is stretched or repeated.
	bool bFedAnything = false;
	TArray<uint8> Decoded;
	while (true)
	{
		MT2Voice::FPacket* NextPacket = Stream.PendingBySeq.Find(Stream.NextSequence);
		if (!NextPacket)
		{
			uint32 OldestPending = MAX_uint32;
			for (const TPair<uint32, MT2Voice::FPacket>& Pair : Stream.PendingBySeq)
			{
				OldestPending = FMath::Min(OldestPending, Pair.Key);
			}
			if (OldestPending == MAX_uint32)
			{
				break;
			}
			if (Stream.GapWaitStartTime <= 0.0)
			{
				Stream.GapWaitStartTime = Now;
				break;
			}
			if ((Now - Stream.GapWaitStartTime) * 1000.0 < Settings.VoiceGapWaitMs)
			{
				break;
			}
			// Declared lost: skip ahead. The unplayed span is the "audio void".
			Stream.NextSequence = OldestPending;
			Stream.GapWaitStartTime = 0.0;
			continue;
		}

		Stream.GapWaitStartTime = 0.0;
		Decoded.SetNumUninitialized(NextPacket->RawSize + OpusDecoderSafetyBytes);
		uint32 DecodedSize = Decoded.Num();
		Stream.Decoder->Decode(
			NextPacket->Payload.GetData(), NextPacket->Payload.Num(), Decoded.GetData(), DecodedSize);
		if (DecodedSize > 0)
		{
			EnsurePlayback(SpeakerId, Stream);
			if (USoundWaveProcedural* Wave = Stream.Wave.Get())
			{
				Wave->QueueAudio(Decoded.GetData(), DecodedSize);
				bFedAnything = true;
			}
		}
		Stream.PendingBySeq.Remove(Stream.NextSequence);
		Stream.NextSequence++;
	}

	// A procedural source may briefly stop while its queue is empty. Retry/restart after audio was
	// queued, and also recreate it if the speaker pawn despawned/respawned.
	if (bFedAnything)
	{
		EnsurePlayback(SpeakerId, Stream);
	}
}

void UMT2VoiceChatClientSubsystem::EnsurePlayback(int32 SpeakerId, FSpeakerStream& Stream)
{
	if (UAudioComponent* ExistingAudio = Stream.Audio.Get())
	{
		if (!ExistingAudio->IsPlaying())
		{
			ExistingAudio->FadeIn(PlaybackFadeSeconds);
		}
		return;
	}
	if (!Stream.Wave.IsValid())
	{
		USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(GetTransientPackage());
		Wave->SetSampleRate(MT2Voice::SampleRate);
		Wave->NumChannels = MT2Voice::NumChannels;
		Wave->Duration = INDEFINITELY_LOOPING_DURATION;
		Wave->SoundGroup = SOUNDGROUP_Voice;
		// Keep the procedural voice alive across short queue underruns. Missing packets intentionally
		// produce silence; stopping the component here prevented later queued speech from being heard.
		Wave->bLooping = true;
		Stream.Wave = TStrongObjectPtr<USoundWaveProcedural>(Wave);
	}

	// Voices live in the world: attach to the speaker's pawn and fall off to silence at the same
	// range the relay stops forwarding at, so the network cutoff is never audible.
	APawn* SpeakerPawn = FindPawnByPlayerId(SpeakerId);
	if (!SpeakerPawn)
	{
		return;
	}
	USoundAttenuation* Attenuation = NewObject<USoundAttenuation>(GetTransientPackage());
	Attenuation->Attenuation.bAttenuate = true;
	Attenuation->Attenuation.bSpatialize = true;
	Attenuation->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
	Attenuation->Attenuation.AttenuationShapeExtents = FVector(500.0f, 0.0f, 0.0f);
	Attenuation->Attenuation.FalloffDistance =
		FMath::Max(UMT2GameplaySettings::Get().VoiceChatRange - 500.0f, 100.0f);

	UAudioComponent* Audio = UGameplayStatics::SpawnSoundAttached(
		Stream.Wave.Get(), SpeakerPawn->GetRootComponent(), NAME_None, FVector::ZeroVector,
		EAttachLocation::KeepRelativeOffset, false, FMT2AudioUserSettings::GetVoiceVolume(),
		1.0f, 0.0f, Attenuation);
	if (Audio)
	{
		Audio->bAutoDestroy = false;
		Audio->FadeIn(PlaybackFadeSeconds);
		Stream.Audio = Audio;
	}
}

APawn* UMT2VoiceChatClientSubsystem::FindPawnByPlayerId(int32 PlayerId) const
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
