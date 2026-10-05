/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Items/MT2ItemTypes.h"

struct FMT2ItemProtoReadResult
{
	TArray<FMT2ItemDefinition> Definitions;
	TArray<FString> Warnings;
};

class FMT2ItemProtoReader
{
public:
	static bool Read(const FString& FilePath, FMT2ItemProtoReadResult& OutResult, FString& OutError);
};
