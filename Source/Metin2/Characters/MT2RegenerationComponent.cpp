/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Characters/MT2RegenerationComponent.h"

#include "Components/MT2HealthComponent.h"
#include "Components/MT2ManaComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Player/MT2PlayerState.h"
#include "Skills/MT2SkillComponent.h"
#include "TimerManager.h"

UMT2RegenerationComponent::UMT2RegenerationComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UMT2RegenerationComponent::StartRegen()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	MarkActivity();
	GetWorld()->GetTimerManager().SetTimer(
		RegenTimer, this, &UMT2RegenerationComponent::TickRegen, RegenCycle, true);
}

void UMT2RegenerationComponent::StopRegen()
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(RegenTimer);
	}
}

void UMT2RegenerationComponent::MarkActivity()
{
	if (const UWorld* World = GetWorld())
	{
		LastActivityTimeSeconds = World->GetTimeSeconds();
	}
}

bool UMT2RegenerationComponent::WasRecentlyActive() const
{
	const UWorld* World = GetWorld();
	return World && (World->GetTimeSeconds() - LastActivityTimeSeconds) < RegenCycle;
}

bool UMT2RegenerationComponent::IsMageProfile() const
{
	// Old DistributeSP mana classes: Shaman, and a Sura who took the Black Magic skill group (2).
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	const AMT2PlayerState* State = OwnerPawn ? OwnerPawn->GetPlayerState<AMT2PlayerState>() : nullptr;
	if (!State)
	{
		return false;
	}
	const EMT2CharacterRace Race = State->GetCharacterAppearance().Race;
	if (Race == EMT2CharacterRace::Shaman)
	{
		return true;
	}
	return Race == EMT2CharacterRace::Sura &&
		State->GetSkillComponent() && State->GetSkillComponent()->GetSkillGroup() == 2;
}

void UMT2RegenerationComponent::TickRegen()
{
	// Mirrors the old TickHealthRegen exactly: mana regen runs first (so its "recently active" check
	// reflects only movement/attack/hits from *before* this cycle - which is why a moving-but-not-
	// fighting player lands on the SP "moving" branch, not the "active" one), then health, which is
	// where movement refreshes the activity clock.
	TickMana();
	TickHealth();
}

void UMT2RegenerationComponent::TickHealth()
{
	UMT2HealthComponent* Health = GetOwner()
		? GetOwner()->FindComponentByClass<UMT2HealthComponent>() : nullptr;
	if (!Health || Health->IsDead() || Health->GetHealth() >= Health->GetMaxHealth())
	{
		return;
	}
	// Movement refreshes the activity clock here, after the full-HP early return above - so exactly
	// as in the old server, a full-HP player moving around does NOT count as "active" for the next
	// cycle's SP branch. Velocity is the reliable server-side movement signal (the input handler
	// only runs on the owning client). Attack and damage are marked by the owner.
	if (GetOwner() && !GetOwner()->GetVelocity().IsNearlyZero())
	{
		MarkActivity();
	}
	const float Percent = WasRecentlyActive() ? HealthActivePercent : HealthRestingPercent;
	const float Amount = HealthBaseAmount + Health->GetMaxHealth() * Percent / 100.0f;
	Health->SetHealth(Health->GetHealth() + Amount);
}

void UMT2RegenerationComponent::TickMana()
{
	UMT2ManaComponent* Mana = GetOwner()
		? GetOwner()->FindComponentByClass<UMT2ManaComponent>() : nullptr;
	const UMT2HealthComponent* Health = GetOwner()
		? GetOwner()->FindComponentByClass<UMT2HealthComponent>() : nullptr;
	if (!Mana || (Health && Health->IsDead()) || Mana->GetMana() >= Mana->GetMaxMana())
	{
		return;
	}

	const float MaxSP = Mana->GetMaxMana();
	const bool bActive = WasRecentlyActive();
	const bool bMoving = GetOwner() && !GetOwner()->GetVelocity().IsNearlyZero();

	// Each SP pair is { flat, percentOfMaxSP }.
	auto Eval = [MaxSP](const FVector2D& Pair) { return Pair.X + MaxSP * Pair.Y / 100.0f; };

	float Amount;
	if (IsMageProfile())
	{
		Amount = bActive ? Eval(MageActiveSP) : bMoving ? Eval(MageMovingSP) : Eval(MageRestingSP);
	}
	else if (bActive)
	{
		Amount = Eval(MeleeActiveSP);
	}
	else if (bMoving)
	{
		Amount = Eval(MeleeMovingSP);
	}
	else
	{
		// Resting flat is higher at full HP (old DistributeSP).
		const bool bFullHealth = Health && Health->GetHealth() >= Health->GetMaxHealth();
		Amount = (bFullHealth ? MeleeRestingSP.X : MeleeRestingFlatNotFullHP) + MaxSP * MeleeRestingSP.Y / 100.0f;
	}
	Mana->SetMana(Mana->GetMana() + Amount);
}
