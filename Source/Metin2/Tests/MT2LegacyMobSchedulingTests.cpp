#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "AIController.h"
#include "AbilitySystemComponent.h"
#include "Abilities/MT2CoreAttributeSet.h"
#include "Components/MT2HealthComponent.h"
#include "Mobs/MT2Mob.h"
#include "Mobs/MT2MobAIComponent.h"
#include "Mobs/MT2MobMovementComponent.h"
#include "Mobs/MT2MobRuntimeSettings.h"
#include "World/MT2WorldSimulationSubsystem.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2LegacyMobSchedulingTest, "Metin2.World.LegacyMobScheduling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2LegacyMobSchedulingTest::RunTest(const FString&)
{
	auto* Settings = GetMutableDefault<UMT2MobRuntimeSettings>();
	TGuardValue<int32> Pulse(Settings->LegacyPulseRate, 25);
	TGuardValue<int32> MovePulses(Settings->LegacyMovePulses, 4);
	TestTrue(TEXT("Four legacy pulses produce 160ms movement steps"), FMath::IsNearlyEqual(Settings->GetMovementInterval(), .16f));
	Settings->LegacyPulseRate = 50;
	TestTrue(TEXT("Pulse setting changes movement cadence"), FMath::IsNearlyEqual(Settings->GetMovementInterval(), .08f));
	Settings->LegacyPulseRate = 25;
	TGuardValue<float> Baseline(Settings->BaselineMobReplicationRate, 1.f);
	TGuardValue<int32> AggressiveMin(Settings->LegacyAggressiveIdleMinSeconds, 1);
	TGuardValue<int32> AggressiveMax(Settings->LegacyAggressiveIdleMaxSeconds, 3);
	TGuardValue<int32> PassiveMin(Settings->LegacyPassiveIdleMinSeconds, 3);
	TGuardValue<int32> PassiveMax(Settings->LegacyPassiveIdleMaxSeconds, 5);
	TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine)); GI->InitializeStandalone();
	UWorld* World = GI->GetWorld(); if (!TestNotNull(TEXT("World"), World)) return false;
	ON_SCOPE_EXIT { GI->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Mob = World->SpawnActor<AMT2Mob>();
	auto* ASC = Mob->GetAbilitySystemComponent();
	ASC->AddAttributeSetSubobject(NewObject<UMT2CoreAttributeSet>(Mob));
	ASC->InitAbilityActorInfo(Mob, Mob);
	ASC->SetNumericAttributeBase(UMT2CoreAttributeSet::GetMaxHealthAttribute(), 100.f);
	ASC->SetNumericAttributeBase(UMT2CoreAttributeSet::GetHealthAttribute(), 100.f);
	Mob->GetHealthComponent()->InitializeWithAbilitySystem(ASC);
	auto* Controller = World->SpawnActor<AAIController>(); Controller->Possess(Mob);
	auto* AI = Mob->GetMobAIComponent();
	AI->AIFlags = 1 << static_cast<int32>(EMT2MobAIFlag::NoMove);
	AI->TickBehavior(Controller, .04f);
	TestTrue(TEXT("Passive idle sleeps 3-5 seconds"), AI->GetLegacyUpdateDelay() >= 3.f && AI->GetLegacyUpdateDelay() <= 5.f);
	const float Deadline = AI->NextDecisionTime;
	AI->TickBehavior(Controller, .04f);
	TestEqual(TEXT("Sleeping idle does not reschedule or search every tick"), AI->NextDecisionTime, Deadline);
	AI->NextDecisionTime = 0.f;
	AI->AIFlags |= 1 << static_cast<int32>(EMT2MobAIFlag::Aggressive);
	AI->AggressiveDetectionRadius = 0.f;
	AI->TickBehavior(Controller, .04f);
	TestTrue(TEXT("Aggressive idle sleeps 1-3 seconds"), AI->GetLegacyUpdateDelay() >= 1.f && AI->GetLegacyUpdateDelay() <= 3.f);
	AI->SetState(EMT2MobAIState::Chasing);
	TestTrue(TEXT("Chase decisions use movement pulse cadence"), FMath::IsNearlyEqual(AI->GetLegacyUpdateDelay(), .16f));
	auto* Simulation = World->GetSubsystem<UMT2WorldSimulationSubsystem>();
	Simulation->NextMobUpdateTimes.Add(Mob, 100.);
	Simulation->WakeMobAI(Mob);
	TestFalse(TEXT("An event clears the old idle deadline"), Simulation->NextMobUpdateTimes.Contains(Mob));
	auto* Movement = CastChecked<UMT2MobMovementComponent>(Mob->GetCharacterMovement());
	Movement->BeginExternalKnockback(FVector::ForwardVector, 200.f, 1.f);
	TestEqual(TEXT("Knockback retains event-driven baseline rate"), Mob->GetNetUpdateFrequency(), 1.f);
	Movement->FinishExternalKnockback();
	TestEqual(TEXT("Knockback completion restores baseline rate"), Mob->GetNetUpdateFrequency(), 1.f);
	auto* Target = World->SpawnActor<AMT2Mob>(); Target->SetActorLocation(FVector(1000,0,110));
	auto* TargetASC = Target->GetAbilitySystemComponent();
	TargetASC->AddAttributeSetSubobject(NewObject<UMT2CoreAttributeSet>(Target));
	TargetASC->InitAbilityActorInfo(Target, Target);
	TargetASC->SetNumericAttributeBase(UMT2CoreAttributeSet::GetMaxHealthAttribute(), 100.f);
	TargetASC->SetNumericAttributeBase(UMT2CoreAttributeSet::GetHealthAttribute(), 100.f);
	Target->GetHealthComponent()->InitializeWithAbilitySystem(TargetASC);
	AI->AIFlags = 1 << static_cast<int32>(EMT2MobAIFlag::AttackMobs);
	AI->Empire = 1; AI->LeashRadius = 10000.f;
	Movement->SetMovementMode(MOVE_Walking); Movement->MaxWalkSpeed = 200.f;
	Mob->SetActorLocation(FVector(0,0,110));
	AI->SetTargetActor(Target);
	AI->TickBehavior(Controller, .16f);
	TestTrue(TEXT("An aggro target starts chase movement"), Movement->IsMoveSegmentInProgress());
	Movement->SetComponentTickEnabled(false);
	AI->TickBehavior(Controller, .16f);
	TestTrue(TEXT("Chase AI recovers suspended movement instead of waiting forever"), Movement->IsMoveSegmentInProgress());
	return true;
}
#endif
