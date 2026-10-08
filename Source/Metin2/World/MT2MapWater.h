#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "MT2MapWater.generated.h"

class AMT2MapPresentationActor;
class UProceduralMeshComponent;
struct FMT2MapAttributes;

// Exact native visual-water grid rectangles, independent of gameplay attributes.
USTRUCT()
struct METIN2_API FMT2WaterRectangle
{
	GENERATED_BODY()
	UPROPERTY() FIntPoint Cell = FIntPoint::ZeroValue;
	UPROPERTY() FIntPoint Size = FIntPoint::ZeroValue;
	UPROPERTY() float Height = 0.f;
};

namespace MT2MapWater
{
	inline constexpr int32 ChunkCells = 128;
	// HeightAt is called only for water cells. Missing surface heights fail the bake atomically.
	METIN2_API bool Bake(const FMT2MapAttributes& Attributes,
		TFunctionRef<bool(int32, int32, float&)> HeightAt, TArray<FMT2WaterRectangle>& Out, FString& Error);
	METIN2_API void AppendQuad(const FMT2WaterRectangle& Rect, FIntPoint GridSize,
		FVector2D WorldMin, FVector2D WorldMax, FVector Origin, float Offset, float UVTileSize,
		TArray<FVector>& Vertices, TArray<int32>& Indices, TArray<FVector2D>& UVs);
}

// Local presentation only: no collision, replication, Tick, or server material loading.
UCLASS()
class METIN2_API UMT2MapWaterComponent : public USceneComponent
{
	GENERATED_BODY()
public:
	UMT2MapWaterComponent();
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	void Rebuild(const AMT2MapPresentationActor& Map, const FIntRect* UpdatedCells = nullptr);
private:
	void ClearChunks();
#if WITH_EDITOR
	void OnWaterSettingsChanged(UObject* Settings, FPropertyChangedEvent& Event);
#endif
	UPROPERTY(Transient, DuplicateTransient) TArray<TObjectPtr<UProceduralMeshComponent>> Chunks;
	TMap<FIntPoint, TWeakObjectPtr<UProceduralMeshComponent>> ChunkLookup;
};
