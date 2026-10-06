#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UI/MT2FloatingDamageActor.h"
#include "Components/TextRenderComponent.h"
#include "Components/MT2HealthComponent.h"
#include "Characters/MT2PlayerCharacter.h"
#include "Combat/MT2CombatComponent.h"
#include "Player/MT2PlayerState.h"
#include "Mobs/MT2Mob.h"
#include "Abilities/MT2CoreAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2DamageNumberColorTest, "Metin2.Combat.DamageFeedback.Colors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2DamageNumberColorTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) { return false; }
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
	const EMT2DamageDisplayType Types[] = {EMT2DamageDisplayType::Normal, EMT2DamageDisplayType::Critical,
		EMT2DamageDisplayType::Penetrating, EMT2DamageDisplayType::Poison, EMT2DamageDisplayType::CriticalPenetrating};
	const FColor Outgoing[] = {FColor(255,255,35), FColor(255,35,35), FColor(35,35,255), FColor(35,255,35), FColor(255,35,255)};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Types); ++Index)
	{
		AMT2FloatingDamageActor* Actor = World->SpawnActor<AMT2FloatingDamageActor>();
		if (!TestNotNull(TEXT("Damage actor"), Actor)) { return false; }
		UTextRenderComponent* Text = Actor->FindComponentByClass<UTextRenderComponent>();
		if (!TestNotNull(TEXT("Text"), Text)) { return false; }
		Actor->InitializeDamage(42, Types[Index], nullptr);
		TestEqual(TEXT("Existing outgoing RGB unchanged"), Text->TextRenderColor.WithAlpha(255), Outgoing[Index]);
		Actor->InitializeDamage(42, Types[Index], nullptr, true);
		TestEqual(TEXT("Every incoming hit type is red"), Text->TextRenderColor.WithAlpha(255), FColor(255,35,35));
		TestEqual(TEXT("Received amount displayed"), Text->Text.ToString(), FText::AsNumber(42).ToString());
		TestEqual(TEXT("Fade-in begins transparent"), Text->TextRenderColor.A, uint8(0));
		Actor->Tick(0.07f);
		TestTrue(TEXT("Existing fade animates incoming number"), Text->TextRenderColor.A > 0 && Text->TextRenderColor.A < 255);
		Actor->Destroy();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2ReceivedDamageRoutingTest, "Metin2.Combat.DamageFeedback.ReceivedHits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2ReceivedDamageRoutingTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) { return false; }
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
	// Actor::ProcessEvent suppresses RPC dispatch until the world's actors are initialized.
	World->InitializeActorsForPlay(FURL());
	AMT2PlayerCharacter* Victim = World->SpawnActor<AMT2PlayerCharacter>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	AMT2PlayerState* State = World->SpawnActor<AMT2PlayerState>();
	if (!Victim || !Controller || !State) { AddError(TEXT("Player fixture unavailable")); return false; }
	Controller->Possess(Victim);
	Controller->SetPlayerState(State);
	Victim->SetPlayerState(State);
	State->GetAbilitySystemComponent()->AddAttributeSetSubobject(State->GetCoreAttributes());
	State->InitializeAbilitySystem(Victim);
	// A real local-player identity is needed by the engine's no-net-driver local-controller guard.
	Controller->Player = NewObject<ULocalPlayer>(GEngine);
	Victim->GetHealthComponent()->InitializeWithAbilitySystem(State->GetAbilitySystemComponent());
	State->GetAbilitySystemComponent()->SetNumericAttributeBase(UMT2CoreAttributeSet::GetMaxHealthAttribute(), 10000);
	State->GetAbilitySystemComponent()->SetNumericAttributeBase(UMT2CoreAttributeSet::GetHealthAttribute(), 10000);
	AMT2Mob* Mob = World->SpawnActor<AMT2Mob>();
	AMT2PlayerCharacter* Attacker = World->SpawnActor<AMT2PlayerCharacter>();
	AMT2PlayerState* AttackerState = World->SpawnActor<AMT2PlayerState>();
	if (!Mob || !Attacker || !AttackerState) { AddError(TEXT("Attacker fixture unavailable")); return false; }
	Mob->GetAbilitySystemComponent()->InitAbilityActorInfo(Mob, Mob);
	Attacker->SetPlayerState(AttackerState);
	AttackerState->GetAbilitySystemComponent()->AddAttributeSetSubobject(AttackerState->GetCoreAttributes());
	AttackerState->InitializeAbilitySystem(Attacker);
	AttackerState->SetAggressiveMode(true);
	for (AActor* Source : {static_cast<AActor*>(Mob), static_cast<AActor*>(Attacker)})
	{
		UMT2CombatComponent* Combat = Source->FindComponentByClass<UMT2CombatComponent>();
		Combat->ConfigureBasicAttack(200, 50, 50, 1, 1);
		for (bool bSkill : {false, true})
		{
			const float Before = Victim->GetHealthComponent()->GetHealth();
			TestTrue(TEXT("Server accepted hostile hit"), bSkill ? Combat->ApplySkillDamage(Victim, 50)
				: Combat->PerformBasicAttackOnTarget(Victim));
			const float Damage = Before - Victim->GetHealthComponent()->GetHealth();
			int32 Count = 0;
			for (TActorIterator<AMT2FloatingDamageActor> It(World); It; ++It)
			{
				++Count;
				const UTextRenderComponent* Text = It->FindComponentByClass<UTextRenderComponent>();
				TestEqual(TEXT("Victim's number is red"), Text->TextRenderColor.WithAlpha(255), FColor(255,35,35));
				TestEqual(TEXT("Number matches post-defense damage"), Text->Text.ToString(),
					FText::AsNumber(FMath::Max(1, FMath::RoundToInt(Damage))).ToString());
				It->Destroy();
			}
			TestEqual(TEXT("Exactly one victim popup per hit"), Count, 1);
		}
	}
	return true;
}
#endif
