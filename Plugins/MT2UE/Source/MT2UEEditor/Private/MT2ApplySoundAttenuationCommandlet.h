/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"
#include "MT2ApplySoundAttenuationCommandlet.generated.h"

// Safeguard pass over the imported character/world sounds: every USoundBase under /Game/sound
// (except ui/ and ambience/) that has NO attenuation gets the shared SA_MT2_CharacterSounds asset,
// so mob and player sounds (footsteps, swings, hits, voices...) are spatialised and fall off with
// distance instead of playing at full volume across the whole map. Imported .mss motion sounds ship
// with no attenuation at all, which is why a crowded map plays every mob's footsteps everywhere.
//
// The shared attenuation asset is created once and NEVER overwritten - tune its ranges by hand and
// re-running this pass keeps your values. Sounds that already have an attenuation (manually
// assigned) are left untouched. Run with: -run=MT2ApplySoundAttenuation
UCLASS()
class UMT2ApplySoundAttenuationCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMT2ApplySoundAttenuationCommandlet();
	virtual int32 Main(const FString& Params) override;
};
