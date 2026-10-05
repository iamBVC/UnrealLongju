/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Mobs/MT2MobTypes.h"

struct FMT2MobProtoReadResult
{
	TArray<FMT2MobDefinition> Definitions;
	TArray<FString> Warnings;
};

class FMT2MobProtoReader
{
public:
	static bool Read(const FString& FilePath, FMT2MobProtoReadResult& OutResult, FString& OutError);
};

