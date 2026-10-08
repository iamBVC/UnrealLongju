#include "MT2VisualWaterPaintData.h"
#include "World/MT2MapPresentationActor.h"
#include "World/MT2MapAttributes.h"
#include "Engine/World.h"

namespace
{
	void IncludeCell(FIntRect& Rect, FIntPoint Cell)
	{
		if (Rect.Width() <= 0) { Rect = FIntRect(Cell, Cell + FIntPoint(1, 1)); }
		else { Rect.Min = Rect.Min.ComponentMin(Cell); Rect.Max = Rect.Max.ComponentMax(Cell + FIntPoint(1, 1)); }
	}
}

bool FMT2VisualWaterGrid::Load(const AMT2MapPresentationActor& Map, FString& Error)
{
	Size = FIntPoint::ZeroValue; Heights.Reset();
	const FVector2D Extent = Map.WorldMax - Map.WorldMin;
	if (Map.MapCells.X <= 0 || Map.MapCells.Y <= 0 || int64(Map.MapCells.X) * Map.MapCells.Y > 256 ||
		!FMath::IsFinite(Map.WorldMin.X) || !FMath::IsFinite(Map.WorldMin.Y) ||
		!FMath::IsFinite(Extent.X) || !FMath::IsFinite(Extent.Y) || Extent.X <= 0 || Extent.Y <= 0)
	{
		Error = TEXT("Map has invalid tile dimensions or world bounds."); return false;
	}
	const FIntPoint Expected = Map.MapCells * 128;
	if (Map.WaterGridSize != Expected && !(Map.WaterGridSize == FIntPoint::ZeroValue && Map.WaterRectangles.IsEmpty()))
	{
		Error = TEXT("Visual grid does not match map tiles; import/bake visual water first."); return false;
	}
	TArray<float> Decoded; Decoded.Init(Empty, Expected.X * Expected.Y);
	for (const FMT2WaterRectangle& Rect : Map.WaterRectangles)
	{
		if (Rect.Cell.X < 0 || Rect.Cell.Y < 0 || Rect.Size.X <= 0 || Rect.Size.Y <= 0 ||
			int64(Rect.Cell.X) + Rect.Size.X > Expected.X || int64(Rect.Cell.Y) + Rect.Size.Y > Expected.Y ||
			!FMath::IsFinite(Rect.Height) || Rect.Height == Empty ||
			Rect.Cell / MT2MapWater::ChunkCells != (Rect.Cell + Rect.Size - FIntPoint(1, 1)) / MT2MapWater::ChunkCells)
		{
			Error = TEXT("Invalid or non-canonical water rectangle; re-bake before editing."); return false;
		}
		for (int32 Y = Rect.Cell.Y; Y < Rect.Cell.Y + Rect.Size.Y; ++Y)
			for (int32 X = Rect.Cell.X; X < Rect.Cell.X + Rect.Size.X; ++X)
			{
				float& Value = Decoded[Y * Expected.X + X];
				if (Value != Empty) { Error = TEXT("Overlapping water rectangles; re-bake before editing."); return false; }
				Value = Rect.Height;
			}
	}
	Size = Expected; WorldMin = Map.WorldMin; WorldMax = Map.WorldMax; Heights = MoveTemp(Decoded);
	Error.Reset(); return true;
}

int32 FMT2VisualWaterGrid::IndexAt(FVector2D Location) const
{
	if (Size.X <= 0 || Size.Y <= 0 || !FMath::IsFinite(Location.X) || !FMath::IsFinite(Location.Y)) { return INDEX_NONE; }
	const FVector2D Position = (WorldMax - Location) * FVector2D(Size) / (WorldMax - WorldMin);
	if (Position.X < 0 || Position.Y < 0 || Position.X >= Size.X || Position.Y >= Size.Y) { return INDEX_NONE; }
	return FMath::FloorToInt(Position.Y) * Size.X + FMath::FloorToInt(Position.X);
}

int32 FMT2VisualWaterGrid::Paint(FVector2D Start, FVector2D End, double Radius, float Height, bool bErase,
	TMap<int32, float>& Before, FIntRect& Dirty)
{
	Dirty = FIntRect(0, 0, 0, 0);
	if (Size.X <= 0 || Size.Y <= 0 || !FMath::IsFinite(Radius) || Radius < 0 || Radius > 10000 ||
		!FMath::IsFinite(Height) || Height == Empty || IndexAt(Start) == INDEX_NONE || IndexAt(End) == INDEX_NONE) { return 0; }
	const FVector2D Cell = (WorldMax - WorldMin) / FVector2D(Size), Delta = End - Start;
	if (!FMath::IsFinite(Delta.SizeSquared())) { return 0; }
	const float New = bErase ? Empty : Height;
	int32 Changed = 0;
	auto Write = [&](int32 Index)
	{
		if (!Heights.IsValidIndex(Index) || Heights[Index] == New) { return; }
		Before.FindOrAdd(Index, Heights[Index]); Heights[Index] = New; ++Changed;
		IncludeCell(Dirty, FIntPoint(Index % Size.X, Index / Size.X));
	};
	if (Radius == 0)
	{
		// Exact traversal avoids missing short cell intersections on diagonal drags.
		const FVector2D Origin = (WorldMax - Start) / Cell, Direction = -Delta / Cell;
		int32 X = FMath::FloorToInt(Origin.X), Y = FMath::FloorToInt(Origin.Y);
		const int32 SX = FMath::Sign(Direction.X), SY = FMath::Sign(Direction.Y);
		const double Infinity = TNumericLimits<double>::Max();
		double NextX = SX ? (X + (SX > 0 ? 1 : 0) - Origin.X) / Direction.X : Infinity;
		double NextY = SY ? (Y + (SY > 0 ? 1 : 0) - Origin.Y) / Direction.Y : Infinity;
		double Time = 0;
		for (int32 Count = 0; Count <= Size.X + Size.Y + 2; ++Count)
		{
			if (X < 0 || Y < 0 || X >= Size.X || Y >= Size.Y || Time >= 1) { break; }
			const double Next = FMath::Min(NextX, NextY);
			if (Next > Time) { Write(Y * Size.X + X); }
			Time = Next;
			const bool bX = NextX <= NextY, bY = NextY <= NextX;
			if (bX) { X += SX; NextX += 1.0 / FMath::Abs(Direction.X); }
			if (bY) { Y += SY; NextY += 1.0 / FMath::Abs(Direction.Y); }
		}
		Write(IndexAt(End));
		return Changed;
	}
	const FVector2D Low(FMath::Min(Start.X, End.X) - Radius, FMath::Min(Start.Y, End.Y) - Radius);
	const FVector2D High(FMath::Max(Start.X, End.X) + Radius, FMath::Max(Start.Y, End.Y) + Radius);
	auto XCell = [&](double X) { return FMath::FloorToInt(FMath::Clamp((WorldMax.X - X) / Cell.X, 0.0, double(Size.X - 1))); };
	auto YCell = [&](double Y) { return FMath::FloorToInt(FMath::Clamp((WorldMax.Y - Y) / Cell.Y, 0.0, double(Size.Y - 1))); };
	const double LengthSquared = Delta.SizeSquared();
	for (int32 Y = YCell(High.Y); Y <= YCell(Low.Y); ++Y)
		for (int32 X = XCell(High.X); X <= XCell(Low.X); ++X)
		{
			const FVector2D Center = WorldMax - FVector2D((X + .5) * Cell.X, (Y + .5) * Cell.Y);
			const double T = LengthSquared > 0 ? FMath::Clamp(FVector2D::DotProduct(Center - Start, Delta) / LengthSquared, 0.0, 1.0) : 0;
			if ((Center - (Start + Delta * T)).SizeSquared() <= Radius * Radius || Y * Size.X + X == IndexAt(End)) { Write(Y * Size.X + X); }
		}
	return Changed;
}

bool FMT2VisualWaterGrid::Commit(AMT2MapPresentationActor& Map, FIntRect Dirty, FString& Error) const
{
	if (Dirty.Width() <= 0 || Dirty.Height() <= 0) { return true; }
	if (int64(Map.MapCells.X) * 128 != Size.X || int64(Map.MapCells.Y) * 128 != Size.Y || Map.WorldMin != WorldMin || Map.WorldMax != WorldMax ||
		int64(Size.X) * Size.Y != Heights.Num() || Dirty.Min.X < 0 || Dirty.Min.Y < 0 || Dirty.Max.X > Size.X || Dirty.Max.Y > Size.Y)
	{
		Error = TEXT("Map/grid changed during water editing."); return false;
	}
	const FIntRect Chunks(Dirty.Min / MT2MapWater::ChunkCells, (Dirty.Max - FIntPoint(1, 1)) / MT2MapWater::ChunkCells + FIntPoint(1, 1));
	TArray<FMT2WaterRectangle> NewRects;
	for (int32 CY = Chunks.Min.Y; CY < Chunks.Max.Y; ++CY)
		for (int32 CX = Chunks.Min.X; CX < Chunks.Max.X; ++CX)
		{
			const FIntPoint Min = FIntPoint(CX, CY) * MT2MapWater::ChunkCells;
			const FIntPoint ChunkSize = (Size - Min).ComponentMin(FIntPoint(MT2MapWater::ChunkCells, MT2MapWater::ChunkCells));
			FMT2MapAttributes Mask; Mask.Size = ChunkSize; Mask.Flags.SetNumZeroed(ChunkSize.X * ChunkSize.Y);
			for (int32 Y = 0; Y < ChunkSize.Y; ++Y)
				for (int32 X = 0; X < ChunkSize.X; ++X)
					if (Heights[(Min.Y + Y) * Size.X + Min.X + X] != Empty) { Mask.Flags[Y * ChunkSize.X + X] = MT2MapAttribute::Water; }
			TArray<FMT2WaterRectangle> Rects;
			if (!MT2MapWater::Bake(Mask, [&](int32 X, int32 Y, float& Z) { Z = Heights[(Min.Y + Y) * Size.X + Min.X + X]; return FMath::IsFinite(Z); }, Rects, Error)) { return false; }
			for (auto& Rect : Rects) { Rect.Cell += Min; NewRects.Add(Rect); }
		}
	Map.WaterRectangles.RemoveAll([&](const FMT2WaterRectangle& Rect) { return Chunks.Contains(Rect.Cell / MT2MapWater::ChunkCells); });
	Map.WaterRectangles.Append(NewRects);
	Map.WaterRectangles.Sort([](const FMT2WaterRectangle& A, const FMT2WaterRectangle& B)
	{
		return A.Cell.Y == B.Cell.Y ? A.Cell.X < B.Cell.X : A.Cell.Y < B.Cell.Y;
	});
	Map.WaterGridSize = Size; Map.MarkPackageDirty();
	if (auto* Water = Map.FindComponentByClass<UMT2MapWaterComponent>()) { Water->Rebuild(Map, &Dirty); }
	return true;
}

FMT2VisualWaterChange::FMT2VisualWaterChange(const FMT2VisualWaterGrid& Grid, FIntPoint InInitialSize,
	const TMap<int32, float>& Before) : Size(Grid.Size), InitialSize(InInitialSize), WorldMin(Grid.WorldMin), WorldMax(Grid.WorldMax)
{
	for (const auto& Pair : Before)
		if (Grid.Heights.IsValidIndex(Pair.Key) && Pair.Value != Grid.Heights[Pair.Key]) { Cells.Add({Pair.Key, Pair.Value, Grid.Heights[Pair.Key]}); }
}
bool FMT2VisualWaterChange::HasExpired(UObject* Object) const
{
	const auto* Map = Cast<AMT2MapPresentationActor>(Object);
	return !IsValid(Map) || !Map->GetWorld() || Map->GetWorld()->WorldType != EWorldType::Editor ||
		Map->WorldMin != WorldMin || Map->WorldMax != WorldMax || int64(Map->MapCells.X) * 128 != Size.X || int64(Map->MapCells.Y) * 128 != Size.Y ||
		(Map->WaterGridSize != Size && Map->WaterGridSize != InitialSize);
}
void FMT2VisualWaterChange::Write(UObject* Object, bool bAfter)
{
	if (HasExpired(Object)) { return; }
	auto* Map = CastChecked<AMT2MapPresentationActor>(Object);
	FMT2VisualWaterGrid Grid; FString Error; FIntRect Dirty(0, 0, 0, 0);
	if (!Grid.Load(*Map, Error)) { UE_LOG(LogTemp, Error, TEXT("Visual water undo: %s"), *Error); return; }
	for (const FCell& Cell : Cells) { Grid.Heights[Cell.Index] = bAfter ? Cell.After : Cell.Before; IncludeCell(Dirty, FIntPoint(Cell.Index % Size.X, Cell.Index / Size.X)); }
	if (!Grid.Commit(*Map, Dirty, Error)) { UE_LOG(LogTemp, Error, TEXT("Visual water undo: %s"), *Error); return; }
	if (!bAfter && Map->WaterRectangles.IsEmpty()) { Map->WaterGridSize = InitialSize; }
}
void FMT2VisualWaterChange::Apply(UObject* Object) { Write(Object, true); }
void FMT2VisualWaterChange::Revert(UObject* Object) { Write(Object, false); }
