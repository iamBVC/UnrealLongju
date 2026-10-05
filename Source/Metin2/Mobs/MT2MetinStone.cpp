/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Mobs/MT2MetinStone.h"

#include "Components/MT2HealthComponent.h"
#include "Core/MT2VnumRegistrySubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Mobs/MT2MobAIComponent.h"
#include "World/MT2WorldSimulationSubsystem.h"

AMT2MetinStone::AMT2MetinStone()
{
	// No AI controller at all: stones never chase, aggro or swing (the old StateBattle rejects
	// them outright). Their entire behavior is the HP-step pulse below.
	AutoPossessAI = EAutoPossessAI::Disabled;
	AIControllerClass = nullptr;
}

void AMT2MetinStone::ConfigureFromDefinition(const FMT2MobDefinition& Definition)
{
	Super::ConfigureFromDefinition(Definition);
	SpawnGroups = Definition.MetinSpawnGroups;
}

void AMT2MetinStone::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority())
	{
		return;
	}
	GetCharacterMovement()->DisableMovement();
	GetHealthComponent()->OnValueChanged.AddUniqueDynamic(
		this, &AMT2MetinStone::HandleHealthChanged);
	GetHealthComponent()->OnDeath.AddUniqueDynamic(this, &AMT2MetinStone::HandleStoneDeath);
	if (UMT2WorldSimulationSubsystem* Simulation = GetWorld()->GetSubsystem<UMT2WorldSimulationSubsystem>())
	{
		Simulation->RegisterMetinStone(this);
	}
}

void AMT2MetinStone::RecordDamage(AActor* Attacker, float Amount)
{
	Super::RecordDamage(Attacker, Amount);
	if (!HasAuthority() || !Attacker)
	{
		return;
	}

	// Every living summon without an enemy defends its stone against the current attacker. Keep an
	// existing target intact so a hit does not pull a mob away from someone it is already fighting.
	for (int32 Index = SpawnedMobs.Num() - 1; Index >= 0; --Index)
	{
		AMT2Mob* Mob = SpawnedMobs[Index].Get();
		if (!Mob)
		{
			SpawnedMobs.RemoveAtSwap(Index);
			continue;
		}
		UMT2MobAIComponent* AI = Mob->GetMobAIComponent();
		if (!Mob->GetHealthComponent()->IsDead() && AI && !AI->GetTargetActor())
		{
			AI->SetTargetActor(Attacker);
		}
	}
}

void AMT2MetinStone::HandleHealthChanged(float OldHealth, float NewHealth)
{
	if (HasAuthority() && NewHealth < OldHealth)
	{
		ProcessStoneBehavior();
	}
}

void AMT2MetinStone::HandleStoneDeath()
{
	if (!HasAuthority())
	{
		return;
	}
	// Old ClearStone -> FuncDeadSpawnedByStone: every mob the stone summoned dies with it, via
	// Dead(NULL) - no killer, so the wave pays out no exp, gold or drops of its own.
	for (const TWeakObjectPtr<AMT2Mob>& Spawned : SpawnedMobs)
	{
		if (AMT2Mob* Mob = Spawned.Get())
		{
			Mob->SetSpawningStone(nullptr);
			Mob->KillWithoutReward();
		}
	}
	SpawnedMobs.Reset();
}

void AMT2MetinStone::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetHealthComponent()->OnValueChanged.RemoveDynamic(
		this, &AMT2MetinStone::HandleHealthChanged);
	if (UWorld* World = GetWorld())
	{
		if (UMT2WorldSimulationSubsystem* Simulation = World->GetSubsystem<UMT2WorldSimulationSubsystem>())
		{
			Simulation->UnregisterMetinStone(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AMT2MetinStone::ProcessStoneBehavior()
{
	const UMT2HealthComponent* Health = GetHealthComponent();
	if (!Health || Health->GetMaxHealth() <= 0.0f)
	{
		return;
	}
	const int32 HealthPercent =
		FMath::FloorToInt(Health->GetHealth() * 100.0f / Health->GetMaxHealth());

	// Fire every crossed step. A single large hit can pass several thresholds before the next
	// scheduler update, including a lethal hit, and none of those waves may be skipped.
	for (const int32 Step : HealthStepPercents)
	{
		if (HealthPercent <= Step && LastFiredStep > Step)
		{
			LastFiredStep = Step;
			// The stone's only "attack": the motion plays in place with no target and no damage -
			// it is the tell for the wave (old SendMovePacket(FUNC_ATTACK)).
			PlayMobMotion(EMT2MobMotion::NormalAttack);
			SpawnWave();
		}
	}
}

void AMT2MetinStone::SpawnWave()
{
	UMT2VnumRegistrySubsystem* Registry = GetWorld() && GetWorld()->GetGameInstance()
		? GetWorld()->GetGameInstance()->GetSubsystem<UMT2VnumRegistrySubsystem>() : nullptr;
	if (!Registry || SpawnGroups.IsEmpty())
	{
		return;
	}

	// The old game picked a random group vnum out of the stone's range for each wave.
	const FMT2MetinSpawnGroup& Group = SpawnGroups[FMath::RandRange(0, SpawnGroups.Num() - 1)];
	AActor* Attacker = GetLastDamageInstigator();

	for (const FMT2MetinSpawnEntry& Entry : Group.Entries)
	{
		const TSubclassOf<AMT2Mob> MobClass = Registry->ResolveMobClass(Entry.MobVnum);
		if (!MobClass)
		{
			continue;
		}
		for (int32 Copy = 0; Copy < FMath::Max(Entry.Count, 1); ++Copy)
		{
			SpawnGroupMember(MobClass, Attacker);
		}
	}
}

void AMT2MetinStone::SpawnGroupMember(TSubclassOf<AMT2Mob> MobClass, AActor* Attacker)
{
	UWorld* World = GetWorld();
	if (!World || !MobClass)
	{
		return;
	}

	// Old SpawnGroup drops members in boxes around the stone; place each one in that ring, on ground
	// and clear of anything already standing there.
	const FVector SpawnLocation = FindGroundSpawnLocation(
		World, MobClass, GetActorLocation(), FMath::FRandRange(SpawnRadiusMin, SpawnRadiusMax), this);

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	AMT2Mob* Spawned = World->SpawnActor<AMT2Mob>(
		MobClass, SpawnLocation, FRotator(0.0, FMath::FRandRange(0.0f, 360.0f), 0.0), SpawnParameters);

	if (!Spawned)
	{
		return;
	}
	// Old SetStone: the two-way link that lets breaking the stone wipe the wave, and banks half of
	// each spawn's exp on the stone.
	Spawned->SetSpawningStone(this);
	SpawnedMobs.Add(Spawned);

	// Old SelectStone(this) handed the stone's attacker to its spawns, so the wave immediately goes
	// for whoever is breaking the stone.
	if (Attacker)
	{
		if (UMT2MobAIComponent* SpawnedAI = Spawned->GetMobAIComponent())
		{
			SpawnedAI->SetTargetActor(Attacker);
		}
	}
}
