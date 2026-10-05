/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Core/MT2GameInstance.h"

#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

void UMT2GameInstance::OnStart()
{
	Super::OnStart();

	// Slate owns desktop activation notifications. Do not bind editor PIE instances because several
	// game instances share the editor's single GEngine frame limiter.
	if (!IsDedicatedServerInstance() && !GIsEditor && FSlateApplication::IsInitialized())
	{
		ApplicationActivationHandle = FSlateApplication::Get()
			.OnApplicationActivationStateChanged()
			.AddUObject(this, &UMT2GameInstance::HandleApplicationActivationChanged);
	}

	// Never on a dedicated server (it is launched with a map + role flags, not a connect address),
	// and only when we actually loaded standalone - if the engine already connected as a client,
	// leave it alone.
	if (IsDedicatedServerInstance())
	{
		return;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("Coordinator")) ||
		FParse::Param(FCommandLine::Get(), TEXT("Gateway")) ||
		FParse::Param(FCommandLine::Get(), TEXT("server")))
	{
		return;
	}
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() != NM_Standalone)
	{
		return;
	}

	const FString ConnectAddress = ResolveStartupConnectAddress();
	if (ConnectAddress.IsEmpty())
	{
		return;
	}
	if (APlayerController* PlayerController = GetFirstLocalPlayerController())
	{
		UE_LOG(LogTemp, Display, TEXT("[MT2Startup] Auto-connecting cooked client to gateway '%s'."), *ConnectAddress);
		PlayerController->ClientTravel(ConnectAddress, TRAVEL_Absolute);
	}
}

void UMT2GameInstance::Shutdown()
{
	if (ApplicationActivationHandle.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().OnApplicationActivationStateChanged()
			.Remove(ApplicationActivationHandle);
		ApplicationActivationHandle.Reset();
	}
	if (bBackgroundFrameRateLimitActive && GEngine)
	{
		GEngine->SetMaxFPS(ForegroundFrameRateLimit);
		bBackgroundFrameRateLimitActive = false;
	}
	Super::Shutdown();
}

void UMT2GameInstance::HandleApplicationActivationChanged(bool bIsActive)
{
	if (!GEngine)
	{
		return;
	}

	if (!bIsActive)
	{
		if (bBackgroundFrameRateLimitActive)
		{
			return;
		}
		ForegroundFrameRateLimit = GEngine->GetMaxFPS();
		if (ForegroundFrameRateLimit <= 0.0f || ForegroundFrameRateLimit > 30.0f)
		{
			GEngine->SetMaxFPS(30.0f);
		}
		bBackgroundFrameRateLimitActive = true;
		return;
	}

	if (bBackgroundFrameRateLimitActive)
	{
		GEngine->SetMaxFPS(ForegroundFrameRateLimit);
		bBackgroundFrameRateLimitActive = false;
	}
}

FString UMT2GameInstance::ResolveStartupConnectAddress()
{
	// Explicit switch wins: -connect=127.0.0.1:11000
	FString Address;
	if (FParse::Value(FCommandLine::Get(), TEXT("connect="), Address) && !Address.IsEmpty())
	{
		return Address;
	}

	// Otherwise scan for a bare "host:port" token (e.g. StartAllShipping passes 127.0.0.1:11000 as
	// the first argument). Skip switches (-x / /x) and content paths (/Game/...).
	TArray<FString> Tokens;
	const FString CommandLine = FCommandLine::Get();
	CommandLine.ParseIntoArrayWS(Tokens);
	for (const FString& Token : Tokens)
	{
		if (Token.IsEmpty() || Token.StartsWith(TEXT("-")) || Token.StartsWith(TEXT("/")))
		{
			continue;
		}
		int32 ColonIndex;
		if (!Token.FindChar(TEXT(':'), ColonIndex) || ColonIndex <= 0 || ColonIndex >= Token.Len() - 1)
		{
			continue;
		}
		// Require the part after ':' to be a numeric port so we don't grab arbitrary tokens.
		const FString PortPart = Token.RightChop(ColonIndex + 1);
		bool bNumericPort = !PortPart.IsEmpty();
		for (const TCHAR Char : PortPart)
		{
			if (!FChar::IsDigit(Char)) { bNumericPort = false; break; }
		}
		if (bNumericPort)
		{
			return Token;
		}
	}
	return FString();
}
