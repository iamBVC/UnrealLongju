/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Importers/MT2ItemProtoReader.h"

#include "Misc/FileHelper.h"
#include "ThirdParty/MiniLZO/minilzo.h"

namespace
{
	constexpr uint32 MakeItemMagic(char A, char B, char C, char D)
	{
		return static_cast<uint8>(A) |
			(static_cast<uint32>(static_cast<uint8>(B)) << 8) |
			(static_cast<uint32>(static_cast<uint8>(C)) << 16) |
			(static_cast<uint32>(static_cast<uint8>(D)) << 24);
	}

	constexpr uint32 ItemProtoMagic = MakeItemMagic('M', 'I', 'P', 'X');
	constexpr uint32 ItemLzoMagic = MakeItemMagic('M', 'C', 'O', 'Z');
	constexpr uint32 ItemProtoKey[4] = {173217u, 72619434u, 408587239u, 27973291u};

	uint32 ReadItemUInt32(const uint8* Data)
	{
		uint32 Value = 0;
		FMemory::Memcpy(&Value, Data, sizeof(Value));
		return Value;
	}

	void TeaDecodeItemBlock(const uint32 ZInput, const uint32 YInput, const uint32* Key, uint32* Destination)
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

	bool DecryptAndDecompressItemProto(
		const TArray<uint8>& FileData, uint32 InnerOffset, TArray<uint8>& OutData, FString& OutError)
	{
		if (FileData.Num() < static_cast<int32>(InnerOffset + 16))
		{
			OutError = TEXT("item_proto compressed header is truncated.");
			return false;
		}

		const uint8* Header = FileData.GetData() + InnerOffset;
		const uint32 CompressionMagic = ReadItemUInt32(Header);
		const uint32 EncryptedSize = ReadItemUInt32(Header + 4);
		const uint32 CompressedSize = ReadItemUInt32(Header + 8);
		const uint32 RealSize = ReadItemUInt32(Header + 12);
		if (CompressionMagic != ItemLzoMagic || EncryptedSize == 0 || (EncryptedSize % 8) != 0 ||
			CompressedSize == 0 || RealSize == 0 || CompressedSize + sizeof(uint32) > EncryptedSize)
		{
			OutError = TEXT("item_proto MCOZ header contains invalid values.");
			return false;
		}

		const uint32 EncryptedOffset = InnerOffset + 16;
		if (EncryptedOffset + EncryptedSize > static_cast<uint32>(FileData.Num()))
		{
			OutError = TEXT("item_proto encrypted payload is truncated.");
			return false;
		}

		TArray<uint8> Decrypted;
		Decrypted.SetNumUninitialized(EncryptedSize);
		for (uint32 Offset = 0; Offset < EncryptedSize; Offset += 8)
		{
			const uint32* SourceWords =
				reinterpret_cast<const uint32*>(FileData.GetData() + EncryptedOffset + Offset);
			uint32 DecodedWords[2];
			TeaDecodeItemBlock(SourceWords[1], SourceWords[0], ItemProtoKey, DecodedWords);
			FMemory::Memcpy(Decrypted.GetData() + Offset, DecodedWords, sizeof(DecodedWords));
		}
		if (ReadItemUInt32(Decrypted.GetData()) != ItemLzoMagic)
		{
			OutError = TEXT("item_proto decryption key is not recognized.");
			return false;
		}

		OutData.SetNumUninitialized(RealSize);
		lzo_uint OutputSize = static_cast<lzo_uint>(RealSize);
		const int32 Result = lzo1x_decompress_safe(
			Decrypted.GetData() + sizeof(uint32), static_cast<lzo_uint>(CompressedSize),
			OutData.GetData(), &OutputSize, nullptr);
		if (Result != LZO_E_OK || OutputSize != RealSize)
		{
			OutError = FString::Printf(
				TEXT("item_proto LZO decompression failed (%d, %llu/%u bytes)."),
				Result, static_cast<uint64>(OutputSize), RealSize);
			OutData.Reset();
			return false;
		}
		return true;
	}

#pragma pack(push, 1)
	struct FItemProtoLimit
	{
		uint8 Type;
		int32 Value;
	};

	struct FItemProtoApply
	{
		uint8 Type;
		int32 Value;
	};

	// Exact MIPX v1 client layout. VnumRange is stored beside Vnum and the compact
	// record ends with refine/specular/socket-chance fields.
	struct FItemProtoRecord156
	{
		uint32 Vnum;
		uint32 VnumRange;
		char Name[25];
		char LocaleName[25];
		uint8 Type;
		uint8 SubType;
		uint8 Weight;
		uint8 Size;
		uint32 AntiFlags;
		uint32 Flags;
		uint32 WearFlags;
		uint32 ImmuneFlags;
		// 40250 TItemTable dwIBuyItemPrice / dwISellItemPrice (GameLib/ItemData.h:398-399).
		uint32 BuyPrice;
		uint32 SellPrice;
		FItemProtoLimit Limits[2];
		FItemProtoApply Applies[3];
		int32 Values[6];
		int32 Sockets[3];
		uint32 RefinedVnum;
		uint16 RefineSet;
		uint8 AlterToMagicItemPct;
		uint8 Specular;
		uint8 GainSocketPct;
	};
#pragma pack(pop)

	static_assert(sizeof(FItemProtoRecord156) == 156, "MIPX v1 item record layout changed.");

	FString ReadItemAnsiField(const char* Data, int32 Capacity)
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

	FMT2ItemDefinition ConvertRecord(const FItemProtoRecord156& Record)
	{
		FMT2ItemDefinition Definition;
		Definition.Vnum = static_cast<int32>(Record.Vnum);
		Definition.InternalName = ReadItemAnsiField(Record.Name, UE_ARRAY_COUNT(Record.Name));
		Definition.DisplayName = ReadItemAnsiField(Record.LocaleName, UE_ARRAY_COUNT(Record.LocaleName));
		Definition.ItemType = Record.Type;
		Definition.SubType = Record.SubType;
		Definition.Size = FMath::Max<int32>(Record.Size, 1);
		Definition.WearFlags = static_cast<int32>(Record.WearFlags);
		Definition.AntiFlags = static_cast<int32>(Record.AntiFlags);
		Definition.Flags = static_cast<int32>(Record.Flags);
		Definition.ImmuneFlags = static_cast<int32>(Record.ImmuneFlags);
		Definition.Weight = Record.Weight;
		Definition.VnumRange = static_cast<int32>(Record.VnumRange);
		Definition.BuyPrice = static_cast<int64>(Record.BuyPrice);
		Definition.SellPrice = static_cast<int64>(Record.SellPrice);
		Definition.RefinedVnum = static_cast<int32>(Record.RefinedVnum);
		Definition.RefineSet = Record.RefineSet;
		Definition.AlterToMagicItemPercent = Record.AlterToMagicItemPct;
		Definition.GainSocketPercent = Record.GainSocketPct;
		Definition.Specular = Record.Specular;
		Definition.Values.Reserve(UE_ARRAY_COUNT(Record.Values));
		for (int32 Value : Record.Values)
		{
			Definition.Values.Add(Value);
		}
		for (const FItemProtoApply& Apply : Record.Applies)
		{
			if (Apply.Type != 0)
			{
				FMT2ItemApply& ItemApply = Definition.Applies.AddDefaulted_GetRef();
				ItemApply.Type = static_cast<EMT2ItemBonusType>(Apply.Type);
				ItemApply.Value = Apply.Value;
			}
		}
		for (const FItemProtoLimit& Limit : Record.Limits)
		{
			if (Limit.Type != 0)
			{
				FMT2ItemLimit& ItemLimit = Definition.Limits.AddDefaulted_GetRef();
				ItemLimit.Type = static_cast<EMT2ItemLimitType>(Limit.Type);
				ItemLimit.Value = Limit.Value;
			}
		}
		return Definition;
	}
}

bool FMT2ItemProtoReader::Read(const FString& FilePath, FMT2ItemProtoReadResult& OutResult, FString& OutError)
{
	OutResult = FMT2ItemProtoReadResult();
	OutError.Reset();

	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
	{
		OutError = FString::Printf(TEXT("Could not read item_proto: %s"), *FilePath);
		return false;
	}

	if (FileData.Num() < 36 || ReadItemUInt32(FileData.GetData()) != ItemProtoMagic)
	{
		OutError = TEXT("item_proto has an invalid MIPX header.");
		return false;
	}

	const uint32 Version = ReadItemUInt32(FileData.GetData() + 4);
	const uint32 Stride = ReadItemUInt32(FileData.GetData() + 8);
	const uint32 ElementCount = ReadItemUInt32(FileData.GetData() + 12);
	const uint32 PayloadSize = ReadItemUInt32(FileData.GetData() + 16);
	if (Version != 1 || Stride != sizeof(FItemProtoRecord156) || ElementCount == 0)
	{
		OutError = FString::Printf(
			TEXT("Unsupported item_proto MIPX layout: version=%u stride=%u rows=%u (expected v1/%u)."),
			Version, Stride, ElementCount, static_cast<uint32>(sizeof(FItemProtoRecord156)));
		return false;
	}
	if (PayloadSize + 20u > static_cast<uint32>(FileData.Num()))
	{
		OutError = TEXT("item_proto MIPX payload is truncated.");
		return false;
	}

	TArray<uint8> RecordsData;
	if (!DecryptAndDecompressItemProto(FileData, 20, RecordsData, OutError))
	{
		return false;
	}
	const uint64 ExpectedSize = static_cast<uint64>(ElementCount) * sizeof(FItemProtoRecord156);
	if (RecordsData.Num() != static_cast<int32>(ExpectedSize))
	{
		OutError = FString::Printf(
			TEXT("item_proto record size mismatch: %d bytes for %u rows (expected %llu)."),
			RecordsData.Num(), ElementCount, ExpectedSize);
		return false;
	}

	const FItemProtoRecord156* Records =
		reinterpret_cast<const FItemProtoRecord156*>(RecordsData.GetData());
	OutResult.Definitions.Reserve(ElementCount);
	for (uint32 Index = 0; Index < ElementCount; ++Index)
	{
		const FItemProtoRecord156& Record = Records[Index];
		if (Record.Vnum == 0 || Record.Type >= 35)
		{
			OutResult.Warnings.Add(FString::Printf(TEXT("Skipped invalid item_proto record %u."), Index));
			continue;
		}
		OutResult.Definitions.Add(ConvertRecord(Record));
	}
	OutResult.Warnings.Add(FString::Printf(
		TEXT("Decoded %d records from English MIPX v1 item_proto."), OutResult.Definitions.Num()));
	return !OutResult.Definitions.IsEmpty();
}
