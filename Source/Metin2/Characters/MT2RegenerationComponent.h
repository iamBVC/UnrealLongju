/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "MT2RegenerationComponent.generated.h"

// Server-side HP/SP recovery, ported from the old server's recovery_event / CHARACTER::DistributeSP
// (char.cpp, char_battle.cpp). Every RegenCycle seconds it heals a percentage of max HP - a small
// one while the owner has recently moved/attacked/been hit, a larger one while resting - and refills
// SP by an activity- and class-dependent amount. Split out of AMT2PlayerCharacter so that logic
// lives in one cohesive place and its formula constants are tunable per-Blueprint.
// See Docs/OldGameResearch (regen sections).
UCLASS(ClassGroup = "MT2", meta = (BlueprintSpawnableComponent))
class METIN2_API UMT2RegenerationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMT2RegenerationComponent();

	// Begin/stop the recovery cycle. Server only (the owner drives these from spawn/death/revive).
	UFUNCTION(BlueprintCallable, Category = "Regeneration")
	void StartRegen();

	UFUNCTION(BlueprintCallable, Category = "Regeneration")
	void StopRegen();

	// Old OnMove(): resets the "resting" clock. Called by the owner on movement (checked here from
	// velocity), on attack, and on taking damage, so the next cycle regenerates at the active rate.
	UFUNCTION(BlueprintCallable, Category = "Regeneration")
	void MarkActivity();

protected:
	// How often the recovery cycle fires (old PASSES_PER_SEC(3) = 3s).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Regeneration", meta = (ClampMin = "0.1", Units = "s"))
	float RegenCycle = 3.0f;

	// --- HP: base flat amount + a percentage of max HP, the percentage depending on activity. ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Regeneration|HP")
	float HealthBaseAmount = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Regeneration|HP", meta = (Units = "Percent"))
	float HealthActivePercent = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Regeneration|HP", meta = (Units = "Percent"))
	float HealthRestingPercent = 5.0f;

	// --- SP: flat + percent-of-max, split by class (mage regenerates faster) and activity. ---
	// Each pair is { flat, percentOfMaxSP }.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Regeneration|SP|Mage")
	FVector2D MageActiveSP = FVector2D(2.0f, 1.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Regeneration|SP|Mage")
	FVector2D MageMovingSP = FVector2D(3.0f, 2.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Regeneration|SP|Mage")
	FVector2D MageRestingSP = FVector2D(10.0f, 3.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Regeneration|SP|Melee")
	FVector2D MeleeActiveSP = FVector2D(2.0f, 0.5f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Regeneration|SP|Melee")
	FVector2D MeleeMovingSP = FVector2D(2.0f, 1.0f);
	// Resting SP flat is higher at full HP (old DistributeSP), so both flats are exposed.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Regeneration|SP|Melee")
	FVector2D MeleeRestingSP = FVector2D(9.0f, 1.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Regeneration|SP|Melee")
	float MeleeRestingFlatNotFullHP = 2.0f;

private:
	void TickRegen();
	void TickHealth();
	void TickMana();
	bool WasRecentlyActive() const;
	bool IsMageProfile() const;

	FTimerHandle RegenTimer;
	float LastActivityTimeSeconds = 0.0f;
};
