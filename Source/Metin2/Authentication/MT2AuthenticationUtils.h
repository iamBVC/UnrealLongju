/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"

namespace MT2Authentication
{
	constexpr int32 PasswordIterations = 210000;

	METIN2_API FString NormalizeUsername(const FString& Username);
	METIN2_API bool ValidateUsername(const FString& Username, FString& OutError);
	METIN2_API bool ValidatePassword(const FString& Password, FString& OutError);
	METIN2_API bool HashPassword(const FString& Password, FString& OutSaltHex, FString& OutHashHex);
	METIN2_API bool VerifyPassword(
		const FString& Password, const FString& SaltHex, const FString& ExpectedHashHex);
	METIN2_API bool ConstantTimeEquals(const FString& A, const FString& B);
	METIN2_API FString GenerateSecureToken(int32 NumBytes = 32);
}
