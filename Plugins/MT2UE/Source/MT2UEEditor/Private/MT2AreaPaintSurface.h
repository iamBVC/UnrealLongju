#pragma once
#include "CoreMinimal.h"

class AMT2MapPresentationActor;
class ALandscapeProxy;
class ULandscapeComponent;
class ULandscapeMaterialInstanceConstant;
class UMaterial;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTexture2D;

// Uses Landscape's transient editor-tool pass: identical terrain triangles/LOD, plus a small WPO.
class FMT2AreaPaintSurface
{
public:
	~FMT2AreaPaintSurface() { Clear(); }
	static UMaterial* CreateMaterial();
	static ULandscapeMaterialInstanceConstant* CreateLandscapeMaterial(UMaterial& Material);
	static TArray<uint8> CopyFlags(const AMT2MapPresentationActor& Map, FIntRect Rect);
	void Sync(AMT2MapPresentationActor& Map, ALandscapeProxy& Terrain, UMaterial& Material, uint8 VisibleBits);
	void InvalidateCells(FIntRect Rect);
	void Clear();
	void AddReferencedObjects(FReferenceCollector& Collector);
	int32 NumComponents() const { return Bindings.Num(); }
private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FMT2AreaSurfaceTest;
#endif
	struct FBinding
	{
		TWeakObjectPtr<ULandscapeComponent> Component;
		FBox Bounds;
		FIntRect Cells;
		TObjectPtr<UTexture2D> Texture;
		TObjectPtr<UMaterialInstanceDynamic> Material;
		TObjectPtr<UMaterialInterface> PreviousMaterial;
	};
	static void Restore(FBinding& Binding);
	TArray<FBinding> Bindings;
	TObjectPtr<ULandscapeMaterialInstanceConstant> LandscapeMaterial;
	TWeakObjectPtr<AMT2MapPresentationActor> CurrentMap;
	FIntPoint Size = FIntPoint::ZeroValue;
	FVector2D WorldMin, WorldMax;
	FIntRect DirtyCells = FIntRect(0, 0, 0, 0);
	uint8 LastVisibleBits = 0;
	bool bOwnsEditModeFlag = false;
	bool bPreviousEditModeFlag = false;
};
