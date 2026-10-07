#include "MT2MapWaterReader.h"
#include "World/MT2MapAttributes.h"
#include "Config/MT2PathSettings.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

bool FMT2MapWaterReader::Decode(const TArray<uint8>& File, TArray<float>& Heights, float HeightScale, FString& Error)
{
	constexpr int32 Cells = 128, Header = 7, Count = Cells * Cells;
	if (File.Num() < Header + Count || !FMath::IsFinite(HeightScale) || HeightScale <= 0)
	{
		Error = TEXT("Truncated water.wtr or invalid HeightScale"); return false;
	}
	auto U16 = [&](int32 At) { return uint16(File[At]) | uint16(File[At + 1]) << 8; };
	if (U16(0) != 5426 || U16(2) != Cells || U16(4) != Cells)
	{
		Error = TEXT("Invalid water.wtr magic or dimensions"); return false;
	}
	const int32 Layers = File[6], Table = Header + Count;
	const int32 Stride = File.Num() == Table + Layers * 2 ? 2 : 4;
	if (File.Num() != Table + Layers * Stride)
	{
		Error = TEXT("Invalid water.wtr height-table length"); return false;
	}
	TArray<float> Values;
	for (int32 Layer = 0; Layer < Layers; ++Layer)
	{
		const int32 At = Table + Layer * Stride;
		const int32 Raw = Stride == 2 ? U16(At) : int32(uint32(File[At]) | uint32(File[At + 1]) << 8 |
			uint32(File[At + 2]) << 16 | uint32(File[At + 3]) << 24);
		Values.Add(Raw == -1 ? -MAX_flt : float(Raw) * HeightScale);
	}
	TArray<float> Result; Result.Init(-MAX_flt, Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const uint8 Layer = File[Header + Index];
		if (Layer == 0xff) { continue; }
		if (Layer >= Layers) { Error = TEXT("water.wtr cell references an invalid height layer"); return false; }
		Result[Index] = Values[Layer];
	}
	Heights = MoveTemp(Result); Error.Reset(); return true;
}

bool FMT2MapWaterReader::Bake(const FString& Directory, FIntPoint MapCells, float HeightScale,
	const FMT2MapAttributes& Attributes, TArray<FMT2WaterRectangle>& Out, FString& Error)
{
	if (MapCells.X <= 0 || MapCells.Y <= 0 || int64(MapCells.X) * MapCells.Y > 256 ||
		(Attributes.Size != MapCells * 256 && Attributes.Size != MapCells * 512) ||
		int64(Attributes.Size.X) * Attributes.Size.Y != Attributes.Flags.Num())
	{
		Error = TEXT("Water map and attribute dimensions do not match"); return false;
	}
	const int32 CellsPerTile = Attributes.Size.X / MapCells.X;
	TMap<FIntPoint, TArray<float>> Tiles;
	// Read only tiles referenced by the authoritative water bit, not unrelated dry terrain.
	for (int32 Y = 0; Y < Attributes.Size.Y; ++Y)
	{
		for (int32 X = 0; X < Attributes.Size.X; ++X)
		{
			if (!(Attributes.Flags[Y * Attributes.Size.X + X] & MT2MapAttribute::Water)) { continue; }
			const FIntPoint Tile(X / CellsPerTile, Y / CellsPerTile);
			if (Tiles.Contains(Tile)) { continue; }
			const FString Filename = Directory / FString::Printf(TEXT("%03d%03d"), Tile.X, Tile.Y) /
				UMT2PathSettings::Path(TEXT("Part_water_wtr"));
			TArray<uint8> File; TArray<float> Heights;
			if (!FFileHelper::LoadFileToArray(File, *Filename) || !Decode(File, Heights, HeightScale, Error))
			{
				Error = Filename + TEXT(": ") + (Error.IsEmpty() ? TEXT("Cannot read water height data") : Error); return false;
			}
			Tiles.Add(Tile, MoveTemp(Heights));
		}
	}
	return MT2MapWater::Bake(Attributes, [&](int32 X, int32 Y, float& Height)
	{
		const TArray<float>* Tile = Tiles.Find(FIntPoint(X / CellsPerTile, Y / CellsPerTile));
		if (!Tile) { return false; }
		const int32 Ratio = CellsPerTile / 128;
		Height = (*Tile)[((Y % CellsPerTile) / Ratio) * 128 + (X % CellsPerTile) / Ratio];
		return Height != -MAX_flt;
	}, Out, Error);
}
