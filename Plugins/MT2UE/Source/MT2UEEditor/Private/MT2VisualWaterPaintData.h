#pragma once
#include "CoreMinimal.h"
#include "Misc/Change.h"
#include "World/MT2MapWater.h"

class AMT2MapPresentationActor;

// Editor-only cache. Only compact rectangles are written to the existing actor properties.
class FMT2VisualWaterGrid
{
public:
	static constexpr float Empty = -MAX_flt;
	bool Load(const AMT2MapPresentationActor& Map, FString& Error);
	int32 IndexAt(FVector2D Location) const;
	int32 Paint(FVector2D Start, FVector2D End, double Radius, float Height, bool bErase,
		TMap<int32, float>& Before, FIntRect& Dirty);
	bool Commit(AMT2MapPresentationActor& Map, FIntRect Dirty, FString& Error) const;
	FIntPoint Size = FIntPoint::ZeroValue;
	FVector2D WorldMin, WorldMax;
	TArray<float> Heights;
};

class FMT2VisualWaterChange : public FCommandChange
{
public:
	FMT2VisualWaterChange(const FMT2VisualWaterGrid& Grid, FIntPoint InitialSize, const TMap<int32, float>& Before);
	virtual void Apply(UObject* Object) override;
	virtual void Revert(UObject* Object) override;
	virtual bool HasExpired(UObject* Object) const override;
	virtual FString ToString() const override { return TEXT("Paint Metin2 visual water"); }
	virtual SIZE_T GetSize() const override { return sizeof(*this) + Cells.GetAllocatedSize(); }
	bool IsEmpty() const { return Cells.IsEmpty(); }
private:
	struct FCell { int32 Index; float Before, After; };
	void Write(UObject* Object, bool bAfter);
	FIntPoint Size, InitialSize;
	FVector2D WorldMin, WorldMax;
	TArray<FCell> Cells;
};
