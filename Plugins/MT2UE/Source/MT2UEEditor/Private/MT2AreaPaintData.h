#pragma once

#include "CoreMinimal.h"
#include "Misc/Change.h"

class AMT2MapPresentationActor;
struct FConvexVolume;

namespace MT2AreaPaint
{
	bool IsValidGrid(const AMT2MapPresentationActor* Map);
	FLinearColor BitColor(int32 Bit);
	FText BitName(int32 Bit);
	FIntRect VisibleGridRect(const AMT2MapPresentationActor& Map, const TArray<FBox>& TerrainBounds, const FConvexVolume& Frustum);
	int32 PaintSegment(AMT2MapPresentationActor& Map, const FVector2D& Start, const FVector2D& End,
		double Radius, int32 Bit, bool bErase, TMap<int32, uint8>& Before, FIntRect* OutDirty = nullptr);
}

// Stores only changed cells and restores only the painted bit, preserving overlapping flags.
class FMT2AreaPaintChange : public FCommandChange
{
public:
	FMT2AreaPaintChange(const AMT2MapPresentationActor& Map, int32 Bit, const TMap<int32, uint8>& Before);
	virtual void Apply(UObject* Object) override;
	virtual void Revert(UObject* Object) override;
	virtual bool HasExpired(UObject* Object) const override;
	virtual FString ToString() const override { return TEXT("Paint Metin2 area attributes"); }
	virtual SIZE_T GetSize() const override { return sizeof(*this) + Cells.GetAllocatedSize(); }
	bool IsEmpty() const { return Cells.IsEmpty(); }
private:
	struct FCell { int32 Index; uint8 Before; uint8 After; };
	void Write(UObject* Object, bool bAfter);
	FIntPoint Size;
	uint8 Mask = 0;
	TArray<FCell> Cells;
};
