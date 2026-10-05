#include "World/MT2MapAttributes.h"
#include "World/MT2MapPresentationActor.h"

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
		if (Map && Map->Attributes.Query(Location, Map->WorldMin, Map->WorldMax, Flags) && (Flags & 4)) { return true; }
	}
	return false;
}
