/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Skills/MT2SkillCastComponent.h"
#include "Fishing/MT2FishingComponent.h"

#include "Animation/AnimSequence.h"
#include "Animation/MT2AnimationMotionData.h"
#include "Characters/MT2PlayerCharacter.h"
#include "Combat/MT2CombatComponent.h"
#include "Components/MT2HealthComponent.h"
#include "Components/MT2ManaComponent.h"
#include "Components/MT2StatusEffectComponent.h"
#include "Core/MT2VnumRegistrySubsystem.h"
#include "Engine/World.h"
#include "Items/MT2InventoryComponent.h"
#include "Items/MT2ItemTemplate.h"
#include "Mounts/MT2MountComponent.h"
#include "Player/MT2PlayerState.h"
#include "Skills/MT2SkillComponent.h"
#include "Skills/MT2SkillDefinition.h"
#include "Skills/MT2SkillFormula.h"
#include "Skills/MT2SkillTypes.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogMT2Skill, Log, All);

UMT2SkillCastComponent::UMT2SkillCastComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

AMT2PlayerCharacter* UMT2SkillCastComponent::GetPlayer() const
{
	return Cast<AMT2PlayerCharacter>(GetOwner());
}

FMT2SkillCastResult UMT2SkillCastComponent::TryUseSkill(int32 SkillVnum)
{
	FMT2SkillCastResult Result;

	AMT2PlayerCharacter* Player = GetPlayer();
	if (Player && Player->GetFishingComponent()->HasRodEquipped()) { return Result; }
	AMT2PlayerState* State = Player ? Player->GetPlayerState<AMT2PlayerState>() : nullptr;
	UMT2SkillComponent* Skills = State ? State->GetSkillComponent() : nullptr;
	const UMT2SkillDefinition* Definition = Skills ? Skills->FindSkillDefinition(SkillVnum) : nullptr;
	const int32 Level = Skills ? Skills->GetSkillLevel(SkillVnum) : 0;
	if (!Player || !Definition || Level <= 0)
	{
		return Result;
	}
	if (Definition->SkillType == EMT2SkillType::Horse)
	{
		if (!Player->GetMountComponent() || !Player->GetMountComponent()->CanUseHorseSkills())
		{
			Player->ClientNotifySkillDenied(SkillVnum, EMT2SkillDenyReason::MustRide);
			return Result;
		}
	}
	else if (Player->GetMountComponent() && Player->GetMountComponent()->IsMounted())
	{
		Player->ClientNotifySkillDenied(SkillVnum, EMT2SkillDenyReason::CannotUseMounted);
		return Result;
	}

	// Old client CanUseWeaponType: skills flagged WEAPON_LIMITATION only fire while the matching
	// weapon type is equipped (skilldesc WEAPON column, EWeaponSubTypes names).
	if (!PassesWeaponLimitation(Definition))
	{
		Player->ClientNotifySkillDenied(SkillVnum, EMT2SkillDenyReason::NotMatchableWeapon);
		return Result;
	}

	const bool bToggle = Definition->HasSkillFlag(EMT2SkillFlag::Toggle);
	UMT2StatusEffectComponent* StatusEffects = Player->GetStatusEffectComponent();

	// Toggle skills with the buff running simply turn off - no cost, no cooldown (old UseSkill).
	if (bToggle && StatusEffects && StatusEffects->RemoveEffectsByType(SkillVnum) > 0)
	{
		return Result;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	if (const double* NextUse = SkillCooldownUntil.Find(SkillVnum); NextUse && Now < *NextUse)
	{
		UE_LOG(LogMT2Skill, Verbose, TEXT("[MT2Skill] %d on cooldown for %.1fs more."),
			SkillVnum, *NextUse - Now);
		Player->ClientNotifySkillDenied(SkillVnum, EMT2SkillDenyReason::WaitCooltime);
		return Result;
	}
	// No skill may start while the previous one is still mid-animation (old IsUsingSkill). Sits after
	// the toggle-off return above, since toggling a buff off isn't a cast. Silent: this fires on
	// rapid presses and would just spam the "not yet" line.
	if (Now < SkillCastLockUntil)
	{
		UE_LOG(LogMT2Skill, Verbose, TEXT("[MT2Skill] %d blocked: cast in progress (%.2fs left)."),
			SkillVnum, SkillCastLockUntil - Now);
		return Result;
	}

	// The old client resolves the victim before it ever sends the skill, so a cast refused for
	// having no target costs nothing. This must stay ahead of the SP/HP charge and the cooldown
	// stamp below, or a denied cast would still spend both. It sits after the toggle-off above
	// because switching a buff back off never needs a target.
	if (!ResolveSkillTarget(Definition))
	{
		return Result;
	}

	// Non-attack support skills affect the selected character. CAN_USE_FOR_ME skills with no
	// selected target keep using the caster. Self-only and attack/toggle auras always stay local.
	if (!Definition->HasSkillFlag(EMT2SkillFlag::SelfOnly) &&
		!Definition->HasSkillFlag(EMT2SkillFlag::Attack))
	{
		const UMT2CombatComponent* Combat = Player->GetCombatComponent();
		const AMT2CharacterBase* BuffTarget =
			Combat ? Cast<AMT2CharacterBase>(Combat->GetSelectedTarget()) : nullptr;
		if (BuffTarget)
		{
			StatusEffects = BuffTarget->GetStatusEffectComponent();
		}
	}

	// SP cost from the baked per-level curve (USE_HP_AS_COST skills pay HP instead).
	const float Cost = Definition->SPCostByLevel.GetRichCurveConst()->Eval(Level);
	if (Cost > 0.0f)
	{
		if (Definition->HasSkillFlag(EMT2SkillFlag::UseHPAsCost))
		{
			UMT2HealthComponent* Health = Player->GetHealthComponent();
			if (!Health || Health->GetHealth() <= Cost)
			{
				Player->ClientNotifySkillDenied(SkillVnum, EMT2SkillDenyReason::NotEnoughHP);
				return Result;
			}
			Health->SetHealth(Health->GetHealth() - Cost);
		}
		else
		{
			UMT2ManaComponent* Mana = Player->GetManaComponent();
			if (!Mana || Mana->GetMana() < Cost)
			{
				UE_LOG(LogMT2Skill, Display, TEXT("[MT2Skill] %d needs %.0f SP (have %.0f)."),
					SkillVnum, Cost, Mana ? Mana->GetMana() : 0.0f);
				Player->ClientNotifySkillDenied(SkillVnum, EMT2SkillDenyReason::NotEnoughSP);
				return Result;
			}
			Mana->SetMana(Mana->GetMana() - Cost);
		}
	}

	const float Cooldown = Definition->CooldownByLevel.GetRichCurveConst()->Eval(Level);
	if (Cooldown > 0.0f)
	{
		SkillCooldownUntil.Add(SkillVnum, Now + Cooldown);
		Player->ClientNotifySkillCooldown(SkillVnum, Cooldown);
	}

	// Buff applies become status effects keyed by the skill vnum, exactly like the old
	// AddAffect(dwVnum, bPointOn, ...). Toggles run infinite until switched off.
	if (StatusEffects)
	{
		const TMap<FString, double> Variables =
			Player->BuildSkillFormulaVariables(Definition, Level, EMT2SkillFormulaValue::Random);
		TArray<FMT2StatusEffect> PendingEffects;

		for (const FMT2SkillApplyDefinition& Apply : Definition->Applies)
		{
			if (Apply.PointOnName.IsEmpty())
			{
				continue;
			}
			if (Apply.bGrandMasterOnly &&
				Skills->GetSkillMastery(SkillVnum) < EMT2SkillMastery::GrandMaster)
			{
				continue;
			}

			double RuntimeValue = 0.0;
			const bool bEvaluatedValue = !Apply.ValueFormula.IsEmpty() &&
				MT2SkillFormula::Evaluate(Apply.ValueFormula, Variables, RuntimeValue);
			const float Value = bEvaluatedValue
				? static_cast<float>(RuntimeValue)
				: Apply.ValueByLevel.GetRichCurveConst()->Eval(Level);

			double RuntimeDuration = 0.0;
			const bool bEvaluatedDuration = !Apply.DurationFormula.IsEmpty() &&
				MT2SkillFormula::Evaluate(Apply.DurationFormula, Variables, RuntimeDuration);
			const float Duration = bEvaluatedDuration
				? static_cast<float>(RuntimeDuration)
				: Apply.DurationByLevel.GetRichCurveConst()->Eval(Level);

			const bool bStateOnly = Apply.PointOnName == TEXT("NONE") && Apply.AffectFlags != 0;
			const bool bToggleMarker = bToggle && Apply.PointOnName == TEXT("HP");
			const bool bStatApply = Apply.PointOnName != TEXT("NONE") &&
				Apply.PointOnName != TEXT("HP");
			if ((!bStateOnly && !bToggleMarker && !bStatApply) ||
				(!bStateOnly && !bToggleMarker && FMath::IsNearlyZero(Value)) ||
				(!bToggle && Duration <= 0.0f))
			{
				continue;
			}

			FMT2StatusEffect& Effect = PendingEffects.AddDefaulted_GetRef();
			Effect.Type = SkillVnum;
			Effect.ApplyType = Apply.ApplyType;
			Effect.ApplyValue = FMath::RoundToInt(Value);
			Effect.Flags = Apply.AffectFlags;
			Effect.RemainingSeconds = bToggle ? -1 : FMath::RoundToInt(Duration);
			Effect.bRemoveOnDeath = Apply.bRemoveOnDeath;
			// Show the skill's own icon in the status-effect bar. Skill icons are packed in an atlas, so
			// carry the atlas texture + the current grade's sub-region (the bar draws just that region).
			Effect.Icon = Definition->IconAtlas;
			const int32 IconGrade = FMath::Min(static_cast<int32>(Skills->GetSkillMastery(SkillVnum)), 2);
			const FMT2AtlasRect IconRegion = Definition->GetIconRegion(IconGrade);
			Effect.IconRegion = FVector4(IconRegion.X, IconRegion.Y, IconRegion.Width, IconRegion.Height);
			if (Effect.Icon.IsNull() || !IconRegion.IsValid())
			{
				UE_LOG(LogMT2Skill, Warning,
					TEXT("[MT2Skill] Skill %d applies '%s', but has no valid status-bar icon."),
					SkillVnum, *Apply.PointOnName);
			}
		}

		if (!PendingEffects.IsEmpty())
		{
			// One skill may carry several applies (Berserk: attack + movement speed, Strong Body:
			// defence + movement penalty). Replace the old group, then keep every apply as a
			// separate record sharing one skill id; the UI deduplicates that id to one icon.
			StatusEffects->RemoveEffectsByType(SkillVnum);
			for (const FMT2StatusEffect& Effect : PendingEffects)
			{
				StatusEffects->AddEffect(Effect, false);
			}
		}
	}

	const int32 Grade = FMath::Min(static_cast<int32>(Skills->GetSkillMastery(SkillVnum)), 2);
	// Lock out other skills for the length of this one's cast animation, so they can't be chained
	// with no gap. Uses the same motion duration the animation multicast plays with.
	SkillCastLockUntil = Now + GetSkillCastDuration(Definition, Grade);

	// Old game: the damage lands at the motion's hit frame(s), not on cast - so schedule it rather
	// than applying it here.
	ScheduleSkillDamage(Definition, Level, Grade);

	Result.bCast = true;
	Result.MasteryGrade = Grade;
	return Result;
}

bool UMT2SkillCastComponent::PassesWeaponLimitation(const UMT2SkillDefinition* Definition) const
{
	if (!Definition->ClientAttributeNames.Contains(TEXT("WEAPON_LIMITATION")) ||
		Definition->WeaponLimitationNames.IsEmpty())
	{
		return true;
	}

	static const TMap<FString, int32> WeaponSubTypes = {
		{TEXT("SWORD"), 0}, {TEXT("DAGGER"), 1}, {TEXT("DOUBLE_SWORD"), 1}, {TEXT("BOW"), 2},
		{TEXT("TWO_HANDED"), 3}, {TEXT("BELL"), 4}, {TEXT("FAN"), 5}, {TEXT("ARROW"), 6}};

	int32 EquippedSubType = INDEX_NONE;
	const AMT2PlayerCharacter* Player = GetPlayer();
	const UMT2InventoryComponent* Inventory = Player ? Player->GetInventoryComponent() : nullptr;
	static const TArray<FMT2ItemSlot> EmptyEquipment;
	const TArray<FMT2ItemSlot>& Equipment = Inventory ? Inventory->GetEquipment() : EmptyEquipment;
	// Old EWearPositions: WEAR_BODY=0 ... WEAR_WEAPON=4.
	constexpr int32 WearWeapon = 4;
	if (Equipment.IsValidIndex(WearWeapon) && !Equipment[WearWeapon].IsEmpty())
	{
		UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
		UMT2VnumRegistrySubsystem* Registry = GameInstance
			? GameInstance->GetSubsystem<UMT2VnumRegistrySubsystem>() : nullptr;
		const TSubclassOf<UMT2ItemTemplate> TemplateClass = Registry
			? Registry->ResolveItemTemplateClass(Equipment[WearWeapon].Vnum) : nullptr;
		if (const UMT2ItemWeaponTemplate* Weapon =
			Cast<UMT2ItemWeaponTemplate>(TemplateClass.GetDefaultObject()))
		{
			EquippedSubType = Weapon->WeaponSubType;
		}
	}

	TArray<FString> AllowedNames;
	Definition->WeaponLimitationNames.ParseIntoArray(AllowedNames, TEXT("|"));
	bool bWeaponAllowed = false;
	for (FString Name : AllowedNames)
	{
		Name.TrimStartAndEndInline();
		if (Name == TEXT("EMPTY_HAND"))
		{
			bWeaponAllowed |= EquippedSubType == INDEX_NONE;
		}
		else if (const int32* SubType = WeaponSubTypes.Find(Name))
		{
			bWeaponAllowed |= EquippedSubType == *SubType;
		}
	}
	if (!bWeaponAllowed)
	{
		UE_LOG(LogMT2Skill, Display, TEXT("[MT2Skill] %d needs weapon [%s] (equipped subtype %d)."),
			Definition->Vnum, *Definition->WeaponLimitationNames, EquippedSubType);
	}
	return bWeaponAllowed;
}

void UMT2SkillCastComponent::ScheduleSkillDamage(
	const UMT2SkillDefinition* Definition, int32 Level, int32 MasteryGrade)
{
	UWorld* World = GetWorld();
	AMT2PlayerCharacter* Player = GetPlayer();
	if (!World || !Player || !Definition)
	{
		return;
	}
	// Only ATTACK skills deal damage at a hit frame; buffs already applied their effect on cast.
	if (!Definition->HasSkillFlag(EMT2SkillFlag::Attack))
	{
		return;
	}
	// The cast lock guarantees no two casts overlap, but clear any stragglers to be safe.
	for (FTimerHandle& Handle : PendingDamageTimers)
	{
		World->GetTimerManager().ClearTimer(Handle);
	}
	PendingDamageTimers.Reset();

	// Hit frames come from the cast animation's .msa (one per SPECIAL_ATTACKING event). With none
	// recorded (a buff, or a skill not yet re-imported) fall back to a single hit partway through
	// the motion so the skill still deals its damage.
	const bool bFemale = Player->GetPlayerState<AMT2PlayerState>()
		&& Player->GetPlayerState<AMT2PlayerState>()->GetCharacterAppearance().Sex == EMT2CharacterSex::Female;
	UAnimSequence* CastAnimation =
		Cast<UAnimSequence>(Definition->GetCastAnimation(MasteryGrade, bFemale).LoadSynchronous());
	const UMT2AnimationMotionData* MotionData =
		CastAnimation ? CastAnimation->GetAssetUserData<UMT2AnimationMotionData>() : nullptr;

	TArray<float> HitTimes;
	if (MotionData && MotionData->AttackHitTimes.Num() > 0)
	{
		HitTimes = MotionData->AttackHitTimes;
	}
	else
	{
		HitTimes.Add(GetSkillCastDuration(Definition, MasteryGrade) * FallbackHitTimeFraction);
	}

	for (const float HitTime : HitTimes)
	{
		FTimerHandle& Handle = PendingDamageTimers.AddDefaulted_GetRef();
		// Definition is a stable skill-definition object; Level is captured by value. The weak lambda
		// no-ops if the player/component is torn down before the hit lands.
		World->GetTimerManager().SetTimer(
			Handle,
			FTimerDelegate::CreateWeakLambda(this, [this, Definition, Level]()
			{
				ApplySkillDamageToTargets(Definition, Level);
			}),
			FMath::Max(HitTime, 0.01f),
			false);
	}
}

float UMT2SkillCastComponent::GetSkillCastDuration(
	const UMT2SkillDefinition* Definition, int32 MasteryGrade) const
{
	// A short floor so even a skill with no cast animation still spaces out presses.
	constexpr float MinCastLock = 0.3f;
	const AMT2PlayerCharacter* Player = GetPlayer();
	const AMT2PlayerState* State = Player ? Player->GetPlayerState<AMT2PlayerState>() : nullptr;
	if (!Definition)
	{
		return MinCastLock;
	}
	const bool bFemale = State && State->GetCharacterAppearance().Sex == EMT2CharacterSex::Female;
	UAnimSequence* CastAnimation =
		Cast<UAnimSequence>(Definition->GetCastAnimation(MasteryGrade, bFemale).LoadSynchronous());
	if (!CastAnimation)
	{
		return MinCastLock;
	}
	// Prefer the baked .msa motion duration (what the multicast uses); fall back to the clip length.
	const UMT2AnimationMotionData* MotionData =
		CastAnimation->GetAssetUserData<UMT2AnimationMotionData>();
	const float Duration = MotionData && MotionData->MotionDuration > UE_SMALL_NUMBER
		? MotionData->MotionDuration : CastAnimation->GetPlayLength();
	return FMath::Max(Duration, MinCastLock);
}

bool UMT2SkillCastComponent::ResolveSkillTarget(const UMT2SkillDefinition* Definition)
{
	AMT2PlayerCharacter* Player = GetPlayer();
	UMT2CombatComponent* Combat = Player ? Player->GetCombatComponent() : nullptr;
	if (!Combat)
	{
		return false;
	}
	// Old client __RunUseSkill, in order:
	//   target already selected              -> use it
	//   SEARCH_TARGET                        -> auto-pick the front instance, else NEED_TARGET error
	//   CAN_USE_FOR_ME (or a SELFONLY skill) -> cast on yourself, no victim needed
	//   NEED_TARGET                          -> NEED_TARGET error
	//   otherwise                            -> no victim required (buffs, splash around self)
	if (Combat->GetSelectedTarget())
	{
		return true;
	}
	if (Definition->HasClientAttribute(TEXT("SEARCH_TARGET")))
	{
		// Old NEW_GetFrontInstance(&target, 2000.0f).
		if (AActor* Front = Combat->FindFrontTarget(2000.0f))
		{
			Combat->SetSelectedTarget(Front);
			return true;
		}
		Player->ClientNotifySkillDenied(Definition->Vnum, EMT2SkillDenyReason::NeedTarget);
		return false;
	}
	if (Definition->HasClientAttribute(TEXT("CAN_USE_FOR_ME")) ||
		Definition->HasSkillFlag(EMT2SkillFlag::SelfOnly))
	{
		return true;
	}
	if (Definition->HasClientAttribute(TEXT("NEED_TARGET")))
	{
		Player->ClientNotifySkillDenied(Definition->Vnum, EMT2SkillDenyReason::NeedTarget);
		return false;
	}
	return true;
}

void UMT2SkillCastComponent::ApplySkillDamageToTargets(
	const UMT2SkillDefinition* Definition, int32 Level)
{
	AMT2PlayerCharacter* Player = GetPlayer();
	UMT2CombatComponent* Combat = Player ? Player->GetCombatComponent() : nullptr;
	if (!Combat)
	{
		return;
	}
	// A caster who died between cast and hit frame deals no damage (the old motion is interrupted).
	if (const UMT2HealthComponent* Health = Player->GetHealthComponent(); Health && Health->IsDead())
	{
		return;
	}
	// Old ComputeSkill: only ATTACK skills whose point is HP with a negative amount deal damage.
	if (!Definition->HasSkillFlag(EMT2SkillFlag::Attack))
	{
		return;
	}
	const FMT2SkillApplyDefinition* DamageApply = Definition->Applies.FindByPredicate(
		[](const FMT2SkillApplyDefinition& Apply) { return Apply.PointOnName == TEXT("HP"); });
	if (!DamageApply || DamageApply->ValueFormula.IsEmpty())
	{
		UE_LOG(LogMT2Skill, Verbose,
			TEXT("[MT2Skill] %d: no HP apply, nothing to damage."), Definition->Vnum);
		return;
	}

	// ResolveSkillTarget already refused the cast if this skill needed a victim and had none, so a
	// null target here just means a skill that does not need one (a self buff, or a splash).
	AActor* Target = Combat->GetSelectedTarget();
	// Old ComputeSkill: posTarget is the victim's position, or the caster's own when there is no
	// victim - which is what lets a SPLASH skill hit everything around you with nothing targeted.
	const bool bSplash = Definition->HasSkillFlag(EMT2SkillFlag::Splash) && Definition->SplashRange > 0.0f;
	if (!Target && !bSplash)
	{
		return;
	}
	// The old server re-checks the range with 50cm of slack before computing. TargetRange is 0 on
	// most protos (melee skills rely on the attack's own reach), so fall back to a normal swing.
	const float MaxRange = (Definition->TargetRange > 0.0f ? Definition->TargetRange : 300.0f) + 50.0f;
	if (Target && FVector::Dist2D(Player->GetActorLocation(), Target->GetActorLocation()) > MaxRange)
	{
		UE_LOG(LogMT2Skill, Display,
			TEXT("[MT2Skill] %d: target further than %.0f, no damage dealt."), Definition->Vnum, MaxRange);
		return;
	}

	const TMap<FString, double> Variables =
		Player->BuildSkillFormulaVariables(Definition, Level, EMT2SkillFormulaValue::Random);

	double Amount = 0.0;
	if (!MT2SkillFormula::Evaluate(DamageApply->ValueFormula, Variables, Amount))
	{
		UE_LOG(LogMT2Skill, Warning, TEXT("[MT2Skill] %d: could not evaluate '%s'"),
			Definition->Vnum, *DamageApply->ValueFormula);
		return;
	}
	// Old char_skill.cpp CRUSH: 200 base sliding length, doubled by CRUSH_LONG. (The 400 variant is
	// for NPC attackers, which don't come through this player path.)
	if (Definition->HasSkillFlag(EMT2SkillFlag::Crush) ||
		Definition->HasSkillFlag(EMT2SkillFlag::CrushLong))
	{
		const float SlideLength = Definition->HasSkillFlag(EMT2SkillFlag::CrushLong) ? 400.0f : 200.0f;
		Combat->SetPendingKnockback(SlideLength);
	}

	// An HP poly only means damage while it is negative; a positive one is a heal (old ComputeSkill,
	// and the same rule the tooltip prints). Taking the magnitude unconditionally would turn a heal
	// into a hit.
	if (Amount >= 0.0)
	{
		return;
	}
	const float Damage = static_cast<float>(-Amount);

	// Old ComputeSkill: a SPLASH skill runs FuncSplashDamage over everything around posTarget,
	// capped by lMaxHit. Everything else hits the single target.
	TArray<AActor*> Victims;
	if (bSplash)
	{
		const FVector SplashCenter = Target ? Target->GetActorLocation() : Player->GetActorLocation();
		Victims = Combat->FindSplashTargets(SplashCenter, Definition->SplashRange, Definition->MaxHits);
	}
	if (Victims.IsEmpty() && Target)
	{
		Victims.Add(Target);
	}

	const float SkillKnockback = Combat->GetPendingKnockback();
	for (AActor* Victim : Victims)
	{
		// The CRUSH slide belongs to the cast, so every victim of the splash gets it.
		Combat->SetPendingKnockback(SkillKnockback);
		Combat->ApplySkillDamage(Victim, Damage);
	}
	Combat->SetPendingKnockback(0.0f);
}
