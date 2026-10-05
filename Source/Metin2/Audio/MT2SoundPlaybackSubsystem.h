/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MT2SoundPlaybackSubsystem.generated.h"

class UAudioComponent;
class USoundAttenuation;
class USoundBase;

// Local audio gate shared by every gameplay/UI caller. A sound already playing on this client is
// not started again, preventing same-frame loot, hit and button events from stacking their volume.
UCLASS()
class METIN2_API UMT2SoundPlaybackSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UAudioComponent* PlayExclusive2D(
		const UObject* WorldContextObject, USoundBase* Sound,
		float VolumeMultiplier = 1.0f, float PitchMultiplier = 1.0f);

	static UAudioComponent* PlayExclusiveAtLocation(
		const UObject* WorldContextObject, USoundBase* Sound, const FVector& Location,
		float VolumeMultiplier = 1.0f, float PitchMultiplier = 1.0f,
		USoundAttenuation* Attenuation = nullptr);

private:
	bool CanStart(USoundBase* Sound);

	TMap<TWeakObjectPtr<USoundBase>, double> LastPlayTimes;
};
