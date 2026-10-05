/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Components/MT2StatusEffectComponent.h"

#include "Characters/MT2CharacterBase.h"
#include "Components/MT2ManaComponent.h"
#include "Components/MT2StatusEffectDefinition.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

UMT2StatusEffectComponent::UMT2StatusEffectComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UMT2StatusEffectComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// Replicated to everyone (not owner-only): mob debuffs and other players' visible affect flags
	// (stun, poison, invisibility) matter to all clients.
	DOREPLIFETIME(UMT2StatusEffectComponent, Effects);
}

void UMT2StatusEffectComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(ProcessTimer);
	}
	Super::EndPlay(EndPlayReason);
}

bool UMT2StatusEffectComponent::AddEffect(const FMT2StatusEffect& Effect, bool bOverride)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Effect.RemainingSeconds == 0)
	{
		return false;
	}

	AMT2CharacterBase* Target = Cast<AMT2CharacterBase>(GetOwner());
	const UMT2StatusEffectDefinition* Definition = UMT2StatusEffectDefinition::Get(Effect.Kind);

	// Non-stacking effects (potions) are rejected while one of the same kind is active, instead of
	// refreshed. This is what lets the caller "deny the item use if the effect is still active".
	if (Definition->DeniesReapplyWhileActive() && HasEffectOfKind(Effect.Kind))
	{
		return false;
	}
	// Per-effect gate (e.g. HP/SP recovery refuses to apply when the resource is already full).
	if (!Definition->CanApply(Target, Effect))
	{
		return false;
	}

	// Old AddAffect: same-type effect + override -> refresh the existing record in place; otherwise a
	// second record of the same type may stack (intended - e.g. two different applies of one skill).
	if (bOverride)
	{
		for (FMT2StatusEffect& Existing : Effects)
		{
			if (Existing.Type == Effect.Type)
			{
				Existing = Effect;
				Definition->OnApplied(Target, Existing);
				StartProcessTimerIfNeeded();
				OnStatusEffectsChanged.Broadcast();
				return true;
			}
		}
	}

	FMT2StatusEffect& Added = Effects.Add_GetRef(Effect);
	Definition->OnApplied(Target, Added);
	StartProcessTimerIfNeeded();
	OnStatusEffectsChanged.Broadcast();
	return true;
}

int32 UMT2StatusEffectComponent::RemoveEffectsByType(int32 Type)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return 0;
	}
	const int32 Removed = Effects.RemoveAll(
		[Type](const FMT2StatusEffect& Effect) { return Effect.Type == Type; });
	if (Removed > 0)
	{
		OnStatusEffectsChanged.Broadcast();
	}
	return Removed;
}

void UMT2StatusEffectComponent::ClearAllEffects()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Effects.IsEmpty())
	{
		return;
	}
	Effects.Reset();
	OnStatusEffectsChanged.Broadcast();
}

int32 UMT2StatusEffectComponent::RemoveEffectsOnDeath()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return 0;
	}

	AMT2CharacterBase* Target = Cast<AMT2CharacterBase>(GetOwner());
	int32 Removed = 0;
	for (int32 Index = Effects.Num() - 1; Index >= 0; --Index)
	{
		if (!Effects[Index].bRemoveOnDeath)
		{
			continue;
		}
		const FMT2StatusEffect Effect = Effects[Index];
		UMT2StatusEffectDefinition::Get(Effect.Kind)->OnRemoved(Target, Effect);
		Effects.RemoveAt(Index);
		++Removed;
	}
	if (Effects.IsEmpty() && GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(ProcessTimer);
	}
	if (Removed > 0)
	{
		OnStatusEffectsChanged.Broadcast();
	}
	return Removed;
}

bool UMT2StatusEffectComponent::HasEffectFlag(int32 Flag) const
{
	for (const FMT2StatusEffect& Effect : Effects)
	{
		if ((Effect.Flags & Flag) != 0)
		{
			return true;
		}
	}
	return false;
}

bool UMT2StatusEffectComponent::HasEffectOfKind(EMT2StatusEffectKind Kind) const
{
	for (const FMT2StatusEffect& Effect : Effects)
	{
		if (Effect.Kind == Kind)
		{
			return true;
		}
	}
	return false;
}

bool UMT2StatusEffectComponent::HasEffectType(int32 Type) const
{
	return Effects.ContainsByPredicate(
		[Type](const FMT2StatusEffect& Effect) { return Effect.Type == Type; });
}

int32 UMT2StatusEffectComponent::GetEffectValueByType(int32 Type) const
{
	for (const FMT2StatusEffect& Effect : Effects)
	{
		if (Effect.Type == Type)
		{
			return Effect.ApplyValue;
		}
	}
	return 0;
}

bool UMT2StatusEffectComponent::UpdateEffectValueByType(int32 Type, int32 NewValue)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}
	for (FMT2StatusEffect& Effect : Effects)
	{
		if (Effect.Type == Type)
		{
			if (Effect.ApplyValue != NewValue)
			{
				Effect.ApplyValue = NewValue;
				OnStatusEffectsChanged.Broadcast();
			}
			return true;
		}
	}
	return false;
}

void UMT2StatusEffectComponent::RestoreEffects(const TArray<FMT2StatusEffect>& SavedEffects)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	Effects = SavedEffects;
	Effects.RemoveAll([](const FMT2StatusEffect& Effect) { return Effect.RemainingSeconds == 0; });
	StartProcessTimerIfNeeded();
	OnStatusEffectsChanged.Broadcast();
}

void UMT2StatusEffectComponent::StartProcessTimerIfNeeded()
{
	if (GetWorld() && !Effects.IsEmpty() && !GetWorld()->GetTimerManager().IsTimerActive(ProcessTimer))
	{
		GetWorld()->GetTimerManager().SetTimer(
			ProcessTimer, this, &UMT2StatusEffectComponent::ProcessEffects, 1.0f, true);
	}
}

void UMT2StatusEffectComponent::ProcessEffects()
{
	AMT2CharacterBase* Target = Cast<AMT2CharacterBase>(GetOwner());

	// Each effect's per-second behaviour lives in its definition (heal a pool, drain SP, count down a
	// duration...). We only broadcast when the list actually changes; the ticking RemainingSeconds still
	// replicates through the array, but stat-affecting buffs only need a recompute on add/remove.
	bool bChanged = false;
	for (int32 Index = Effects.Num() - 1; Index >= 0; --Index)
	{
		FMT2StatusEffect& Effect = Effects[Index];
		const UMT2StatusEffectDefinition* Definition = UMT2StatusEffectDefinition::Get(Effect.Kind);
		if (Definition->Tick(Target, Effect))
		{
			Definition->OnRemoved(Target, Effect);
			Effects.RemoveAt(Index);
			bChanged = true;
		}
	}

	if (Effects.IsEmpty() && GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(ProcessTimer);
	}
	if (bChanged)
	{
		OnStatusEffectsChanged.Broadcast();
	}
}

void UMT2StatusEffectComponent::OnRep_Effects()
{
	OnStatusEffectsChanged.Broadcast();
}
