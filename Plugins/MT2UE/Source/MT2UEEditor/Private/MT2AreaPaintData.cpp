#include "MT2AreaPaintData.h"
#include "World/MT2MapPresentationActor.h"
#include "ConvexVolume.h"

FIntRect MT2AreaPaint::VisibleGridRect(const AMT2MapPresentationActor& Map, const TArray<FBox>& TerrainBounds, const FConvexVolume& Frustum)
{
	if (!IsValidGrid(&Map)) { return FIntRect(0, 0, 0, 0); }
	FBox2D Visible(ForceInit);
	for (const FBox& Bounds : TerrainBounds)
	{
		if (!Bounds.IsValid || !Frustum.IntersectBox(Bounds.GetCenter(), Bounds.GetExtent())) { continue; }
		const FVector2D Low(FMath::Max(Bounds.Min.X, Map.WorldMin.X), FMath::Max(Bounds.Min.Y, Map.WorldMin.Y));
		const FVector2D High(FMath::Min(Bounds.Max.X, Map.WorldMax.X), FMath::Min(Bounds.Max.Y, Map.WorldMax.Y));
		if (Low.X >= High.X || Low.Y >= High.Y) { continue; }
		// Project each frustum half-space conservatively through the terrain's height range.
		// This crops even a large component when zoomed in, without excluding visible slopes.
		TArray<FVector2D, TInlineAllocator<16>> Polygon = {Low, FVector2D(High.X, Low.Y), High, FVector2D(Low.X, High.Y)};
		for (const FPlane& Plane : Frustum.Planes)
		{
			if (Polygon.IsEmpty()) { break; }
			const double Z = FMath::Min(Plane.Z * Bounds.Min.Z, Plane.Z * Bounds.Max.Z);
			auto Distance = [&](const FVector2D& Point) { return Plane.X * Point.X + Plane.Y * Point.Y + Z - Plane.W; };
			TArray<FVector2D, TInlineAllocator<16>> Clipped;
			FVector2D Previous = Polygon.Last(); double PreviousDistance = Distance(Previous);
			for (const FVector2D& Current : Polygon)
			{
				const double CurrentDistance = Distance(Current);
				if ((CurrentDistance <= 0) != (PreviousDistance <= 0))
				{
					Clipped.Add(Previous + (Current - Previous) * (PreviousDistance / (PreviousDistance - CurrentDistance)));
				}
				if (CurrentDistance <= 0) { Clipped.Add(Current); }
				Previous = Current; PreviousDistance = CurrentDistance;
			}
			Polygon = MoveTemp(Clipped);
		}
		for (const FVector2D& Point : Polygon) { Visible += Point; }
	}
	if (!Visible.bIsValid) { return FIntRect(0, 0, 0, 0); }
	const FIntPoint Size = Map.Attributes.Size;
	const FVector2D Cell = (Map.WorldMax - Map.WorldMin) / FVector2D(Size);
	const FVector2D First = (Map.WorldMax - Visible.Max) / Cell;
	const FVector2D Last = (Map.WorldMax - Visible.Min) / Cell;
	return FIntRect(FMath::Clamp(FMath::FloorToInt(First.X), 0, Size.X), FMath::Clamp(FMath::FloorToInt(First.Y), 0, Size.Y),
		FMath::Clamp(FMath::CeilToInt(Last.X), 0, Size.X), FMath::Clamp(FMath::CeilToInt(Last.Y), 0, Size.Y));
}

bool MT2AreaPaint::IsValidGrid(const AMT2MapPresentationActor* Map)
{
	if (!IsValid(Map)) { return false; }
	const FVector2D Extent = Map->WorldMax - Map->WorldMin;
	return Map->Attributes.Size.X > 0 && Map->Attributes.Size.Y > 0 &&
		int64(Map->Attributes.Size.X) * Map->Attributes.Size.Y == Map->Attributes.Flags.Num() &&
		FMath::IsFinite(Map->WorldMin.X) && FMath::IsFinite(Map->WorldMin.Y) &&
		FMath::IsFinite(Map->WorldMax.X) && FMath::IsFinite(Map->WorldMax.Y) &&
		FMath::IsFinite(Extent.X) && FMath::IsFinite(Extent.Y) && Extent.X > 0 && Extent.Y > 0;
}

FLinearColor MT2AreaPaint::BitColor(int32 Bit)
{
	static const FLinearColor Colors[] = {FLinearColor(1, .08f, .04f), FLinearColor(.05f, .35f, 1),
		FLinearColor(.05f, 1, .15f), FLinearColor(1, 1, .05f), FLinearColor(.6f, .15f, 1),
		FLinearColor(.05f, 1, 1), FLinearColor(1, .15f, .65f), FLinearColor(1, .45f, .05f)};
	return Bit >= 0 && Bit < 8 ? Colors[Bit] : FLinearColor::White;
}

FText MT2AreaPaint::BitName(int32 Bit)
{
	static const TCHAR* Names[] = {TEXT("Block / no walk (0x01)"), TEXT("Water (0x02)"),
		TEXT("Safezone / BANPK (0x04)"), TEXT("Reserved bit 3 (0x08)"), TEXT("Reserved bit 4 (0x10)"),
		TEXT("Reserved bit 5 (0x20)"), TEXT("Reserved bit 6 (0x40)"), TEXT("Object / no walk (0x80)")};
	return FText::FromString(Bit >= 0 && Bit < 8 ? Names[Bit] : TEXT("Invalid bit"));
}

int32 MT2AreaPaint::PaintSegment(AMT2MapPresentationActor& Map, const FVector2D& Start,
	const FVector2D& End, double Radius, int32 Bit, bool bErase, TMap<int32, uint8>& Before, FIntRect* OutDirty)
{
	if (OutDirty) { *OutDirty = FIntRect(0, 0, 0, 0); }
	if (!IsValidGrid(&Map) || Bit < 0 || Bit > 7 || !FMath::IsFinite(Radius) || Radius < 0 ||
		!FMath::IsFinite(Start.X) || !FMath::IsFinite(Start.Y) || !FMath::IsFinite(End.X) || !FMath::IsFinite(End.Y)) { return 0; }
	const FIntPoint Size = Map.Attributes.Size;
	const FVector2D Cell = (Map.WorldMax - Map.WorldMin) / FVector2D(Size);
	const FVector2D Low(FMath::Min(Start.X, End.X) - Radius, FMath::Min(Start.Y, End.Y) - Radius);
	const FVector2D High(FMath::Max(Start.X, End.X) + Radius, FMath::Max(Start.Y, End.Y) + Radius);
	if (High.X < Map.WorldMin.X || High.Y < Map.WorldMin.Y || Low.X > Map.WorldMax.X || Low.Y > Map.WorldMax.Y) { return 0; }
	auto Column = [&](double X) { return FMath::Clamp(FMath::FloorToInt(FMath::Clamp((Map.WorldMax.X - X) / Cell.X, 0.0, double(Size.X))), 0, Size.X - 1); };
	auto Row = [&](double Y) { return FMath::Clamp(FMath::FloorToInt(FMath::Clamp((Map.WorldMax.Y - Y) / Cell.Y, 0.0, double(Size.Y))), 0, Size.Y - 1); };
	const FVector2D Delta = End - Start;
	const double LengthSquared = Delta.SizeSquared();
	if (!FMath::IsFinite(LengthSquared) || !FMath::IsFinite(Radius * Radius)) { return 0; }
	uint8 EndFlags;
	const int32 EndIndex = Map.Attributes.Query(FVector(End, 0), Map.WorldMin, Map.WorldMax, EndFlags)
		? Row(End.Y) * Size.X + Column(End.X) : INDEX_NONE;
	const uint8 Mask = uint8(1 << Bit);
	int32 Changed = 0;
	auto WriteCell = [&](int32 Index)
	{
		uint8& Flags = Map.Attributes.Flags[Index];
		const uint8 NewFlags = bErase ? uint8(Flags & ~Mask) : uint8(Flags | Mask);
		if (Flags == NewFlags) { return; }
		if (!Before.Contains(Index)) { Before.Add(Index, Flags); }
		Flags = NewFlags; ++Changed;
		if (OutDirty)
		{
			const FIntPoint Point(Index % Size.X, Index / Size.X);
			if (OutDirty->Width() <= 0) { *OutDirty = FIntRect(Point, Point + FIntPoint(1, 1)); }
			else { OutDirty->Min.X = FMath::Min(OutDirty->Min.X, Point.X); OutDirty->Min.Y = FMath::Min(OutDirty->Min.Y, Point.Y);
				OutDirty->Max.X = FMath::Max(OutDirty->Max.X, Point.X + 1); OutDirty->Max.Y = FMath::Max(OutDirty->Max.Y, Point.Y + 1); }
		}
	};
	if (Radius == 0)
	{
		// DDA visits the original cells along a drag without quadratic bounding-box work.
		const FVector2D Origin = (Map.WorldMax - Start) / Cell;
		const FVector2D Direction = -Delta / Cell;
		if (!FMath::IsFinite(Origin.X) || !FMath::IsFinite(Origin.Y) ||
			!FMath::IsFinite(Direction.X) || !FMath::IsFinite(Direction.Y)) { return 0; }
		double Enter = 0, Exit = 1;
		for (int32 Axis = 0; Axis < 2; ++Axis)
		{
			if (Direction[Axis] == 0)
			{
				if (Origin[Axis] < 0 || Origin[Axis] >= Size[Axis]) { return 0; }
				continue;
			}
			double Near = -Origin[Axis] / Direction[Axis], Far = (Size[Axis] - Origin[Axis]) / Direction[Axis];
			if (Near > Far) { Swap(Near, Far); }
			Enter = FMath::Max(Enter, Near); Exit = FMath::Min(Exit, Far);
			if (Enter > Exit) { return 0; }
		}
		const FVector2D First = Origin + Direction * Enter;
		int32 X = FMath::Clamp(FMath::FloorToInt(First.X), 0, Size.X - 1);
		int32 Y = FMath::Clamp(FMath::FloorToInt(First.Y), 0, Size.Y - 1);
		const int32 SX = FMath::Sign(Direction.X), SY = FMath::Sign(Direction.Y);
		const double Infinity = TNumericLimits<double>::Max();
		double NextX = SX ? (X + (SX > 0 ? 1 : 0) - Origin.X) / Direction.X : Infinity;
		double NextY = SY ? (Y + (SY > 0 ? 1 : 0) - Origin.Y) / Direction.Y : Infinity;
		double Time = Enter;
		for (int64 Count = 0; Count <= int64(Size.X) + Size.Y + 2; ++Count)
		{
			if (X < 0 || Y < 0 || X >= Size.X || Y >= Size.Y || Time >= Exit) { break; }
			const double Next = FMath::Min(NextX, NextY);
			// Exclude cells touched only at a boundary; endpoint lookup uses runtime ownership.
			if (Next > Time) { WriteCell(Y * Size.X + X); }
			Time = Next;
			const bool bX = NextX <= NextY, bY = NextY <= NextX;
			if (bX) { X += SX; NextX += SX ? 1.0 / FMath::Abs(Direction.X) : 0; }
			if (bY) { Y += SY; NextY += SY ? 1.0 / FMath::Abs(Direction.Y) : 0; }
		}
		if (EndIndex != INDEX_NONE) { WriteCell(EndIndex); }
		if (Changed) { Map.MarkPackageDirty(); }
		return Changed;
	}
	for (int32 Y = Row(High.Y); Y <= Row(Low.Y); ++Y)
	{
		for (int32 X = Column(High.X); X <= Column(Low.X); ++X)
		{
			const FVector2D Center = Map.WorldMax - FVector2D((X + .5) * Cell.X, (Y + .5) * Cell.Y);
			const double T = LengthSquared > 0 ? FMath::Clamp(FVector2D::DotProduct(Center - Start, Delta) / LengthSquared, 0.0, 1.0) : 0;
			const int32 Index = Y * Size.X + X;
			if ((Center - (Start + Delta * T)).SizeSquared() > Radius * Radius && Index != EndIndex) { continue; }
			WriteCell(Index);
		}
	}
	if (Changed) { Map.MarkPackageDirty(); }
	return Changed;
}

FMT2AreaPaintChange::FMT2AreaPaintChange(const AMT2MapPresentationActor& Map, int32 Bit,
	const TMap<int32, uint8>& Before) : Size(Map.Attributes.Size), Mask(uint8(1 << FMath::Clamp(Bit, 0, 7)))
{
	for (const auto& Pair : Before)
	{
		if (Map.Attributes.Flags.IsValidIndex(Pair.Key) && ((Pair.Value ^ Map.Attributes.Flags[Pair.Key]) & Mask))
		{
			Cells.Add({Pair.Key, uint8(Pair.Value & Mask), uint8(Map.Attributes.Flags[Pair.Key] & Mask)});
		}
	}
}

bool FMT2AreaPaintChange::HasExpired(UObject* Object) const
{
	const AMT2MapPresentationActor* Map = Cast<AMT2MapPresentationActor>(Object);
	return !MT2AreaPaint::IsValidGrid(Map) || Map->Attributes.Size != Size;
}
void FMT2AreaPaintChange::Write(UObject* Object, bool bAfter)
{
	if (HasExpired(Object)) { return; }
	AMT2MapPresentationActor* Map = CastChecked<AMT2MapPresentationActor>(Object);
	for (const FCell& Cell : Cells)
	{
		uint8& Flags = Map->Attributes.Flags[Cell.Index];
		Flags = uint8((Flags & ~Mask) | (bAfter ? Cell.After : Cell.Before));
	}
	Map->MarkPackageDirty();
}
void FMT2AreaPaintChange::Apply(UObject* Object) { Write(Object, true); }
void FMT2AreaPaintChange::Revert(UObject* Object) { Write(Object, false); }
