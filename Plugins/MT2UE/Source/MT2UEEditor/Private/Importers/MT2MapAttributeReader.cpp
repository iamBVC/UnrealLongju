#include "MT2MapAttributeReader.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ThirdParty/MiniLZO/minilzo.h"

bool FMT2MapAttributeReader::Read(const FString& Filename, FMT2MapAttributes& Out, FString& Error)
{
	TArray<uint8> File;
	if (!FFileHelper::LoadFileToArray(File, *Filename)) { Error = TEXT("Cannot read server_attr"); return false; }
	int64 Cursor = 0;
	auto Read32 = [&](uint32& Value)
	{
		if (Cursor + 4 > File.Num()) { return false; }
		Value = uint32(File[Cursor]) | uint32(File[Cursor + 1]) << 8 | uint32(File[Cursor + 2]) << 16 | uint32(File[Cursor + 3]) << 24;
		Cursor += 4;
		return true;
	};
	uint32 Width = 0, Height = 0;
	constexpr int32 Cells = 128; // SECTREE_SIZE(6400) / CELL_SIZE(50).
	if (!Read32(Width) || !Read32(Height) || Width == 0 || Height == 0 ||
		uint64(Width) * Height > 4096)
	{
		Error = TEXT("Invalid server_attr dimensions (maximum 4096 sectrees)"); return false;
	}
	FMT2MapAttributes Parsed;
	Parsed.Size = FIntPoint(Width * Cells, Height * Cells);
	Parsed.Flags.SetNumZeroed(Parsed.Size.X * Parsed.Size.Y);
	TArray<uint8> Decoded;
	Decoded.SetNumUninitialized(Cells * Cells * 4);
	for (uint32 Y = 0; Y < Height; ++Y)
	{
		for (uint32 X = 0; X < Width; ++X)
		{
			uint32 Length = 0;
			if (!Read32(Length) || Length == 0 || Cursor + Length > File.Num())
			{
				Error = TEXT("Truncated server_attr sectree"); return false;
			}
			lzo_uint OutputLength = Decoded.Num();
			const int32 Result = lzo1x_decompress_safe(File.GetData() + Cursor, Length, Decoded.GetData(), &OutputLength, nullptr);
			if (Result != LZO_E_OK || OutputLength != Decoded.Num())
			{
				Error = TEXT("Invalid LZO server_attr sectree"); return false;
			}
			Cursor += Length;
			for (int32 Row = 0; Row < Cells; ++Row)
			{
				for (int32 Column = 0; Column < Cells; ++Column)
				{
					const int32 Source = (Row * Cells + Column) * 4;
					// Static terrain attributes are the legacy byte flags (block/water/banpk/object).
					// Do not silently truncate map-authored extended server-only flags.
					if (Decoded[Source + 1] || Decoded[Source + 2] || Decoded[Source + 3])
					{
						Error = TEXT("Extended server_attr flags require a wider attribute representation"); return false;
					}
					Parsed.Flags[(Y * Cells + Row) * Parsed.Size.X + X * Cells + Column] = Decoded[Source];
				}
			}
		}
	}
	if (Cursor != File.Num()) { Error = TEXT("Unexpected trailing server_attr data"); return false; }
	Out = MoveTemp(Parsed);
	return true;
}

bool FMT2MapAttributeReader::ReadClient(const FString& Directory, FIntPoint MapCells, FMT2MapAttributes& Out, FString& Error)
{
	// Client attributes use HALF_CELLSCALE (100), unlike the server's 50-unit cells.
	constexpr int32 Cells = 256;
	if (MapCells.X <= 0 || MapCells.Y <= 0 || int64(MapCells.X) * MapCells.Y > 256)
	{
		Error = TEXT("Invalid client attribute dimensions"); return false;
	}
	FMT2MapAttributes Parsed;
	Parsed.Size = MapCells * Cells;
	Parsed.Flags.SetNumZeroed(Parsed.Size.X * Parsed.Size.Y);
	for (int32 Y = 0; Y < MapCells.Y; ++Y)
	{
		for (int32 X = 0; X < MapCells.X; ++X)
		{
			const FString Filename = Directory / FString::Printf(TEXT("%03d%03d/attr.atr"), X, Y);
			TArray<uint8> File;
			if (!FFileHelper::LoadFileToArray(File, *Filename) || File.Num() != 6 + Cells * Cells ||
				(uint16(File[0]) | uint16(File[1]) << 8) != 2634 ||
				(uint16(File[2]) | uint16(File[3]) << 8) != Cells ||
				(uint16(File[4]) | uint16(File[5]) << 8) != Cells)
			{
				Error = TEXT("Missing or invalid client attr.atr: ") + Filename; return false;
			}
			for (int32 Row = 0; Row < Cells; ++Row)
			{
				FMemory::Memcpy(Parsed.Flags.GetData() + (Y * Cells + Row) * Parsed.Size.X + X * Cells,
					File.GetData() + 6 + Row * Cells, Cells);
			}
		}
	}
	Out = MoveTemp(Parsed);
	return true;
}
