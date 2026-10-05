/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Authentication/MT2AuthenticationUtils.h"

THIRD_PARTY_INCLUDES_START
#define UI OPENSSL_UI
#include <openssl/evp.h>
#include <openssl/rand.h>
#undef UI
THIRD_PARTY_INCLUDES_END

namespace
{
	bool DerivePassword(
		const FString& Password, const TArray<uint8>& Salt, TArray<uint8>& OutHash)
	{
		FTCHARToUTF8 PasswordUtf8(*Password);
		OutHash.SetNumUninitialized(32);
		return PKCS5_PBKDF2_HMAC(
			PasswordUtf8.Get(), PasswordUtf8.Length(), Salt.GetData(), Salt.Num(),
			MT2Authentication::PasswordIterations, EVP_sha256(), OutHash.Num(), OutHash.GetData()) == 1;
	}

}

bool MT2Authentication::ConstantTimeEquals(const FString& A, const FString& B)
{
	uint32 Difference = static_cast<uint32>(A.Len() ^ B.Len());
	const int32 Count = FMath::Max(A.Len(), B.Len());
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Difference |= static_cast<uint32>(
			(A.IsValidIndex(Index) ? A[Index] : 0) ^ (B.IsValidIndex(Index) ? B[Index] : 0));
	}
	return Difference == 0;
}

FString MT2Authentication::NormalizeUsername(const FString& Username)
{
	FString Result = Username;
	Result.TrimStartAndEndInline();
	Result.ToLowerInline();
	return Result;
}

bool MT2Authentication::ValidateUsername(const FString& Username, FString& OutError)
{
	const FString Normalized = NormalizeUsername(Username);
	if (Normalized.Len() < 3 || Normalized.Len() > 32)
	{
		OutError = TEXT("Username must contain between 3 and 32 characters.");
		return false;
	}
	for (const TCHAR Character : Normalized)
	{
		if (!FChar::IsAlnum(Character) && Character != TEXT('_') && Character != TEXT('-') &&
			Character != TEXT('.') && Character != TEXT('@'))
		{
			OutError = TEXT("Username contains unsupported characters.");
			return false;
		}
	}
	return true;
}

bool MT2Authentication::ValidatePassword(const FString& Password, FString& OutError)
{
	if (Password.Len() < 8 || Password.Len() > 128)
	{
		OutError = TEXT("Password must contain between 8 and 128 characters.");
		return false;
	}
	return true;
}

bool MT2Authentication::HashPassword(
	const FString& Password, FString& OutSaltHex, FString& OutHashHex)
{
	TArray<uint8> Salt;
	Salt.SetNumUninitialized(16);
	if (RAND_bytes(Salt.GetData(), Salt.Num()) != 1)
	{
		return false;
	}
	TArray<uint8> Hash;
	if (!DerivePassword(Password, Salt, Hash))
	{
		return false;
	}
	OutSaltHex = BytesToHex(Salt.GetData(), Salt.Num());
	OutHashHex = BytesToHex(Hash.GetData(), Hash.Num());
	return true;
}

bool MT2Authentication::VerifyPassword(
	const FString& Password, const FString& SaltHex, const FString& ExpectedHashHex)
{
	TArray<uint8> Salt;
	Salt.SetNumUninitialized(SaltHex.Len() / 2);
	if (SaltHex.Len() != 32 || !FString::ToHexBlob(SaltHex, Salt.GetData(), Salt.Num()))
	{
		return false;
	}
	TArray<uint8> Hash;
	if (!DerivePassword(Password, Salt, Hash))
	{
		return false;
	}
	return ConstantTimeEquals(BytesToHex(Hash.GetData(), Hash.Num()), ExpectedHashHex);
}

FString MT2Authentication::GenerateSecureToken(int32 NumBytes)
{
	NumBytes = FMath::Clamp(NumBytes, 16, 64);
	TArray<uint8> Bytes;
	Bytes.SetNumUninitialized(NumBytes);
	return RAND_bytes(Bytes.GetData(), Bytes.Num()) == 1
		? BytesToHex(Bytes.GetData(), Bytes.Num()).ToLower()
		: FString();
}
