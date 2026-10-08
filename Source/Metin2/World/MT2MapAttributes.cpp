#include "World/MT2MapAttributes.h"
#include "World/MT2MapPresentationActor.h"
#include "Engine/HitResult.h"

bool FMT2MapAttributes::TraceBlockedSegment(const FVector& Start, const FVector& End,
	const FVector2D& WorldMin, const FVector2D& WorldMax, double& OutTime, FVector& OutNormal) const
{
	OutTime = 1.0;
	OutNormal = FVector::ZeroVector;
	const FVector2D Extent = WorldMax - WorldMin;
	if (Size.X <= 0 || Size.Y <= 0 || int64(Size.X) * Size.Y != Flags.Num() ||
		!FMath::IsFinite(Extent.X) || !FMath::IsFinite(Extent.Y) || Extent.X <= 0 || Extent.Y <= 0 ||
		!FMath::IsFinite(WorldMax.X) || !FMath::IsFinite(WorldMax.Y) ||
		!FMath::IsFinite(Start.X) || !FMath::IsFinite(Start.Y) ||
		!FMath::IsFinite(End.X) || !FMath::IsFinite(End.Y)) { return false; }
	const FVector2D Origin((WorldMax.X - Start.X) * Size.X / Extent.X,
		(WorldMax.Y - Start.Y) * Size.Y / Extent.Y);
	const FVector2D Direction((Start.X - End.X) * Size.X / Extent.X,
		(Start.Y - End.Y) * Size.Y / Extent.Y);
	if (!FMath::IsFinite(Origin.X) || !FMath::IsFinite(Origin.Y) ||
		!FMath::IsFinite(Direction.X) || !FMath::IsFinite(Direction.Y)) { return false; }
	if (Direction.X == 0.0 && Direction.Y == 0.0) { return false; }

	// Clip to this map, then traverse crossed cells rather than sampling the endpoint.
	double Enter = 0.0, Exit = 1.0;
	FVector EntryNormal = FVector::ZeroVector;
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		const double Position = Origin[Axis], Delta = Direction[Axis];
		const double Limit = Axis == 0 ? Size.X : Size.Y;
		if (Delta == 0.0)
		{
			if (Position < 0.0 || Position >= Limit) { return false; }
			continue;
		}
		double Near = -Position / Delta, Far = (Limit - Position) / Delta;
		if (Near > Far) { Swap(Near, Far); }
		if (Near > Enter)
		{
			Enter = Near;
			EntryNormal = Axis == 0 ? FVector(FMath::Sign(Delta), 0, 0) : FVector(0, FMath::Sign(Delta), 0);
		}
		Exit = FMath::Min(Exit, Far);
		if (Enter > Exit) { return false; }
	}
	const FVector2D First = Origin + Direction * Enter;
	if ((First.X >= Size.X && Direction.X >= 0) || (First.Y >= Size.Y && Direction.Y >= 0)) { return false; }
	int32 Column = FMath::Clamp(FMath::FloorToInt(First.X), 0, Size.X - 1);
	int32 Row = FMath::Clamp(FMath::FloorToInt(First.Y), 0, Size.Y - 1);
	const int32 StepX = Direction.X > 0 ? 1 : Direction.X < 0 ? -1 : 0;
	const int32 StepY = Direction.Y > 0 ? 1 : Direction.Y < 0 ? -1 : 0;
	const double Infinity = TNumericLimits<double>::Max();
	double NextX = StepX ? (Column + (StepX > 0 ? 1 : 0) - Origin.X) / Direction.X : Infinity;
	double NextY = StepY ? (Row + (StepY > 0 ? 1 : 0) - Origin.Y) / Direction.Y : Infinity;
	const double IntervalX = StepX ? 1.0 / FMath::Abs(Direction.X) : Infinity;
	const double IntervalY = StepY ? 1.0 / FMath::Abs(Direction.Y) : Infinity;
	auto Blocked = [&](int32 X, int32 Y)
	{
		return X >= 0 && Y >= 0 && X < Size.X && Y < Size.Y &&
			(Flags[int64(Y) * Size.X + X] & MT2MapAttribute::NoWalk) != 0;
	};
	uint8 InitialFlags = 0;
	// A bad authored spawn/admin teleport must not permanently trap the character.
	// It may leave its initial blocked region, but cannot re-enter after reaching clear ground.
	bool bEscaping = Query(Start, WorldMin, WorldMax, InitialFlags) && (InitialFlags & MT2MapAttribute::NoWalk);
	double Time = Enter;
	FVector Normal = EntryNormal;
	for (int64 Count = 0; Count <= int64(Size.X) + Size.Y + 2; ++Count)
	{
		if (Column < 0 || Row < 0 || Column >= Size.X || Row >= Size.Y || Time > Exit) { break; }
		if (Blocked(Column, Row))
		{
			if (!bEscaping)
			{
				OutTime = FMath::Clamp(Time, 0.0, 1.0);
				OutNormal = Normal.IsNearlyZero() ? (Start - End).GetSafeNormal2D() : Normal;
				return true;
			}
		}
		else { bEscaping = false; }
		Time = FMath::Min(NextX, NextY);
		if (Time > Exit) { break; }
		const bool bCrossX = NextX <= NextY, bCrossY = NextY <= NextX;
		// Supercover at exact corners prevents diagonally cutting between blocked cells.
		if (bCrossX && bCrossY && !bEscaping &&
			(Blocked(Column + StepX, Row) || Blocked(Column, Row + StepY)))
		{
			OutTime = FMath::Clamp(Time, 0.0, 1.0);
			OutNormal = FVector(StepX, StepY, 0).GetSafeNormal();
			return true;
		}
		Normal = FVector(bCrossX ? StepX : 0, bCrossY ? StepY : 0, 0).GetSafeNormal();
		if (bCrossX) { Column += StepX; NextX += IntervalX; }
		if (bCrossY) { Row += StepY; NextY += IntervalY; }
	}
	return false;
}

bool FMT2MapAttributes::Query(const FVector& Location, const FVector2D& WorldMin,
	const FVector2D& WorldMax, uint8& OutFlags) const
{
	OutFlags = 0;
	const FVector2D Extent = WorldMax - WorldMin;
	if (Size.X <= 0 || Size.Y <= 0 || int64(Size.X) * Size.Y != Flags.Num() ||
		Extent.X <= 0 || Extent.Y <= 0 || !FMath::IsFinite(Location.X) || !FMath::IsFinite(Location.Y)) { return false; }
	const double X = (WorldMax.X - Location.X) / Extent.X;
	const double Y = (WorldMax.Y - Location.Y) / Extent.Y;
	if (X < 0 || Y < 0 || X >= 1 || Y >= 1) { return false; }
	const int32 Column = FMath::FloorToInt(X * Size.X);
	const int32 Row = FMath::FloorToInt(Y * Size.Y);
	const int64 Index = int64(Row) * Size.X + Column;
	if (!Flags.IsValidIndex(Index)) { return false; }
	OutFlags = Flags[Index];
	return true;
}

void UMT2MapAttributeSubsystem::RegisterMap(AMT2MapPresentationActor* Map)
{
	Maps.AddUnique(Map);
}
void UMT2MapAttributeSubsystem::UnregisterMap(AMT2MapPresentationActor* Map)
{
	Maps.Remove(Map);
}
bool UMT2MapAttributeSubsystem::IsSafeZone(const FVector& Location) const
{
	for (const auto& WeakMap : Maps)
	{
		const AMT2MapPresentationActor* Map = WeakMap.Get();
		uint8 Flags = 0;
		if (Map && Map->Attributes.Query(Location, Map->WorldMin, Map->WorldMax, Flags) && (Flags & MT2MapAttribute::BanPK)) { return true; }
	}
	return false;
}

bool UMT2MapAttributeSubsystem::IsBlocked(const FVector& Location) const
{
	for (const auto& WeakMap : Maps)
	{
		const AMT2MapPresentationActor* Map = WeakMap.Get();
		uint8 Flags = 0;
		if (Map && Map->Attributes.Query(Location, Map->WorldMin, Map->WorldMax, Flags) && (Flags & MT2MapAttribute::NoWalk)) { return true; }
	}
	return false;
}

AMT2MapPresentationActor* UMT2MapAttributeSubsystem::FindMapAt(const FVector& Location, uint8& OutFlags) const
{
	OutFlags = 0;
	for (const auto& WeakMap : Maps)
	{
		AMT2MapPresentationActor* Map = WeakMap.Get();
		if (Map && Map->Attributes.Query(Location, Map->WorldMin, Map->WorldMax, OutFlags)) { return Map; }
	}
	return nullptr;
}

bool UMT2MapAttributeSubsystem::TraceBlockedMovement(const FVector& Start, const FVector& End, FHitResult& OutHit) const
{
	OutHit = FHitResult(1.0f);
	bool bBlocked = false;
	for (const auto& WeakMap : Maps)
	{
		AMT2MapPresentationActor* Map = WeakMap.Get();
		double Time;
		FVector Normal;
		if (Map && Map->Attributes.TraceBlockedSegment(Start, End, Map->WorldMin, Map->WorldMax, Time, Normal) &&
			(!bBlocked || Time < OutHit.Time))
		{
			const FVector Point = FMath::Lerp(Start, End, Time);
			OutHit = FHitResult(Map, nullptr, Point, Normal);
			OutHit.bBlockingHit = true;
			OutHit.Time = Time;
			OutHit.TraceStart = Start;
			OutHit.TraceEnd = End;
			OutHit.Distance = FVector::Distance(Start, Point);
			bBlocked = true;
		}
	}
	return bBlocked;
}
