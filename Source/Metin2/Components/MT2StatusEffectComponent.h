/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "TimerManager.h"
#include "MT2StatusEffectComponent.generated.h"

class UMT2StatusEffectDefinition;
class UTexture2D;

// Which behaviour class drives an effect. The component resolves this to a UMT2StatusEffectDefinition
// (one subclass per kind) that owns the effect's logic - so new effects are "add an enum + a class",
// not "extend a giant switch". StatBonus (the default) is the generic apply-a-stat affect used by
// skills/equipment/admin. See MT2StatusEffectDefinition.h and Docs/OldGameResearch/PotionsAndAffects.md.
UENUM(BlueprintType)
enum class EMT2StatusEffectKind : uint8
{
	StatBonus = 0,     // generic EApplyTypes stat bonus while active (skill buffs, admin, etc.)
	HealthRecovery,    // heals HP from a pool over time (USE_POTION HP potions)
	ManaRecovery,      // heals SP from a pool over time (USE_POTION SP potions)
	AttackSpeed,       // timed +% attack speed buff (USE_ABILITY_UP)
	MovementSpeed,     // timed +% movement speed buff (USE_ABILITY_UP)
	AutoHealthRecovery,// active USE_SPECIAL HP reservoir; inventory owns the actual regeneration
	AutoManaRecovery,  // active USE_SPECIAL MP reservoir; inventory owns the actual regeneration
};

// One buff/debuff, mirroring the old game's CAffect (affect.h): an affect id, one stat apply
// (EApplyTypes ordinal + value, the same apply system items use), optional state-flag bits, a
// remaining duration ticked once per second, and an optional SP drain per tick.
// See Docs/OldGameResearch/AffectSystem.md.
USTRUCT(BlueprintType)
struct METIN2_API FMT2StatusEffect
{
	GENERATED_BODY()

	// Selects the behaviour class (UMT2StatusEffectDefinition subclass) that runs this effect's logic.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status Effect")
	EMT2StatusEffectKind Kind = EMT2StatusEffectKind::StatBonus;

	// Affect id (skill vnum for skill buffs, or a dedicated AFFECT_* id for potions/etc).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status Effect")
	int32 Type = 0;

	// Item that granted this effect. Keeps item-driven affects distinct in the UI even when the old
	// protocol reuses one affect id, and lets the tooltip resolve the original localized item text.
	// Zero means the effect did not originate from an item (skills, admin effects, debuffs, etc.).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status Effect")
	int32 SourceItemVnum = 0;

	// EApplyTypes ordinal (APPLY_ATT_SPEED=7, APPLY_MOV_SPEED=8, ... enums.h:441).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status Effect")
	int32 ApplyType = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status Effect")
	int32 ApplyValue = 0;

	// AFF_* bitmask (stun/poison/invisibility...); purely data for now, consumers filter on it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status Effect")
	int32 Flags = 0;

	// Seconds left. Negative = infinite (never expires; the old game faked this with huge values).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status Effect")
	int32 RemainingSeconds = 0;

	// SP drained every tick; the effect ends when SP runs out (old game: mounts/auras).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status Effect")
	int32 SPCostPerTick = 0;

	// Temporary combat effects opt in to removal when their owner dies. Persistent effects such as
	// book-reading modifiers keep the default false value.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status Effect")
	bool bRemoveOnDeath = false;

	// Icon shown by the status-effect bar; set from the item (or skill) that granted the effect.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status Effect")
	TSoftObjectPtr<UTexture2D> Icon;

	// Atlas sub-region (X, Y, Width, Height in pixels) of Icon, for icons packed in an atlas (skill
	// icons). Width <= 0 means use the whole texture (item icons like potions).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status Effect")
	FVector4 IconRegion = FVector4(0.0f, 0.0f, 0.0f, 0.0f);

	bool IsInfinite() const { return RemainingSeconds < 0; }
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMT2StatusEffectsChangedSignature);

// Server-authoritative buff/debuff list for players AND mobs, replicated so UIs can display it.
// Mirrors the old server's affect list: 1-second ProcessAffect tick, override-on-re-add semantics,
// and persistence via plain data snapshots (players save/restore through the PlayerState JSON).
UCLASS(BlueprintType, ClassGroup = "MT2", meta = (BlueprintSpawnableComponent))
class METIN2_API UMT2StatusEffectComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMT2StatusEffectComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// Server-only. Adds an effect; when bOverride and an effect of the same Type exists, it is
	// refreshed in place (old AddAffect semantics), otherwise a second record stacks.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Status Effects")
	bool AddEffect(const FMT2StatusEffect& Effect, bool bOverride = true);

	// Server-only. Removes every effect with the given affect id. Returns how many were removed.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Status Effects")
	int32 RemoveEffectsByType(int32 Type);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Status Effects")
	void ClearAllEffects();

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Status Effects")
	int32 RemoveEffectsOnDeath();

	UFUNCTION(BlueprintPure, Category = "Status Effects")
	const TArray<FMT2StatusEffect>& GetEffects() const { return Effects; }

	UFUNCTION(BlueprintPure, Category = "Status Effects")
	bool HasEffectFlag(int32 Flag) const;

	// True when any active effect uses the given behaviour kind (used to block non-stacking re-use).
	UFUNCTION(BlueprintPure, Category = "Status Effects")
	bool HasEffectOfKind(EMT2StatusEffectKind Kind) const;

	UFUNCTION(BlueprintPure, Category = "Status Effects")
	bool HasEffectType(int32 Type) const;

	// Magnitude (ApplyValue) of the first active effect with this affect id, or 0. Lets a repeated
	// potion drink merge into one recovery pool instead of stacking separate, faster-draining effects.
	UFUNCTION(BlueprintPure, Category = "Status Effects")
	int32 GetEffectValueByType(int32 Type) const;

	// Server-only value refresh for externally-owned effects such as an automatic potion reservoir.
	bool UpdateEffectValueByType(int32 Type, int32 NewValue);

	// Server-only bulk replace used by persistence restore (no per-effect notifications, one broadcast).
	void RestoreEffects(const TArray<FMT2StatusEffect>& SavedEffects);

	UPROPERTY(BlueprintAssignable, Category = "Status Effects")
	FMT2StatusEffectsChangedSignature OnStatusEffectsChanged;

private:
	// The old server's ProcessAffect: runs once per second while any effect exists - drains SP costs,
	// decrements finite durations, removes expired effects, stops itself when the list empties.
	void ProcessEffects();
	void StartProcessTimerIfNeeded();

	UFUNCTION()
	void OnRep_Effects();

	UPROPERTY(ReplicatedUsing = OnRep_Effects)
	TArray<FMT2StatusEffect> Effects;

	FTimerHandle ProcessTimer;
};
