/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Audio/MT2SoundPlaybackSubsystem.h"

#include "Components/AudioComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	UMT2SoundPlaybackSubsystem* ResolveSoundSubsystem(const UObject* WorldContextObject)
	{
		const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
		if (!World || World->GetNetMode() == NM_DedicatedServer)
		{
			return nullptr;
		}
		UGameInstance* GameInstance = World->GetGameInstance();
		return GameInstance ? GameInstance->GetSubsystem<UMT2SoundPlaybackSubsystem>() : nullptr;
	}
}

bool UMT2SoundPlaybackSubsystem::CanStart(USoundBase* Sound)
{
	if (!Sound)
	{
		return false;
	}

	for (auto It = LastPlayTimes.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
	constexpr double MinimumRepeatInterval = 0.25;
	const double Now = FPlatformTime::Seconds();
	if (const double* LastPlayTime = LastPlayTimes.Find(Sound);
		LastPlayTime && Now - *LastPlayTime < MinimumRepeatInterval)
	{
		return false;
	}
	LastPlayTimes.FindOrAdd(Sound) = Now;
	return true;
}

UAudioComponent* UMT2SoundPlaybackSubsystem::PlayExclusive2D(
	const UObject* WorldContextObject, USoundBase* Sound,
	float VolumeMultiplier, float PitchMultiplier)
{
	UMT2SoundPlaybackSubsystem* Subsystem = ResolveSoundSubsystem(WorldContextObject);
	if (!Subsystem || !Subsystem->CanStart(Sound))
	{
		return nullptr;
	}
	UAudioComponent* Component = UGameplayStatics::SpawnSound2D(
		WorldContextObject, Sound, VolumeMultiplier, PitchMultiplier);
	return Component;
}

UAudioComponent* UMT2SoundPlaybackSubsystem::PlayExclusiveAtLocation(
	const UObject* WorldContextObject, USoundBase* Sound, const FVector& Location,
	float VolumeMultiplier, float PitchMultiplier, USoundAttenuation* Attenuation)
{
	UMT2SoundPlaybackSubsystem* Subsystem = ResolveSoundSubsystem(WorldContextObject);
	if (!Subsystem || !Subsystem->CanStart(Sound))
	{
		return nullptr;
	}
	UAudioComponent* Component = UGameplayStatics::SpawnSoundAtLocation(
		WorldContextObject, Sound, Location, FRotator::ZeroRotator,
		VolumeMultiplier, PitchMultiplier, 0.0f, Attenuation);
	return Component;
}
