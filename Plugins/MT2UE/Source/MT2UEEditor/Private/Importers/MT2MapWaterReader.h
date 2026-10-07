#pragma once
#include "CoreMinimal.h"
#include "World/MT2MapWater.h"

class FMT2MapWaterReader
{
public:
	// Legacy packed header + 128x128 layer indices + 16- or 32-bit LE height table.
	static bool Decode(const TArray<uint8>& File, TArray<float>& Heights, float HeightScale, FString& Error);
	static bool Bake(const FString& Directory, FIntPoint MapCells, float HeightScale,
		const FMT2MapAttributes& Attributes, TArray<FMT2WaterRectangle>& Out, FString& Error);
};
