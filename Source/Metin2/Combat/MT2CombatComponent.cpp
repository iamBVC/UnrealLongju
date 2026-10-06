/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Combat/MT2CombatComponent.h"

#include "Abilities/Effects/MT2DamageGameplayEffect.h"
#include "Abilities/MT2GameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Components/CapsuleComponent.h"
#include "Components/MT2HealthComponent.h"
#include "Characters/MT2PlayerCharacter.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Mobs/MT2Mob.h"
#include "Mobs/MT2MobAIComponent.h"
#include "Characters/MT2CharacterBase.h"
#include "Net/UnrealNetwork.h"
#include "Player/MT2PlayerState.h"
#include "Stats/MT2CombatStatsComponent.h"
#include "Stats/MT2PlayerStatFormula.h"
#include "Stats/MT2PrimaryStatsComponent.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogMT2Combat, Log, All);

namespace
{
	constexpr int32 ApplyCriticalChance = 15;
	constexpr int32 ApplyPenetratingChance = 16;
	constexpr int32 ApplyCriticalResistance = 90;
	constexpr int32 ApplyPenetratingResistance = 91;

	struct FResolvedSpecialDamage
	{
		float Damage = 0.0f;
		EMT2DamageDisplayType DisplayType = EMT2DamageDisplayType::Normal;
	};

	int32 ResolveProcChance(int32 Chance, int32 Resistance, bool bSkillDamage)
	{
		// The old server reduces critical/penetrating chance for melee/range/magic skills, while
		// normal attacks use the displayed item chance directly.
		if (bSkillDamage)
		{
			Chance = Chance >= 10 ? 5 + (Chance - 10) / 4 : Chance / 2;
		}
		return FMath::Clamp(Chance - Resistance, 0, 100);
	}

	FResolvedSpecialDamage ResolveSpecialDamage(const AMT2PlayerCharacter* Attacker,
		const AMT2PlayerCharacter* Defender, float PostDefenseDamage, float Defense,
		bool bSkillDamage, EMT2DamageDisplayType DefaultDisplayType)
	{
		FResolvedSpecialDamage Result{PostDefenseDamage, DefaultDisplayType};
		if (!Attacker || DefaultDisplayType == EMT2DamageDisplayType::Poison)
		{
			return Result;
		}

		const int32 CriticalResistance = Defender
			? Defender->GetItemApplyBonus(ApplyCriticalResistance) : 0;
		const int32 PenetratingResistance = Defender
			? Defender->GetItemApplyBonus(ApplyPenetratingResistance) : 0;
		const int32 CriticalChance = ResolveProcChance(
			Attacker->GetItemApplyBonus(ApplyCriticalChance), CriticalResistance, bSkillDamage);
		const int32 PenetratingChance = ResolveProcChance(
			Attacker->GetItemApplyBonus(ApplyPenetratingChance), PenetratingResistance, bSkillDamage);

		const bool bCritical = CriticalChance > 0 && FMath::RandRange(1, 100) <= CriticalChance;
		const bool bPenetrating = PenetratingChance > 0 && FMath::RandRange(1, 100) <= PenetratingChance;
		if (bCritical)
		{
			Result.Damage *= 2.0f;
		}
		if (bPenetrating)
		{
			// Damage is already post-defense here. Adding defense back reproduces the old
			// penetrating-hit rule without running a second damage calculation.
			Result.Damage += Defense;
		}

		Result.DisplayType = bCritical && bPenetrating ? EMT2DamageDisplayType::CriticalPenetrating
			: bCritical ? EMT2DamageDisplayType::Critical
			: bPenetrating ? EMT2DamageDisplayType::Penetrating : DefaultDisplayType;
		return Result;
	}

	// The old flat +50 assumed human chest height, which put the trace point above (or through) the
	// capsule entirely for small creatures like mob 101 - use the actual capsule height instead so
	// this scales correctly for any character size.
	float GetTraceHeight(const AActor* Actor)
	{
		if (const ACharacter* Character = Cast<ACharacter>(Actor))
		{
			if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
			{
				return Capsule->GetScaledCapsuleHalfHeight();
			}
		}
		return 50.0f;
	}

	// Half-width of a target, so a swing that reaches its edge counts as reaching it.
	float GetTraceRadius(const AActor* Actor)
	{
		if (const ACharacter* Character = Cast<ACharacter>(Actor))
		{
			if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
			{
				return Capsule->GetScaledCapsuleRadius();
			}
		}
		return 34.0f;
	}
}

UMT2CombatComponent::UMT2CombatComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	DamageEffectClass = UMT2DamageGameplayEffect::StaticClass();
	// Needed for ServerSetSelectedTarget below to route through the owner's connection.
	SetIsReplicatedByDefault(true);
}

AActor* UMT2CombatComponent::PerformBasicAttack()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return nullptr;
	}

	CurrentComboIndex = AdvanceBasicAttackCombo();
	LastAttackTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	CurrentAttackInputWindow = BasicAttackInterval;
	CurrentAttackWorldAdvance = FVector::ZeroVector;
	OnBasicAttackPerformed.Broadcast(CurrentComboIndex);

	// One swing hits everything in its arc, like the old client's per-instance collision loop.
	// The knockback is a property of the swing, so every victim it lands on gets it.
	const TArray<AActor*> TargetActors = FindBasicAttackTargets();
	const float SwingKnockback = PendingKnockbackDistance;
	AActor* PrimaryTarget = nullptr;
	for (AActor* TargetActor : TargetActors)
	{
		SetPendingKnockback(SwingKnockback);
		if (ApplyBasicAttackDamage(TargetActor) && !PrimaryTarget)
		{
			PrimaryTarget = TargetActor;
		}
	}
	PendingKnockbackDistance = 0.0f;

	UE_LOG(LogMT2Combat, Warning, TEXT("[MT2Attack] PerformBasicAttack Owner='%s' HasAuthority=%s Hits=%d Primary='%s'"),
		*GetNameSafe(OwnerActor),
		OwnerActor->HasAuthority() ? TEXT("true") : TEXT("false"),
		TargetActors.Num(),
		*GetNameSafe(PrimaryTarget));
	return PrimaryTarget;
}

bool UMT2CombatComponent::PerformBasicAttackOnTarget(AActor* TargetActor)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority() || !TargetActor)
	{
		return false;
	}

	const double CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (CurrentTime - LastAttackTime > ComboResetDelay)
	{
		CurrentComboIndex = INDEX_NONE;
	}
	CurrentComboIndex = AdvanceBasicAttackCombo();
	LastAttackTime = CurrentTime;
	CurrentAttackInputWindow = BasicAttackInterval;
	CurrentAttackWorldAdvance = FVector::ZeroVector;
	OnBasicAttackPerformed.Broadcast(CurrentComboIndex);
	return ApplyBasicAttackDamage(TargetActor);
}

void UMT2CombatComponent::ConfigureBasicAttack(
	float Range, float Radius, float Damage, float Interval, int32 ComboLength)
{
	BasicAttackRange = FMath::Max(Range, 0.0f);
	BasicAttackRadius = FMath::Max(Radius, 0.0f);
	BasicAttackDamage = FMath::Max(Damage, 0.0f);
	BasicAttackInterval = FMath::Max(Interval, 0.05f);
	CurrentAttackInputWindow = BasicAttackInterval;
	BasicAttackComboLength = FMath::Max(ComboLength, 1);
	BasicAttackComboLoopStartIndex = FMath::Clamp(
		BasicAttackComboLoopStartIndex, 0, BasicAttackComboLength - 1);
}

void UMT2CombatComponent::ConfigureBasicAttackCombo(int32 ComboLength, int32 LoopStartIndex)
{
	BasicAttackComboLength = FMath::Max(ComboLength, 1);
	BasicAttackComboLoopStartIndex = FMath::Clamp(LoopStartIndex, 0, BasicAttackComboLength - 1);
	if (CurrentComboIndex >= BasicAttackComboLength)
	{
		ResetBasicAttackCombo();
	}
}

bool UMT2CombatComponent::ApplyBasicAttackDamage(AActor* TargetActor)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority() || BasicAttackDamage <= 0.0f || !DamageEffectClass)
	{
		return false;
	}

	UAbilitySystemComponent* SourceAbilitySystem =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OwnerActor);
	UAbilitySystemComponent* TargetAbilitySystem =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(TargetActor);
	if (!SourceAbilitySystem || !TargetAbilitySystem)
	{
		return false;
	}
	if (const AMT2CharacterBase* TargetCharacter = Cast<AMT2CharacterBase>(TargetActor);
		TargetCharacter && !TargetCharacter->IsAttackable())
	{
		return false;
	}
	// Authoritative PvP gate: the sweep already filters by hostility, but a click-selected target
	// bypasses it (FindBasicAttackTargets adds it directly), so the hit itself must re-check.
	// Player-vs-player is only hostile inside a duel or with the attacker in aggressive mode.
	if (const AMT2CharacterBase* OwnerCharacter = Cast<AMT2CharacterBase>(OwnerActor);
		OwnerCharacter && !OwnerCharacter->IsHostileTo(TargetActor))
	{
		return false;
	}
	if (AMT2Mob* TargetMob = Cast<AMT2Mob>(TargetActor))
	{
		TargetMob->SetLastDamageInstigator(OwnerActor);
	}
	if (AMT2PlayerCharacter* TargetPlayer = Cast<AMT2PlayerCharacter>(TargetActor))
	{
		TargetPlayer->SetLastPlayerDamageInstigator(Cast<AMT2PlayerCharacter>(OwnerActor));
	}

	// Landing a hit adopts the victim as the selected target when nothing valid is selected, so the
	// ring latches onto whatever you start swinging at (like the old client keeping the last damaged
	// actor). An existing live selection is left alone. Player-controlled attackers only - a mob's
	// swing shouldn't retarget it.
	if (const APawn* OwnerPawn = Cast<APawn>(OwnerActor);
		OwnerPawn && OwnerPawn->IsPlayerControlled() && !HasValidSelectedTarget())
	{
		SetSelectedTarget(TargetActor);
	}

	FGameplayEffectContextHandle EffectContext = SourceAbilitySystem->MakeEffectContext();
	EffectContext.AddSourceObject(this);
	FGameplayEffectSpecHandle DamageSpec =
		SourceAbilitySystem->MakeOutgoingSpec(DamageEffectClass, 1.0f, EffectContext);
	if (!DamageSpec.IsValid())
	{
		return false;
	}

	// Base (unarmed) hit plus the equipped weapon's rolled attack, mirroring the old game where the
	// weapon's effective physical attack (item_proto Value3/Value4 plus Value5 refinement power,
	// bonuses on equip) adds on top of the character's own attack power. Defense subtracts from the total.
	float AppliedDamage = BasicAttackDamage;
	if (const AMT2CharacterBase* AttackerCharacter = Cast<AMT2CharacterBase>(OwnerActor))
	{
		if (const UMT2CombatStatsComponent* AttackerStats = AttackerCharacter->GetCombatStatsComponent())
		{
			const FMT2CombatStatBonuses& Bonuses = AttackerStats->GetBonuses();
			if (Bonuses.DamageMax > 0.0f)
			{
				AppliedDamage += FMath::FRandRange(Bonuses.DamageMin, Bonuses.DamageMax);
			}
		}
	}

	// Old CalcAttackRating: DEX and level determine how much of physical attack power survives the
	// attack/evasion comparison. Keep the level contribution outside the multiplier, as the server did.
	if (const AMT2PlayerCharacter* AttackerPlayer = Cast<AMT2PlayerCharacter>(OwnerActor))
	{
		const AMT2PlayerState* AttackerState = AttackerPlayer->GetPlayerState<AMT2PlayerState>();
		const int32 AttackerLevel = AttackerState ? AttackerState->GetCharacterLevel() : 1;
		const int32 AttackerDexterity = AttackerPlayer->GetCalculatedPrimaryStats().Dexterity;
		int32 DefenderLevel = 1;
		int32 DefenderDexterity = 0;

		if (const AMT2PlayerCharacter* DefenderPlayer = Cast<AMT2PlayerCharacter>(TargetActor))
		{
			const AMT2PlayerState* DefenderState = DefenderPlayer->GetPlayerState<AMT2PlayerState>();
			DefenderLevel = DefenderState ? DefenderState->GetCharacterLevel() : 1;
			DefenderDexterity = DefenderPlayer->GetCalculatedPrimaryStats().Dexterity;
		}
		else if (const AMT2Mob* DefenderMob = Cast<AMT2Mob>(TargetActor))
		{
			DefenderLevel = DefenderMob->GetMobLevel();
			if (const UMT2PrimaryStatsComponent* DefenderStats = DefenderMob->GetPrimaryStatsComponent())
			{
				DefenderDexterity = DefenderStats->GetCalculatedStats().Dexterity;
			}
		}

		const float LevelAttack = static_cast<float>(AttackerLevel * 2);
		const float AttackRating = MT2PlayerStatFormula::CalculateAttackRating(
			AttackerLevel, AttackerDexterity, DefenderLevel, DefenderDexterity);
		AppliedDamage = (AppliedDamage - LevelAttack) * AttackRating + LevelAttack;
	}
	float TargetDefense = 0.0f;
	if (const AMT2CharacterBase* TargetCharacter = Cast<AMT2CharacterBase>(TargetActor))
	{
		const UMT2CombatStatsComponent* TargetStats = TargetCharacter->GetCombatStatsComponent();
		TargetDefense = TargetStats ? TargetStats->GetCalculatedStats().Defense : 0.0f;
		AppliedDamage = FMath::Max(AppliedDamage - TargetDefense, 1.0f);
	}
	const FResolvedSpecialDamage SpecialDamage = ResolveSpecialDamage(
		Cast<AMT2PlayerCharacter>(OwnerActor), Cast<AMT2PlayerCharacter>(TargetActor),
		AppliedDamage, TargetDefense, false, EMT2DamageDisplayType::Normal);
	AppliedDamage = FMath::Max(SpecialDamage.Damage, 1.0f);
	// Old CHARACTER::Damage records the final post-defense damage on the victim's damage map, which
	// is what decides the exp split and drop ownership. It has to happen *before* the effect is
	// applied: an instant lethal hit runs the victim's death (and its reward payout) synchronously
	// inside ApplyGameplayEffectSpecToTarget below, so a hit recorded afterwards would be a hit the
	// killing blow's own reward split never saw.
	if (AMT2Mob* TargetMob = Cast<AMT2Mob>(TargetActor))
	{
		TargetMob->RecordDamage(OwnerActor, AppliedDamage);
	}
	DamageSpec.Data->SetSetByCallerMagnitude(MT2GameplayTags::Data_Damage, AppliedDamage);
	SourceAbilitySystem->ApplyGameplayEffectSpecToTarget(*DamageSpec.Data.Get(), TargetAbilitySystem);
	if (AMT2PlayerCharacter* AttackerPlayer = Cast<AMT2PlayerCharacter>(OwnerActor))
	{
		AttackerPlayer->ClientShowDamageNumber(
			TargetActor, AppliedDamage, SpecialDamage.DisplayType);
	}
	if (AMT2PlayerCharacter* VictimPlayer = Cast<AMT2PlayerCharacter>(TargetActor))
	{
		VictimPlayer->ClientShowDamageNumber(VictimPlayer, AppliedDamage, SpecialDamage.DisplayType);
	}

	// Instant GameplayEffects resolve synchronously, so by this point a lethal hit has already gone
	// through UMT2HealthComponent::OnDeath -> AMT2Mob::HandleDeath -> PlayMobMotion(FrontDead). Only
	// play a hit-reaction on top of that if the target is still alive, or it would stomp the death motion.
	if (AMT2Mob* TargetMob = Cast<AMT2Mob>(TargetActor); TargetMob && !TargetMob->GetHealthComponent()->IsDead())
	{
		// Matches the old client's ActorInstanceBattle.cpp __HitGood: attacker and victim facing each
		// other (dot product of their forward vectors is negative) is a front hit, otherwise the
		// attacker is roughly behind the victim.
		const float FacingDot = FVector::DotProduct(OwnerActor->GetActorForwardVector(), TargetMob->GetActorForwardVector());
		TargetMob->PlayMobMotion(FacingDot < 0.0f ? EMT2MobMotion::FrontDamage : EMT2MobMotion::BackDamage);

		// Counter-attack: getting hit pulls even a currently-passive mob into combat against its
		// attacker. TickBehavior already knows how to chase and swing once AttackRange (sourced from
		// mob_proto's ATTACK_RANGE) is met - it just needs a target to do it against.
		if (UMT2MobAIComponent* MobAI = TargetMob->GetMobAIComponent())
		{
			MobAI->SetTargetActor(OwnerActor);
		}
	}

	ApplyPendingKnockback(TargetActor);
	OnBasicAttackHit.Broadcast(TargetActor, AppliedDamage);
	return true;
}


void UMT2CombatComponent::ApplyPendingKnockback(AActor* TargetActor)
{
	const float Distance = PendingKnockbackDistance;
	PendingKnockbackDistance = 0.0f;
	if (Distance <= 0.0f)
	{
		return;
	}
	// Only living victims slide; a corpse keeps its death motion in place.
	if (AMT2CharacterBase* Victim = Cast<AMT2CharacterBase>(TargetActor);
		Victim && !Victim->GetHealthComponent()->IsDead())
	{
		Victim->ApplyKnockback(GetOwner(), Distance);
	}
}

bool UMT2CombatComponent::ApplySkillDamage(
	AActor* TargetActor, float RawDamage, EMT2DamageDisplayType DisplayType)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority() || RawDamage <= 0.0f || !DamageEffectClass)
	{
		return false;
	}
	if (const AMT2PlayerCharacter* Player = Cast<AMT2PlayerCharacter>(OwnerActor))
	{
		RawDamage *= 1.0f + Player->GetItemApplyBonus(71) / 100.0f;
	}

	UAbilitySystemComponent* SourceAbilitySystem =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OwnerActor);
	UAbilitySystemComponent* TargetAbilitySystem =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(TargetActor);
	if (!SourceAbilitySystem || !TargetAbilitySystem)
	{
		return false;
	}
	if (const AMT2CharacterBase* TargetCharacter = Cast<AMT2CharacterBase>(TargetActor);
		TargetCharacter && !TargetCharacter->IsAttackable())
	{
		return false;
	}
	// Same authoritative PvP gate as basic attacks: skills aimed via the selected target must not
	// damage other players outside a duel / aggressive mode.
	if (const AMT2CharacterBase* OwnerCharacter = Cast<AMT2CharacterBase>(OwnerActor);
		OwnerCharacter && !OwnerCharacter->IsHostileTo(TargetActor))
	{
		return false;
	}
	if (AMT2Mob* TargetMob = Cast<AMT2Mob>(TargetActor))
	{
		TargetMob->SetLastDamageInstigator(OwnerActor);
	}
	if (AMT2PlayerCharacter* TargetPlayer = Cast<AMT2PlayerCharacter>(TargetActor))
	{
		TargetPlayer->SetLastPlayerDamageInstigator(Cast<AMT2PlayerCharacter>(OwnerActor));
	}

	FGameplayEffectContextHandle EffectContext = SourceAbilitySystem->MakeEffectContext();
	EffectContext.AddSourceObject(this);
	FGameplayEffectSpecHandle DamageSpec =
		SourceAbilitySystem->MakeOutgoingSpec(DamageEffectClass, 1.0f, EffectContext);
	if (!DamageSpec.IsValid())
	{
		return false;
	}

	// The skill poly already folds in the attacker's attack power and weapon; only the victim's
	// defense is applied on top, like the old FuncSplashDamage -> Damage() path.
	float AppliedDamage = RawDamage;
	float TargetDefense = 0.0f;
	if (const AMT2CharacterBase* TargetCharacter = Cast<AMT2CharacterBase>(TargetActor))
	{
		const UMT2CombatStatsComponent* TargetStats = TargetCharacter->GetCombatStatsComponent();
		TargetDefense = TargetStats ? TargetStats->GetCalculatedStats().Defense : 0.0f;
		AppliedDamage = FMath::Max(AppliedDamage - TargetDefense, 1.0f);
	}
	const FResolvedSpecialDamage SpecialDamage = ResolveSpecialDamage(
		Cast<AMT2PlayerCharacter>(OwnerActor), Cast<AMT2PlayerCharacter>(TargetActor),
		AppliedDamage, TargetDefense, true, DisplayType);
	AppliedDamage = FMath::Max(SpecialDamage.Damage, 1.0f);
	// Old CHARACTER::Damage records the final post-defense damage on the victim's damage map, which
	// is what decides the exp split and drop ownership. It has to happen *before* the effect is
	// applied: an instant lethal hit runs the victim's death (and its reward payout) synchronously
	// inside ApplyGameplayEffectSpecToTarget below, so a hit recorded afterwards would be a hit the
	// killing blow's own reward split never saw.
	if (AMT2Mob* TargetMob = Cast<AMT2Mob>(TargetActor))
	{
		TargetMob->RecordDamage(OwnerActor, AppliedDamage);
	}
	DamageSpec.Data->SetSetByCallerMagnitude(MT2GameplayTags::Data_Damage, AppliedDamage);
	SourceAbilitySystem->ApplyGameplayEffectSpecToTarget(*DamageSpec.Data.Get(), TargetAbilitySystem);
	if (AMT2PlayerCharacter* AttackerPlayer = Cast<AMT2PlayerCharacter>(OwnerActor))
	{
		AttackerPlayer->ClientShowDamageNumber(TargetActor, AppliedDamage, SpecialDamage.DisplayType);
	}
	if (AMT2PlayerCharacter* VictimPlayer = Cast<AMT2PlayerCharacter>(TargetActor))
	{
		VictimPlayer->ClientShowDamageNumber(VictimPlayer, AppliedDamage, SpecialDamage.DisplayType);
	}

	if (AMT2Mob* TargetMob = Cast<AMT2Mob>(TargetActor); TargetMob && !TargetMob->GetHealthComponent()->IsDead())
	{
		const float FacingDot = FVector::DotProduct(
			OwnerActor->GetActorForwardVector(), TargetMob->GetActorForwardVector());
		TargetMob->PlayMobMotion(FacingDot < 0.0f ? EMT2MobMotion::FrontDamage : EMT2MobMotion::BackDamage);
		if (UMT2MobAIComponent* MobAI = TargetMob->GetMobAIComponent())
		{
			MobAI->SetTargetActor(OwnerActor);
		}
	}

	ApplyPendingKnockback(TargetActor);
	OnBasicAttackHit.Broadcast(TargetActor, AppliedDamage);
	return true;
}

void UMT2CombatComponent::StartBasicAttackLoop()
{
	if (bBasicAttackHeld || !GetWorld())
	{
		return;
	}

	bBasicAttackHeld = true;
	GetWorld()->GetTimerManager().ClearTimer(BasicAttackTimer);
	ResetBasicAttackCombo();
	if (GetOwner() && !GetOwner()->HasAuthority())
	{
		ServerResetBasicAttackCombo();
	}
	RequestBasicAttack();
	// A valid attack motion schedules its own .msa DirectInputTime in NotifyAttackMotion. Keep a
	// fallback only for an unavailable animation/ability so holding attack never stalls permanently.
	if (!GetWorld()->GetTimerManager().IsTimerActive(BasicAttackTimer))
	{
		ScheduleNextBasicAttack(BasicAttackInterval);
	}
}

void UMT2CombatComponent::NotifyAttackMotion(float InputWindowSeconds, const FVector& WorldAdvance)
{
	if (InputWindowSeconds <= 0.0f)
	{
		return;
	}
	CurrentAttackInputWindow = FMath::Max(InputWindowSeconds, 0.05f);
	CurrentAttackWorldAdvance = WorldAdvance;
	if (!bBasicAttackHeld || !GetWorld()) return;

	ScheduleNextBasicAttack(CurrentAttackInputWindow);
}

void UMT2CombatComponent::StopBasicAttackLoop()
{
	bBasicAttackHeld = false;
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(BasicAttackTimer);
	}
}

void UMT2CombatComponent::RequestBasicAttack()
{
	if (!bBasicAttackHeld)
	{
		return;
	}

	bool bActivated = false;
	if (UAbilitySystemComponent* AbilitySystem =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner()))
	{
		FGameplayTagContainer AttackTags;
		AttackTags.AddTag(MT2GameplayTags::Ability_Attack);
		bActivated = AbilitySystem->TryActivateAbilitiesByTag(AttackTags, true);
	}
	if (!bActivated && bBasicAttackHeld)
	{
		ScheduleNextBasicAttack(BasicAttackRetryInterval);
	}
}

void UMT2CombatComponent::ScheduleNextBasicAttack(float DelaySeconds)
{
	if (!bBasicAttackHeld || !GetWorld())
	{
		return;
	}
	GetWorld()->GetTimerManager().SetTimer(
		BasicAttackTimer, this, &UMT2CombatComponent::RequestBasicAttack,
		FMath::Max(DelaySeconds, 0.01f), false);
}

void UMT2CombatComponent::ResetBasicAttackCombo()
{
	CurrentComboIndex = INDEX_NONE;
	LastAttackTime = -DBL_MAX;
}

int32 UMT2CombatComponent::AdvanceBasicAttackCombo()
{
	if (CurrentComboIndex == INDEX_NONE)
	{
		return 0;
	}
	const int32 ComboLength = FMath::Max(BasicAttackComboLength, 1);
	const int32 NextIndex = CurrentComboIndex + 1;
	return NextIndex < ComboLength
		? NextIndex
		: FMath::Clamp(BasicAttackComboLoopStartIndex, 0, ComboLength - 1);
}

void UMT2CombatComponent::ServerResetBasicAttackCombo_Implementation()
{
	ResetBasicAttackCombo();
}

void UMT2CombatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// Only the owning client draws the ring / reads its own selection, so it doesn't need to go to
	// everyone.
	DOREPLIFETIME_CONDITION(UMT2CombatComponent, SelectedTarget, COND_OwnerOnly);
}

void UMT2CombatComponent::SetSelectedTarget(AActor* NewTarget)
{
	// Set the value locally on both the server (authoritative) and the owning client (optimistic).
	// The optimistic client set matters: client-side readers - notably UpdateCombatEngagement's Tick
	// - call GetSelectedTarget() the very next frame, and without a local value they'd see the not-
	// yet-replicated null and immediately treat the fresh click as "target gone" and clear it. When
	// the server's value replicates back it just reconfirms the same target (or corrects a reject).
	// Simulated proxies never call this, so writing the replicated property here is safe.
	SelectedTarget = NewTarget;
	BroadcastTargetIfChanged(NewTarget);

	// Every server-side consumer - skill targeting, the attack sweep, the range checks - reads the
	// selection there, so a client's pick must reach the server or the player looks untargeted.
	if (GetOwnerRole() == ROLE_AutonomousProxy)
	{
		ServerSetSelectedTarget(NewTarget);
	}
}

void UMT2CombatComponent::ServerSetSelectedTarget_Implementation(AActor* NewTarget)
{
	// The selection is only intent: hostility, range and liveness are still validated server-side
	// wherever the target is actually used.
	SelectedTarget = NewTarget;
	BroadcastTargetIfChanged(NewTarget);
}

void UMT2CombatComponent::OnRep_SelectedTarget()
{
	BroadcastTargetIfChanged(SelectedTarget);
}

void UMT2CombatComponent::BroadcastTargetIfChanged(AActor* NewTarget)
{
	AActor* OldTarget = LastBroadcastTarget.Get();
	if (OldTarget == NewTarget)
	{
		return;
	}
	LastBroadcastTarget = NewTarget;
	OnSelectedTargetChanged.Broadcast(OldTarget, NewTarget);
}

bool UMT2CombatComponent::HasValidSelectedTarget() const
{
	const AActor* Target = SelectedTarget;
	if (!Target)
	{
		return false;
	}
	const AMT2CharacterBase* Character = Cast<AMT2CharacterBase>(Target);
	if (Character && !Character->IsAttackable())
	{
		return false;
	}
	const UAbilitySystemComponent* AbilitySystem =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
	return AbilitySystem && !AbilitySystem->HasMatchingGameplayTag(MT2GameplayTags::Status_Dead);
}

AActor* UMT2CombatComponent::FindFrontTarget(float MaxDistance) const
{
	const AMT2CharacterBase* OwnerCharacter = Cast<AMT2CharacterBase>(GetOwner());
	if (!OwnerCharacter || MaxDistance <= 0.0f)
	{
		return nullptr;
	}
	// Old NEW_GetFrontInstance constants.
	constexpr float HalfFanRotMin = 10.0f;
	constexpr float HalfFanRotMax = 50.0f;
	constexpr float HalfFanRotMinDistance = 1000.0f;
	constexpr float RotPerUnit = (HalfFanRotMax - HalfFanRotMin) / HalfFanRotMinDistance;

	// Every hostile in range is a candidate; the fan then rejects the ones off to the side.
	const TArray<AActor*> Candidates = FindSplashTargets(
		OwnerCharacter->GetActorLocation(), MaxDistance, 0);
	const float OwnerYaw = OwnerCharacter->GetActorRotation().Yaw;

	for (AActor* Candidate : Candidates)
	{
		const FVector ToCandidate = Candidate->GetActorLocation() - OwnerCharacter->GetActorLocation();
		const float Distance = FMath::Min(ToCandidate.Size2D(), HalfFanRotMinDistance);
		const float HalfFanRot = (HalfFanRotMax - HalfFanRotMin) - RotPerUnit * Distance + HalfFanRotMin;
		const float DeltaYaw = FMath::Abs(
			FMath::FindDeltaAngleDegrees(OwnerYaw, ToCandidate.Rotation().Yaw));
		if (DeltaYaw <= HalfFanRot)
		{
			// FindSplashTargets already sorted by distance, so the first hit is the nearest.
			return Candidate;
		}
	}
	return nullptr;
}

TArray<AActor*> UMT2CombatComponent::FindSplashTargets(
	const FVector& Center, float Radius, int32 MaxHits) const
{
	TArray<AActor*> Targets;
	const AMT2CharacterBase* OwnerCharacter = Cast<AMT2CharacterBase>(GetOwner());
	UWorld* World = GetWorld();
	if (!OwnerCharacter || !World || Radius <= 0.0f)
	{
		return Targets;
	}

	FCollisionObjectQueryParams ObjectQuery;
	ObjectQuery.AddObjectTypesToQuery(ECC_Pawn);
	// Mobs live on their own object channel, not Pawn (see AMT2Mob's constructor).
	ObjectQuery.AddObjectTypesToQuery(ECC_GameTraceChannel1);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MT2SkillSplash), false, GetOwner());

	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(
		Overlaps, Center, FQuat::Identity, ObjectQuery, FCollisionShape::MakeSphere(Radius), QueryParams);

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Candidate = Overlap.GetActor();
		if (!Candidate || Targets.Contains(Candidate) || !OwnerCharacter->IsHostileTo(Candidate))
		{
			continue;
		}
		const UAbilitySystemComponent* CandidateAbilitySystem =
			UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Candidate);
		if (!CandidateAbilitySystem
			|| CandidateAbilitySystem->HasMatchingGameplayTag(MT2GameplayTags::Status_Dead))
		{
			continue;
		}
		Targets.Add(Candidate);
	}

	// The old cap keeps whoever the sectree walk reached first; nearest-first is the fair reading.
	Targets.Sort([&Center](const AActor& A, const AActor& B)
	{
		return FVector::DistSquared(Center, A.GetActorLocation())
			< FVector::DistSquared(Center, B.GetActorLocation());
	});
	if (MaxHits > 0 && Targets.Num() > MaxHits)
	{
		Targets.SetNum(MaxHits);
	}
	return Targets;
}

TArray<AActor*> UMT2CombatComponent::FindBasicAttackTargets() const
{
	TArray<AActor*> Targets;
	const AActor* OwnerActor = GetOwner();
	UWorld* World = GetWorld();
	if (!OwnerActor || !World)
	{
		return Targets;
	}

	// A click-selected target is always hit if it is in range, even when the sweep misses it -
	// otherwise clicking a specific mob wouldn't reliably mean attacking THAT mob. It goes in first
	// so it stays the "primary" victim the callers report.
	if (AActor* Target = SelectedTarget)
	{
		const UAbilitySystemComponent* TargetAbilitySystem = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
		const bool bTargetAlive = TargetAbilitySystem && !TargetAbilitySystem->HasMatchingGameplayTag(MT2GameplayTags::Status_Dead);
		const bool bInRange = FVector::DistSquared(OwnerActor->GetActorLocation(), Target->GetActorLocation()) <= FMath::Square(BasicAttackRange);
		const AMT2CharacterBase* TargetCharacter = Cast<AMT2CharacterBase>(Target);
		if (bTargetAlive && bInRange && (!TargetCharacter || TargetCharacter->IsAttackable()))
		{
			Targets.Add(Target);
		}
	}

	// The swing covers a vertical band spanning the attacker's own body height, extending forward to
	// BasicAttackRange and BasicAttackRadius to either side. It is gathered with an *overlap*, not a
	// sweep: a sweep stops at the first blocking hit, so in a crowd the nearest mob shielded everyone
	// behind it and a swing into a pack only landed on one or two. Nothing about a body should stop a
	// blade from reaching the mob behind it, so every hostile standing in the volume is a victim.
	const float AttackerHalfHeight = GetTraceHeight(OwnerActor);
	const FVector Start = OwnerActor->GetActorLocation();
	const FVector Forward = OwnerActor->GetActorForwardVector().GetSafeNormal2D();
	const FVector End = Start + Forward * BasicAttackRange;

	FCollisionObjectQueryParams ObjectQuery;
	ObjectQuery.AddObjectTypesToQuery(ECC_Pawn);
	// Mobs use the "Mob" collision profile/object type (see AMT2Mob's constructor), not Pawn, so
	// they need to be queried for separately or basic attacks would never find them.
	ObjectQuery.AddObjectTypesToQuery(ECC_GameTraceChannel1);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MT2BasicAttack), false, OwnerActor);
	TArray<FOverlapResult> Overlaps;
	// One capsule wide enough to contain the whole swing; candidates are then narrowed to the actual
	// swept volume below, which the overlap shape only approximates.
	World->OverlapMultiByObjectType(
		Overlaps,
		(Start + End) * 0.5f,
		FQuat::Identity,
		ObjectQuery,
		FCollisionShape::MakeCapsule(BasicAttackRange * 0.5f + BasicAttackRadius, AttackerHalfHeight),
		QueryParams);

	TSet<TObjectPtr<AActor>> CheckedActors;
	for (AActor* AlreadyTargeted : Targets)
	{
		CheckedActors.Add(AlreadyTargeted);
	}
	TArray<AActor*> SweptTargets;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Candidate = Overlap.GetActor();
		if (!Candidate || Candidate == OwnerActor || CheckedActors.Contains(Candidate))
		{
			continue;
		}
		CheckedActors.Add(Candidate);

		UAbilitySystemComponent* CandidateAbilitySystem =
			UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Candidate);
		if (!CandidateAbilitySystem || CandidateAbilitySystem->HasMatchingGameplayTag(MT2GameplayTags::Status_Dead))
		{
			continue;
		}
		// Old battle_is_attackable: a swing only lands on the other side. Without this, an attack
		// sweeping several actors would hit the attacker's own faction and NPCs too.
		const AMT2CharacterBase* OwnerCharacter = Cast<AMT2CharacterBase>(OwnerActor);
		if (!OwnerCharacter || !OwnerCharacter->IsHostileTo(Candidate))
		{
			continue;
		}

		// Narrow the round overlap down to the swing itself: how far the target stands from the line
		// the weapon travels, measured on the ground plane and allowing for the target's own width.
		const FVector CandidateLocation = Candidate->GetActorLocation();
		const float DistanceToSwing = FMath::PointDistToSegment(
			FVector(CandidateLocation.X, CandidateLocation.Y, 0.0f),
			FVector(Start.X, Start.Y, 0.0f),
			FVector(End.X, End.Y, 0.0f));
		if (DistanceToSwing > BasicAttackRadius + GetTraceRadius(Candidate))
		{
			continue;
		}

		// Line of sight, against level geometry only. Tracing on Visibility would let another mob's
		// body count as cover, which is the crowd-blocking problem again by a different route.
		FCollisionObjectQueryParams GeometryQuery;
		GeometryQuery.AddObjectTypesToQuery(ECC_WorldStatic);
		GeometryQuery.AddObjectTypesToQuery(ECC_WorldDynamic);
		const FVector TargetLocation = CandidateLocation + FVector(0.0f, 0.0f, GetTraceHeight(Candidate));
		if (World->LineTraceTestByObjectType(
				Start + FVector(0.0f, 0.0f, AttackerHalfHeight), TargetLocation, GeometryQuery, QueryParams))
		{
			continue;
		}
		SweptTargets.Add(Candidate);
	}

	// Nearest first, so the closest victim is the one callers treat as the primary target.
	SweptTargets.Sort([OwnerActor](const AActor& A, const AActor& B)
	{
		return FVector::DistSquared(OwnerActor->GetActorLocation(), A.GetActorLocation())
			< FVector::DistSquared(OwnerActor->GetActorLocation(), B.GetActorLocation());
	});
	Targets.Append(SweptTargets);
	return Targets;
}
