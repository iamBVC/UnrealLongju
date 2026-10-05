/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "MT2SkillCastComponent.generated.h"

class AMT2PlayerCharacter;
class UMT2SkillDefinition;

// Result of a cast attempt: whether the character should now play the skill's cast animation, and
// at which mastery grade. The animation multicast itself stays on the character, since it drives
// the shared attack-motion facing/advance state that basic attacks use too.
USTRUCT()
struct FMT2SkillCastResult
{
	GENERATED_BODY()

	bool bCast = false;
	int32 MasteryGrade = 0;
};

// Server-side skill execution, split out of AMT2PlayerCharacter: the weapon-limitation gate, the
// toggle-off / cooldown / cast-lock gates, target resolution, SP/HP cost, buff application and
// damage. The character keeps only the thin RPC surface (ServerUseSkill delegates here, and the
// cast animation / client-notify RPCs stay on the actor). See char_skill.cpp / the old client
// __RunUseSkill; details in Docs/OldGameResearch/SkillSystem.md.
UCLASS(ClassGroup = "MT2", meta = (BlueprintSpawnableComponent))
class METIN2_API UMT2SkillCastComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMT2SkillCastComponent();

	// Runs every server-side gate and applies the skill's cost/buffs/damage. Returns whether the
	// cast succeeded (so the character can multicast the animation) and the grade to play.
	FMT2SkillCastResult TryUseSkill(int32 SkillVnum);

	// Cast animation length used for the cast lock, from the cast anim's baked motion duration.
	// Public because the character's animation multicast wants the same value.
	float GetSkillCastDuration(const UMT2SkillDefinition* Definition, int32 MasteryGrade) const;

protected:
	// Where in a cast animation the hit lands when the .msa recorded no attack event (buffs, or a
	// skill not yet re-imported). Fraction of the cast duration.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skills", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FallbackHitTimeFraction = 0.4f;

private:
	// Old CanUseWeaponType: WEAPON_LIMITATION skills only fire with a matching weapon equipped.
	bool PassesWeaponLimitation(const UMT2SkillDefinition* Definition) const;
	// Old client __RunUseSkill target rules; false = the cast is refused and costs nothing.
	bool ResolveSkillTarget(const UMT2SkillDefinition* Definition);
	// Old ComputeSkill -> FuncSplashDamage: evaluate the HP poly and apply it to the target(s).
	void ApplySkillDamageToTargets(const UMT2SkillDefinition* Definition, int32 Level);

	// Old game: an attack skill deals its damage at the motion's hit frame(s), not on cast. Schedules
	// one ApplySkillDamageToTargets per recorded hit time (falling back to a single mid-motion hit).
	void ScheduleSkillDamage(const UMT2SkillDefinition* Definition, int32 Level, int32 MasteryGrade);

	AMT2PlayerCharacter* GetPlayer() const;

	// Per-skill cooldown end times and the global cast-animation lock (old IsUsingSkill).
	TMap<int32, double> SkillCooldownUntil;
	double SkillCastLockUntil = 0.0;

	// One-shot timers for the pending cast's hit ticks; cleared before each new cast.
	TArray<FTimerHandle> PendingDamageTimers;
};
