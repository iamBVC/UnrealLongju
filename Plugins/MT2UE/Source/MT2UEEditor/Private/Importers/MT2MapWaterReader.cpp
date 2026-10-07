#include "MT2MapWaterReader.h"
#include "World/MT2MapAttributes.h"
#include "Config/MT2PathSettings.h"
#include "Config/MT2GameplaySettings.h"
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

bool FMT2MapWaterReader::BuildVisual(FIntPoint Size, const TArray<float>& Heights, int32 PaddingCells,
	TArray<FMT2WaterRectangle>& Out, FString& Error)
{
	if (Size.X <= 0 || Size.Y <= 0 || int64(Size.X) * Size.Y != Heights.Num() || PaddingCells < 0 || PaddingCells > 4)
	{
		Error = TEXT("Invalid visual water grid or shoreline padding (0..4 cells)"); return false;
	}
	TArray<float> Padded = Heights;
	auto Valid = [](float Height) { return Height != -MAX_flt && FMath::IsFinite(Height); };
	for (int32 Y = 0; Y < Size.Y; ++Y)
	{
		for (int32 X = 0; X < Size.X; ++X)
		{
			const int32 Index = Y * Size.X + X;
			if (Valid(Heights[Index])) { continue; }
			int32 BestDistance = MAX_int32; float Height = -MAX_flt; bool bAmbiguous = false;
			for (int32 DY = -PaddingCells; DY <= PaddingCells; ++DY)
			{
				for (int32 DX = -PaddingCells; DX <= PaddingCells; ++DX)
				{
					const int32 NX = X + DX, NY = Y + DY, Distance = DX * DX + DY * DY;
					if (Distance > PaddingCells * PaddingCells || NX < 0 || NY < 0 || NX >= Size.X || NY >= Size.Y) { continue; }
					const float Candidate = Heights[NY * Size.X + NX];
					if (!Valid(Candidate)) { continue; }
					if (Distance < BestDistance) { BestDistance = Distance; Height = Candidate; bAmbiguous = false; }
					else if (Distance == BestDistance && Candidate != Height) { bAmbiguous = true; }
				}
			}
			// Do not extend across an ambiguous boundary between different water levels.
			if (!bAmbiguous) { Padded[Index] = Height; }
		}
	}
	FMT2MapAttributes Visual; Visual.Size = Size; Visual.Flags.SetNumZeroed(Heights.Num());
	for (int32 Index = 0; Index < Padded.Num(); ++Index) { if (Valid(Padded[Index])) { Visual.Flags[Index] = MT2MapAttribute::Water; } }
	return MT2MapWater::Bake(Visual, [&](int32 X, int32 Y, float& Z)
	{
		Z = Padded[Y * Size.X + X]; return Valid(Z);
	}, Out, Error);
}

bool FMT2MapWaterReader::Bake(const FString& Directory, FIntPoint MapCells, float HeightScale,
	const FMT2MapAttributes& Attributes, TArray<FMT2WaterRectangle>& Out, FString& Error, FIntPoint& OutGridSize)
{
	if (MapCells.X <= 0 || MapCells.Y <= 0 || int64(MapCells.X) * MapCells.Y > 256 ||
		(Attributes.Size != MapCells * 256 && Attributes.Size != MapCells * 512) ||
		int64(Attributes.Size.X) * Attributes.Size.Y != Attributes.Flags.Num())
	{
		Error = TEXT("Water map and attribute dimensions do not match"); return false;
	}
	const FIntPoint Grid = MapCells * 128;
	TArray<float> Heights; Heights.Init(-MAX_flt, Grid.X * Grid.Y);
	// Water rendering uses the client's water-layer mask, not fishing/walkability attributes.
	for (int32 Y = 0; Y < MapCells.Y; ++Y)
	{
		for (int32 X = 0; X < MapCells.X; ++X)
		{
			const FString Filename = Directory / FString::Printf(TEXT("%03d%03d"), X, Y) /
				UMT2PathSettings::Path(TEXT("Part_water_wtr"));
			TArray<uint8> File; TArray<float> Tile;
			if (!FFileHelper::LoadFileToArray(File, *Filename) || !Decode(File, Tile, HeightScale, Error))
			{
				Error = Filename + TEXT(": ") + (Error.IsEmpty() ? TEXT("Cannot read water height data") : Error); return false;
			}
			for (int32 Row = 0; Row < 128; ++Row)
			{
				FMemory::Memcpy(Heights.GetData() + (Y * 128 + Row) * Grid.X + X * 128, Tile.GetData() + Row * 128, 128 * sizeof(float));
			}
		}
	}
	if (!BuildVisual(Grid, Heights, UMT2GameplaySettings::Get().WaterShorelinePaddingCells, Out, Error)) { return false; }
	OutGridSize = Grid; return true;
}
