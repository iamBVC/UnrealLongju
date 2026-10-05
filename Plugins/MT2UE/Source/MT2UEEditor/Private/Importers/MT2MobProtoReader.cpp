/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Importers/MT2MobProtoReader.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "ThirdParty/MiniLZO/minilzo.h"

// Single supported mob_proto format: the English MMPT container with TEA-encrypted, LZO-compressed
// ("MCOZ") 255-byte records, exactly as consumed by the 40250 client build
// (Sources/40250 client sources/source/UserInterface/PythonNonPlayer.h/.cpp).
// Older variants (raw 291-byte arrays, GF 292-byte MMPT+Snappy) were removed on purpose - this
// project imports only from locale/en protos now. See Docs/OldGameResearch/MobProtoFormats.md.
namespace
{
	constexpr uint32 MakeMagic(char A, char B, char C, char D)
	{
		return static_cast<uint8>(A) |
			(static_cast<uint32>(static_cast<uint8>(B)) << 8) |
			(static_cast<uint32>(static_cast<uint8>(C)) << 16) |
			(static_cast<uint32>(static_cast<uint8>(D)) << 24);
	}

	constexpr uint32 MobProtoMagic = MakeMagic('M', 'M', 'P', 'T');
	constexpr uint32 MobLzoMagic = MakeMagic('M', 'C', 'O', 'Z');
	constexpr uint32 MobProtoKey[4] = {4813894u, 18955u, 552631u, 6822045u};

	uint32 ReadUInt32(const uint8* Data)
	{
		uint32 Value = 0;
		FMemory::Memcpy(&Value, Data, sizeof(Value));
		return Value;
	}

	void TeaDecodeBlock(const uint32 ZInput, const uint32 YInput, const uint32* Key, uint32* Destination)
	{
		uint32 Y = YInput;
		uint32 Z = ZInput;
		uint32 Sum = 0x9E3779B9u * 32u;
		for (uint32 Round = 0; Round < 32; ++Round)
		{
			Z -= (((Y << 4) ^ (Y >> 5)) + Y) ^ (Sum + Key[(Sum >> 11) & 3]);
			Sum -= 0x9E3779B9u;
			Y -= (((Z << 4) ^ (Z >> 5)) + Z) ^ (Sum + Key[Sum & 3]);
		}
		Destination[0] = Y;
		Destination[1] = Z;
	}

	bool DecryptAndDecompress(
		const TArray<uint8>& FileData,
		uint32 InnerOffset,
		TArray<uint8>& OutData,
		FString& OutError)
	{
		if (FileData.Num() < static_cast<int32>(InnerOffset + 20))
		{
			OutError = TEXT("mob_proto compressed header is truncated.");
			return false;
		}

		const uint8* Header = FileData.GetData() + InnerOffset;
		const uint32 CompressionMagic = ReadUInt32(Header);
		const uint32 EncryptedSize = ReadUInt32(Header + 4);
		const uint32 CompressedSize = ReadUInt32(Header + 8);
		const uint32 RealSize = ReadUInt32(Header + 12);
		if (CompressionMagic != MobLzoMagic || EncryptedSize == 0 || CompressedSize == 0 ||
			RealSize == 0 || CompressedSize + sizeof(uint32) > EncryptedSize)
		{
			OutError = TEXT("mob_proto MCOZ header contains invalid values.");
			return false;
		}

		const uint32 EncryptedOffset = InnerOffset + 16;
		if (EncryptedOffset + EncryptedSize > static_cast<uint32>(FileData.Num()))
		{
			OutError = TEXT("mob_proto encrypted payload is truncated.");
			return false;
		}

		TArray<uint8> Decrypted;
		Decrypted.SetNumZeroed(EncryptedSize);
		for (uint32 Offset = 0; Offset + 8 <= EncryptedSize; Offset += 8)
		{
			const uint32* SourceWords = reinterpret_cast<const uint32*>(FileData.GetData() + EncryptedOffset + Offset);
			uint32 DecodedWords[2];
			TeaDecodeBlock(SourceWords[1], SourceWords[0], MobProtoKey, DecodedWords);
			FMemory::Memcpy(Decrypted.GetData() + Offset, DecodedWords, sizeof(DecodedWords));
		}

		if (ReadUInt32(Decrypted.GetData()) != MobLzoMagic)
		{
			OutError = TEXT("mob_proto decryption key is not recognized.");
			return false;
		}

		OutData.SetNumUninitialized(RealSize);
		lzo_uint OutputSize = static_cast<lzo_uint>(RealSize);
		const int32 LzoResult = lzo1x_decompress_safe(
			Decrypted.GetData() + sizeof(uint32),
			static_cast<lzo_uint>(CompressedSize),
			OutData.GetData(),
			&OutputSize,
			nullptr);
		if (LzoResult != LZO_E_OK || OutputSize != RealSize)
		{
			OutError = FString::Printf(
				TEXT("mob_proto LZO decompression failed (%d, %llu/%u bytes)."),
				LzoResult, static_cast<uint64>(OutputSize), RealSize);
			OutData.Reset();
			return false;
		}
		return true;
	}

#pragma pack(push, 1)
	struct FMobProtoSkillRecord
	{
		uint32 Vnum;
		uint8 Level;
	};

	// Field order verified against the 40250 client build's CPythonNonPlayer::SMobTable
	// (Sources/40250 client sources/source/UserInterface/PythonNonPlayer.h:57, pack(1)). This layout
	// differs substantially from the classic TMobTable: bType comes BEFORE bRank, the
	// gold/exp/HP/regen/defense block sits immediately after bSize (not mid-record), the flag dwords
	// follow that block (with no mount-capacity byte between them), resurrection precedes drop-item,
	// and mount-capacity/on-click/empire/folder[65] live near the tail. Same 255-byte total as a
	// misordered guess would produce - so ONLY matching this exact order decodes correctly.
	struct FMobProtoRecord255
	{
		uint32 Vnum;
		char Name[25];
		char LocaleName[25];
		uint8 Type;
		uint8 Rank;
		uint8 BattleType;
		uint8 Level;
		uint8 Size;
		uint32 GoldMin;
		uint32 GoldMax;
		uint32 Experience;
		uint32 MaxHealth;
		uint8 RegenerationCycle;
		uint8 RegenerationPercent;
		uint16 Defense;
		uint32 AIFlags;
		uint32 RaceFlags;
		uint32 ImmuneFlags;
		uint8 Strength;
		uint8 Dexterity;
		uint8 Constitution;
		uint8 Intelligence;
		uint32 DamageRange[2];
		int16 AttackSpeed;
		int16 MovementSpeed;
		uint8 AggressiveHealthPercent;
		uint16 AggressiveSight;
		uint16 AttackRange;
		int8 Enchants[6];
		int8 Resistances[11];
		uint32 ResurrectionVnum;
		uint32 DropItemVnum;
		uint8 MountCapacity;
		uint8 OnClickType;
		uint8 Empire;
		char Folder[65];
		float DamageMultiplier;
		uint32 SummonVnum;
		uint32 DrainSP;
		uint32 MobColor;
		uint32 PolymorphItemVnum;
		FMobProtoSkillRecord Skills[5];
		uint8 BerserkPoint;
		uint8 StoneSkinPoint;
		uint8 GodSpeedPoint;
		uint8 DeathBlowPoint;
		uint8 RevivePoint;
	};
#pragma pack(pop)

	static_assert(sizeof(FMobProtoRecord255) == 255, "English MMPT mob record layout changed.");

	FString ReadAnsiField(const char* Data, int32 Capacity)
	{
		int32 Length = 0;
		while (Length < Capacity && Data[Length] != '\0')
		{
			++Length;
		}
		TArray<ANSICHAR> Buffer;
		Buffer.Append(Data, Length);
		Buffer.Add('\0');
		return FString(ANSI_TO_TCHAR(Buffer.GetData()));
	}

	FMT2MobDefinition ConvertRecord(const FMobProtoRecord255& Record)
	{
		FMT2MobDefinition Definition;
		Definition.Vnum = static_cast<int32>(Record.Vnum);
		Definition.InternalName = ReadAnsiField(Record.Name, UE_ARRAY_COUNT(Record.Name));
		Definition.DisplayName = ReadAnsiField(Record.LocaleName, UE_ARRAY_COUNT(Record.LocaleName));
		Definition.SourceFolder = ReadAnsiField(Record.Folder, UE_ARRAY_COUNT(Record.Folder));
		Definition.Rank = static_cast<EMT2MobRank>(FMath::Min<uint8>(Record.Rank, 5));
		Definition.Type = static_cast<EMT2MobType>(FMath::Min<uint8>(Record.Type, 9));
		Definition.BattleType = static_cast<EMT2MobBattleType>(FMath::Min<uint8>(Record.BattleType, 7));
		Definition.Size = static_cast<EMT2MobSize>(FMath::Min<uint8>(Record.Size, 3));
		Definition.ScalePercent = 100;
		Definition.Level = FMath::Max<int32>(Record.Level, 1);
		Definition.AIFlags = static_cast<int32>(Record.AIFlags);
		Definition.RaceFlags = static_cast<int32>(Record.RaceFlags);
		Definition.ImmuneFlags = static_cast<int32>(Record.ImmuneFlags);
		Definition.Empire = Record.Empire;
		Definition.Strength = Record.Strength;
		Definition.Dexterity = Record.Dexterity;
		Definition.Constitution = Record.Constitution;
		Definition.Intelligence = Record.Intelligence;
		Definition.DamageMin = static_cast<int32>(Record.DamageRange[0]);
		Definition.DamageMax = static_cast<int32>(Record.DamageRange[1]);
		Definition.MaxHealth = FMath::Max<int32>(static_cast<int32>(Record.MaxHealth), 1);
		Definition.RegenerationCycle = Record.RegenerationCycle;
		Definition.RegenerationPercent = Record.RegenerationPercent;
		Definition.OnClickType = Record.OnClickType;
		Definition.GoldMin = static_cast<int64>(Record.GoldMin);
		Definition.GoldMax = static_cast<int64>(Record.GoldMax);
		Definition.ExperienceReward = static_cast<int64>(Record.Experience);
		Definition.Defense = Record.Defense;
		Definition.AttackSpeed = FMath::Max<int32>(Record.AttackSpeed, 1);
		Definition.MovementSpeed = FMath::Max<int32>(Record.MovementSpeed, 0);
		Definition.AggressiveHealthPercent = Record.AggressiveHealthPercent;
		Definition.AggressiveSight = Record.AggressiveSight;
		Definition.AttackRange = Record.AttackRange;
		Definition.DropItemVnum = static_cast<int32>(Record.DropItemVnum);
		Definition.ResurrectionVnum = static_cast<int32>(Record.ResurrectionVnum);
		Definition.DamageMultiplier = FMath::Max(Record.DamageMultiplier, 0.0f);
		Definition.SummonVnum = static_cast<int32>(Record.SummonVnum);
		for (int8 Value : Record.Enchants)
		{
			Definition.Enchants.Add(Value);
		}
		for (int8 Value : Record.Resistances)
		{
			Definition.Resistances.Add(Value);
		}
		for (const FMobProtoSkillRecord& Skill : Record.Skills)
		{
			if (Skill.Vnum != 0)
			{
				FMT2MobSkillDefinition& MobSkill = Definition.Skills.AddDefaulted_GetRef();
				MobSkill.SkillVnum = static_cast<int32>(Skill.Vnum);
				MobSkill.Level = Skill.Level;
			}
		}
		Definition.BerserkHealthPercent = Record.BerserkPoint;
		Definition.StoneSkinHealthPercent = Record.StoneSkinPoint;
		Definition.GodSpeedHealthPercent = Record.GodSpeedPoint;
		Definition.DeathBlowHealthPercent = Record.DeathBlowPoint;
		Definition.ReviveHealthPercent = Record.RevivePoint;
		return Definition;
	}
}

bool FMT2MobProtoReader::Read(
	const FString& FilePath, FMT2MobProtoReadResult& OutResult, FString& OutError)
{
	OutResult = FMT2MobProtoReadResult();
	OutError.Reset();

	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
	{
		OutError = FString::Printf(TEXT("Could not read mob_proto: %s"), *FilePath);
		return false;
	}
	if (FileData.Num() < 12 || ReadUInt32(FileData.GetData()) != MobProtoMagic)
	{
		OutError = TEXT("mob_proto has an invalid MMPT header.");
		return false;
	}

	const uint32 ElementCount = ReadUInt32(FileData.GetData() + 4);
	const uint32 PayloadSize = ReadUInt32(FileData.GetData() + 8);
	if (PayloadSize + 12u > static_cast<uint32>(FileData.Num()))
	{
		OutError = TEXT("mob_proto MMPT payload is truncated.");
		return false;
	}

	TArray<uint8> RecordsData;
	if (!DecryptAndDecompress(FileData, 12, RecordsData, OutError))
	{
		return false;
	}
	if (ElementCount == 0 || RecordsData.Num() != static_cast<int32>(ElementCount * sizeof(FMobProtoRecord255)))
	{
		OutError = FString::Printf(
			TEXT("mob_proto record size mismatch: %d bytes for %u records (expected %llu)."),
			RecordsData.Num(), ElementCount,
			static_cast<uint64>(ElementCount) * sizeof(FMobProtoRecord255));
		return false;
	}

	const FMobProtoRecord255* Records = reinterpret_cast<const FMobProtoRecord255*>(RecordsData.GetData());
	OutResult.Definitions.Reserve(ElementCount);
	for (uint32 Index = 0; Index < ElementCount; ++Index)
	{
		const FMobProtoRecord255& Record = Records[Index];
		if (Record.Vnum == 0 || Record.Type > 20 || Record.Rank > 20 || Record.BattleType > 20)
		{
			OutResult.Warnings.Add(FString::Printf(TEXT("Skipped invalid mob_proto record %u."), Index));
			continue;
		}
		OutResult.Definitions.Add(ConvertRecord(Record));
	}
	OutResult.Warnings.Add(FString::Printf(
		TEXT("Decoded %d records from English MMPT mob_proto (255-byte layout)."),
		OutResult.Definitions.Num()));
	return !OutResult.Definitions.IsEmpty();
}
