/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Mobs/MT2MobLifecycleComponent.h"

#include "Components/MT2HealthComponent.h"
#include "Core/MT2VnumRegistrySubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "FramePro/FramePro.h"
#include "Mobs/MT2Mob.h"
#include "Mobs/MT2MobTypes.h"
#include "World/MT2WorldSimulationSubsystem.h"

UMT2MobLifecycleComponent::UMT2MobLifecycleComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UMT2MobLifecycleComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopRegeneration();
	Super::EndPlay(EndPlayReason);
}

void UMT2MobLifecycleComponent::Configure(const FMT2MobDefinition& Definition)
{
	RegenerationCycle = FMath::Max(Definition.RegenerationCycle, 0.0f);
	RegenerationPercent = FMath::Clamp(Definition.RegenerationPercent, 0.0f, 100.0f);
	ResurrectionVnum = FMath::Max(Definition.ResurrectionVnum, 0);
	SummonVnum = FMath::Max(Definition.SummonVnum, 0);
}

void UMT2MobLifecycleComponent::StartRegeneration(UMT2HealthComponent* HealthComponent)
{
	StopRegeneration();
	if (!GetOwner() || !GetOwner()->HasAuthority() || !HealthComponent ||
		RegenerationCycle <= 0.0f || RegenerationPercent <= 0.0f)
	{
		return;
	}
	Health = HealthComponent;
	NextRegenerationTime = GetWorld()->GetTimeSeconds() + RegenerationCycle;
	if (UMT2WorldSimulationSubsystem* Simulation = GetWorld()->GetSubsystem<UMT2WorldSimulationSubsystem>())
	{
		Simulation->RegisterRegeneration(this);
	}
}

void UMT2MobLifecycleComponent::StopRegeneration()
{
	if (UWorld* World = GetWorld())
	{
		if (UMT2WorldSimulationSubsystem* Simulation = World->GetSubsystem<UMT2WorldSimulationSubsystem>())
		{
			Simulation->UnregisterRegeneration(this);
		}
	}
	Health.Reset();
	NextRegenerationTime = 0.0;
}

bool UMT2MobLifecycleComponent::SpawnResurrectionMob()
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	if (!Owner || !Owner->HasAuthority() || ResurrectionVnum <= 0 || !GameInstance)
	{
		return false;
	}

	UMT2VnumRegistrySubsystem* Registry = GameInstance->GetSubsystem<UMT2VnumRegistrySubsystem>();
	TSubclassOf<AMT2Mob> MobClass = Registry ? Registry->ResolveMobClass(ResurrectionVnum) : nullptr;
	if (!MobClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("Mob resurrection VNUM %d is not registered."), ResurrectionVnum);
		return false;
	}

	FActorSpawnParameters Parameters;
	Parameters.Owner = Owner->GetOwner();
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	return World->SpawnActor<AMT2Mob>(MobClass, Owner->GetActorTransform(), Parameters) != nullptr;
}

void UMT2MobLifecycleComponent::ProcessRegeneration(double CurrentTime)
{
	FRAMEPRO_NAMED_SCOPE("MT2.MobLifecycle.Regeneration");
	if (CurrentTime < NextRegenerationTime) return;
	NextRegenerationTime = CurrentTime + RegenerationCycle;
	UMT2HealthComponent* HealthComponent = Health.Get();
	if (!HealthComponent || HealthComponent->IsDead())
	{
		return;
	}
	const float Heal = HealthComponent->GetMaxHealth() * RegenerationPercent / 100.0f;
	HealthComponent->SetHealth(HealthComponent->GetHealth() + Heal);
}
