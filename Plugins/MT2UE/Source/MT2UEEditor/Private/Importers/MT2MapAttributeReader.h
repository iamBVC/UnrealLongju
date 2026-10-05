#pragma once
#include "CoreMinimal.h"
#include "World/MT2MapAttributes.h"

class FMT2MapAttributeReader
{
public:
	static bool Read(const FString& Filename, FMT2MapAttributes& Out, FString& Error);
	static bool ReadClient(const FString& Directory, FIntPoint MapCells, FMT2MapAttributes& Out, FString& Error);
};
