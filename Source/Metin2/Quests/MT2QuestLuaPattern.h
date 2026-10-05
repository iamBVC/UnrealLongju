#pragma once

#include "CoreMinimal.h"

// Lua 5.0 byte-pattern/string-replacement semantics, independent of world and UObject lifetimes.
namespace MT2QuestLuaPattern
{
	struct FCaptureValue
	{
		FString Text;
		int32 Position = 0;
		bool bPosition = false;
	};
	METIN2_API bool Find(const FString& Source, const FString& Pattern, int32 InitialPosition, bool bPlain,
		bool& bOutFound, int32& OutStart, int32& OutEnd, TArray<FCaptureValue>& OutCaptures, FString& OutError);
	METIN2_API bool Substitute(const FString& Source, const FString& Pattern, const FString& Replacement,
		int32 Maximum, FString& OutText, int32& OutCount, FString& OutError);
}
