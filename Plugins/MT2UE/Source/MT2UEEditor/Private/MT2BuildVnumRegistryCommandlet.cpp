/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "MT2BuildVnumRegistryCommandlet.h"

#include "MT2VnumRegistryBuilder.h"

UMT2BuildVnumRegistryCommandlet::UMT2BuildVnumRegistryCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UMT2BuildVnumRegistryCommandlet::Main(const FString& Params)
{
	TArray<FString> Errors;
	int32 MobCount = 0;
	int32 ItemCount = 0;
	if (!FMT2VnumRegistryBuilder::RebuildAndSave(Errors, MobCount, ItemCount))
	{
		for (const FString& Error : Errors)
		{
			UE_LOG(LogTemp, Error, TEXT("%s"), *Error);
		}
		return 1;
	}
	UE_LOG(LogTemp, Display, TEXT("VNUM registry saved: %d mobs, %d items."),
		MobCount, ItemCount);
	return 0;
}
