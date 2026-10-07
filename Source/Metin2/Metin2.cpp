/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Metin2.h"
#include "Combat/MT2AbilitySystemGlobals.h"
#include "Combat/MT2GameplayCueManager.h"
#include "Features/IModularFeatures.h"
#include "GameplayAbilitiesDeveloperSettings.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/ConfigCacheIni.h"
#include "Server/MT2ProfilingPorts.h"
#include "Modules/ModuleManager.h"
#include "Performance/MaxTickRateHandlerModule.h"

DEFINE_LOG_CATEGORY_STATIC(LogMT2Module, Log, All);

namespace
{
#if UE_BUILD_SHIPPING && !UE_SERVER
	constexpr TCHAR PatcherTokenEnvironmentVariable[] = TEXT("MT2UE_PATCHER_TOKEN");

	bool IsAuthorizedPatcherLaunch()
	{
		FString CommandLineToken;
		if (!FParse::Value(FCommandLine::Get(), TEXT("MT2PatcherToken="), CommandLineToken))
		{
			return false;
		}

		const FString EnvironmentToken =
			FPlatformMisc::GetEnvironmentVariable(PatcherTokenEnvironmentVariable);
		FGuid ParsedToken;
		const bool bAuthorized = !EnvironmentToken.IsEmpty() &&
			CommandLineToken.Equals(EnvironmentToken, ESearchCase::CaseSensitive) &&
			FGuid::ParseExact(CommandLineToken, EGuidFormats::Digits, ParsedToken);

		FPlatformMisc::SetEnvironmentVar(PatcherTokenEnvironmentVariable, TEXT(""));
		return bAuthorized;
	}
#endif
}

class FMT2ServerTickRateHandler final : public IMaxTickRateHandlerModule
{
public:
	virtual void Initialize() override { NextTickTime = 0.0; }
	virtual void SetEnabled(bool bInEnabled) override { bEnabled = bInEnabled; }
	virtual bool GetEnabled() override { return bEnabled; }
	virtual bool GetAvailable() override { return IsRunningDedicatedServer(); }
	virtual void SetFlags(uint32 InFlags) override { Flags = InFlags; }
	virtual uint32 GetFlags() override { return Flags; }

	virtual bool HandleMaxTickRate(float DesiredMaxTickRate) override
	{
		if (!bEnabled || DesiredMaxTickRate <= 0.0f)
		{
			return false;
		}

		const double TickInterval = 1.0 / static_cast<double>(DesiredMaxTickRate);
		double Now = FPlatformTime::Seconds();
		if (NextTickTime <= 0.0)
		{
			NextTickTime = Now + TickInterval;
			return true;
		}

		const double Remaining = NextTickTime - Now;
		if (Remaining > 0.001)
		{
			FPlatformProcess::SleepNoStats(static_cast<float>(Remaining - 0.001));
		}
		while ((Now = FPlatformTime::Seconds()) < NextTickTime)
		{
			FPlatformProcess::SleepNoStats(0.0f);
		}

		// Keep a stable cadence, but discard accumulated debt after a genuine over-budget hitch.
		NextTickTime = Now - NextTickTime > TickInterval
			? Now + TickInterval
			: NextTickTime + TickInterval;
		return true;
	}

private:
	bool bEnabled = false;
	uint32 Flags = 0;
	double NextTickTime = 0.0;
};

class FMetin2Module final : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		FDefaultGameModuleImpl::StartupModule();

		// UE reads FrameProPort at its first frame. Selecting it here avoids rebinding
		// a live listener or modifying FramePro's port buffer from a world callback.
		int32 ExplicitFrameProPort = 0;
		if (!FParse::Value(FCommandLine::Get(), TEXT("FrameProPort="), ExplicitFrameProPort))
		{
			int32 DefaultGamePort = 0;
			if (GConfig) { GConfig->GetInt(TEXT("URL"), TEXT("Port"), DefaultGamePort, GEngineIni); }
			int32 FrameProPort = 0;
			if (MT2ProfilingPorts::TryDeriveFrameProPort(FCommandLine::Get(), DefaultGamePort, FrameProPort))
			{
				FCommandLine::Append(*FString::Printf(TEXT(" -FrameProPort=%d"), FrameProPort));
				UE_LOG(LogMT2Module, Display, TEXT("FramePro TCP port configured to %d (startup game port + 200)."), FrameProPort);
			}
			else
			{
				UE_LOG(LogMT2Module, Warning, TEXT("Cannot derive FramePro port: game port must be 1..65335. Supply -FrameProPort= explicitly."));
			}
		}

#if UE_BUILD_SHIPPING && !UE_SERVER
		if (!IsAuthorizedPatcherLaunch())
		{
			FPlatformMisc::MessageBoxExt(EAppMsgType::Ok,
				TEXT("Please start the game using UnrealLongjuPatcher.exe."), TEXT("UnrealLongju"));
			FPlatformMisc::RequestExitWithStatus(true, 1, TEXT("Shipping client requires patcher"));
			return;
		}
#endif

		UGameplayAbilitiesDeveloperSettings* Settings =
			GetMutableDefault<UGameplayAbilitiesDeveloperSettings>();
		Settings->AbilitySystemGlobalsClassName =
			FSoftClassPath(UMT2AbilitySystemGlobals::StaticClass());
		Settings->GlobalGameplayCueManagerClass =
			FSoftClassPath(UMT2GameplayCueManager::StaticClass());
		UE_LOG(LogMT2Module, Display, TEXT("Ability globals=%s, Gameplay Cue manager=%s"),
			*Settings->AbilitySystemGlobalsClassName.ToString(),
			*Settings->GlobalGameplayCueManagerClass.ToString());

		ServerTickRateHandler.Initialize();
		ServerTickRateHandler.SetEnabled(IsRunningDedicatedServer());
		if (ServerTickRateHandler.GetEnabled())
		{
			IModularFeatures::Get().RegisterModularFeature(
				IMaxTickRateHandlerModule::GetModularFeatureName(), &ServerTickRateHandler);
		}
	}

	virtual void ShutdownModule() override
	{
		if (ServerTickRateHandler.GetEnabled() && IModularFeatures::Get().IsModularFeatureAvailable(
			IMaxTickRateHandlerModule::GetModularFeatureName()))
		{
			IModularFeatures::Get().UnregisterModularFeature(
				IMaxTickRateHandlerModule::GetModularFeatureName(), &ServerTickRateHandler);
		}
		FDefaultGameModuleImpl::ShutdownModule();
	}

private:
	FMT2ServerTickRateHandler ServerTickRateHandler;
};

IMPLEMENT_PRIMARY_GAME_MODULE(FMetin2Module, Metin2, "Metin2");
