/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"

// Wire format of the self-hosted proximity voice chat (see MT2VoiceRelaySubsystem /
// MT2VoiceChatClientSubsystem). Plain unencrypted UDP by design: voice here is not sensitive, and
// dropping crypto keeps the path allocation-free and trivial to debug. A lost datagram is simply a
// hole in the stream - the receiver plays silence for that span and resumes.
namespace MT2Voice
{
	// 'M2VC' - rejects stray datagrams on the port.
	inline constexpr uint32 PacketMagic = 0x4D325643;

	// Default used when the launch command does not provide voice_port/voice_port_offset.
	// Each map server still gets a distinct voice port derived from its game port.
	inline constexpr int32 VoicePortOffset = 100;

	enum class EPacketType : uint8
	{
		// Client -> relay heartbeat; teaches/refreshes the sender's UDP endpoint for its player id.
		Hello = 0,
		// One chunk of Opus-compressed microphone audio, relayed verbatim to nearby players.
		Audio = 1,
	};

	// Capture/encode format. Capture at the native 48 kHz rate used by most Windows devices to avoid
	// the coarse capture-side resampling that made speech sound digitally crushed. Opus still keeps
	// the stream compact, and mono is sufficient for positional voice.
	inline constexpr int32 SampleRate = 48000;
	inline constexpr int32 NumChannels = 1;
	inline constexpr int32 BytesPerSample = 2;
	inline constexpr int32 EncoderBitRate = 48000;
	inline constexpr int32 EncoderComplexity = 5;
	inline constexpr int32 BytesPerSecond = SampleRate * NumChannels * BytesPerSample;   // 96000
	// Cap one datagram at 100 ms of audio so a single lost packet never costs more than that.
	inline constexpr int32 MaxRawBytesPerPacket = BytesPerSecond / 10;                    // 9600
	inline constexpr int32 MaxCompressedPayload = 4096;

	// magic(4) + type(1) + speakerId(4) + sequence(4) + rawSize(2) + payloadSize(2)
	inline constexpr int32 HeaderSize = 17;

	struct FPacket
	{
		EPacketType Type = EPacketType::Hello;
		// PlayerState->GetPlayerId() of the speaker; the relay validates it against the endpoint
		// registered by that id's Hello, so casual spoofing from another address is ignored.
		int32 SpeakerId = 0;
		// Monotonic per client session. Receivers use it to reorder and to detect losses.
		uint32 Sequence = 0;
		// Decoded PCM byte count of Payload - sizes the decode buffer exactly on the receiver.
		uint16 RawSize = 0;
		TArray<uint8> Payload;

		void Serialize(TArray<uint8>& Out) const
		{
			Out.Reset(HeaderSize + Payload.Num());
			auto Append32 = [&Out](uint32 Value)
			{
				Out.Add(static_cast<uint8>(Value & 0xFF));
				Out.Add(static_cast<uint8>((Value >> 8) & 0xFF));
				Out.Add(static_cast<uint8>((Value >> 16) & 0xFF));
				Out.Add(static_cast<uint8>((Value >> 24) & 0xFF));
			};
			auto Append16 = [&Out](uint16 Value)
			{
				Out.Add(static_cast<uint8>(Value & 0xFF));
				Out.Add(static_cast<uint8>((Value >> 8) & 0xFF));
			};
			Append32(PacketMagic);
			Out.Add(static_cast<uint8>(Type));
			Append32(static_cast<uint32>(SpeakerId));
			Append32(Sequence);
			Append16(RawSize);
			Append16(static_cast<uint16>(Payload.Num()));
			Out.Append(Payload);
		}

		static bool Deserialize(const uint8* Data, int32 Size, FPacket& Out)
		{
			if (!Data || Size < HeaderSize)
			{
				return false;
			}
			auto Read32 = [Data](int32 Offset) -> uint32
			{
				return static_cast<uint32>(Data[Offset]) |
					(static_cast<uint32>(Data[Offset + 1]) << 8) |
					(static_cast<uint32>(Data[Offset + 2]) << 16) |
					(static_cast<uint32>(Data[Offset + 3]) << 24);
			};
			auto Read16 = [Data](int32 Offset) -> uint16
			{
				return static_cast<uint16>(Data[Offset]) |
					(static_cast<uint16>(Data[Offset + 1]) << 8);
			};
			if (Read32(0) != PacketMagic)
			{
				return false;
			}
			Out.Type = static_cast<EPacketType>(Data[4]);
			Out.SpeakerId = static_cast<int32>(Read32(5));
			Out.Sequence = Read32(9);
			Out.RawSize = Read16(13);
			const uint16 PayloadSize = Read16(15);
			if (PayloadSize > MaxCompressedPayload || HeaderSize + PayloadSize > Size)
			{
				return false;
			}
			Out.Payload.SetNumUninitialized(PayloadSize);
			if (PayloadSize > 0)
			{
				FMemory::Memcpy(Out.Payload.GetData(), Data + HeaderSize, PayloadSize);
			}
			return true;
		}
	};
}
