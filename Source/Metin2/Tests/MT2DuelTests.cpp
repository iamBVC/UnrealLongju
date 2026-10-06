#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Duel/MT2DuelComponent.h"
#include "UI/MT2TargetInfoWidget.h"
#include "Config/MT2GameplaySettings.h"
#include "Characters/MT2PlayerCharacter.h"
#include "Player/MT2PlayerState.h"
#include "Components/MT2HealthComponent.h"
#include "Combat/MT2CombatComponent.h"
#include "Abilities/MT2CoreAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "World/MT2MapPresentationActor.h"
#include "World/MT2MapAttributes.h"

namespace
{
	AMT2PlayerCharacter* MakeDuelPlayer(UWorld* World)
	{
		AMT2PlayerCharacter* Pawn = World->SpawnActor<AMT2PlayerCharacter>();
		APlayerController* Controller = World->SpawnActor<APlayerController>();
		AMT2PlayerState* State = World->SpawnActor<AMT2PlayerState>();
		if (!Pawn || !Controller || !State) { return nullptr; }
		Controller->Possess(Pawn);
		Controller->SetPlayerState(State); State->SetOwner(Controller); Pawn->SetPlayerState(State);
		State->GetAbilitySystemComponent()->AddAttributeSetSubobject(State->GetCoreAttributes());
		State->InitializeAbilitySystem(Pawn);
		Pawn->GetHealthComponent()->InitializeWithAbilitySystem(State->GetAbilitySystemComponent());
		State->GetAbilitySystemComponent()->SetNumericAttributeBase(UMT2CoreAttributeSet::GetMaxHealthAttribute(), 1000);
		State->GetAbilitySystemComponent()->SetNumericAttributeBase(UMT2CoreAttributeSet::GetHealthAttribute(), 1000);
		return Pawn;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2DuelLifecycleTest, "Metin2.Combat.Duels.Lifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2DuelLifecycleTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) { return false; }
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
	World->InitializeActorsForPlay(FURL());
	AMT2PlayerCharacter* A = MakeDuelPlayer(World);
	AMT2PlayerCharacter* B = MakeDuelPlayer(World);
	AMT2PlayerCharacter* C = MakeDuelPlayer(World);
	if (!TestNotNull(TEXT("Player A"), A) || !TestNotNull(TEXT("Player B"), B) || !TestNotNull(TEXT("Player C"), C)) { return false; }
	AMT2PlayerState* SA = A->GetPlayerState<AMT2PlayerState>();
	AMT2PlayerState* SB = B->GetPlayerState<AMT2PlayerState>();
	AMT2PlayerState* SC = C->GetPlayerState<AMT2PlayerState>();
	UMT2DuelComponent* DA = SA->GetDuelComponent(); UMT2DuelComponent* DB = SB->GetDuelComponent();
	TestEqual(TEXT("State resolves its current pawn"), SA->GetPawn(), static_cast<APawn*>(A));
	FString Error;
	TestFalse(TEXT("Self challenge rejected"), DA->RequestDuel(SA, Error));
	TestTrue(TEXT("Challenge accepted"), DA->RequestDuel(SB, Error));
	TestFalse(TEXT("Challenge alone enables no duel attacks"), A->IsDuelingWith(B));
	TestTrue(TEXT("Only recipient can accept"), DB->FindDuel(SA)->bCanAccept && !DA->FindDuel(SB)->bCanAccept);
	TestFalse(TEXT("Request rate limited"), DA->RequestDuel(SC, Error));
	DA->LastRequestTime = -1.e30;
	TestFalse(TEXT("Requester cannot unilaterally accept"), DA->RequestDuel(SB, Error));
	TestTrue(TEXT("Reciprocal click starts duel"), DB->RequestDuel(SA, Error));
	TestTrue(TEXT("Bidirectional combat permission"), A->IsPvPEnabledAgainst(B) && B->IsPvPEnabledAgainst(A));
	C->SetActorLocation(FVector(UMT2GameplaySettings::Get().DuelRequestRange + 10000, 0, 0));
	DA->LastRequestTime = -1.e30;
	TestFalse(TEXT("Forged distant challenge rejected"), DA->RequestDuel(SC, Error));
	C->SetActorLocation(FVector::ZeroVector);
	TestTrue(TEXT("Multiple legacy agreements supported"), DA->RequestDuel(SC, Error));
	TestTrue(TEXT("Cancelling another pair preserves fight"), DA->CancelDuel(SC) && A->IsDuelingWith(B));
	TestNull(TEXT("Cancellation mirrored"), SC->GetDuelComponent()->FindDuel(SA));
	AMT2MapPresentationActor* Map = World->SpawnActor<AMT2MapPresentationActor>();
	Map->WorldMin = FVector2D(0,-100); Map->WorldMax = FVector2D(100,0);
	Map->Attributes.Size = FIntPoint(2,2); Map->Attributes.Flags = {4,0,0,0};
	World->GetSubsystem<UMT2MapAttributeSubsystem>()->RegisterMap(Map);
	A->SetActorLocation(FVector(25,-25,1000)); B->SetActorLocation(FVector(75,-25,1000));
	TestFalse(TEXT("Safezones override active duels"), A->IsPvPEnabledAgainst(B));
	B->SetActorLocation(FVector(30,-25,1000));
	TestTrue(TEXT("Leaving safezone restores duel permission"), A->IsPvPEnabledAgainst(B));
	DA->FindMutable(SB)->LastActivity = -100;
	DA->RecordHit(SB);
	TestEqual(TEXT("Hits refresh both expiry timestamps"), DA->FindDuel(SB)->LastActivity, DB->FindDuel(SA)->LastActivity);
	A->GetHealthComponent()->SetHealth(0);
	TestTrue(TEXT("Defeat classified as agreed duel"), DA->HandleDefeat(SB));
	TestFalse(TEXT("Fight stops after victory"), A->IsDuelingWith(B) || B->IsDuelingWith(A));
	TestTrue(TEXT("Loser alone can request revenge"), DA->FindDuel(SB)->bCanAccept && !DB->FindDuel(SA)->bCanAccept);
	TestFalse(TEXT("Dead loser cannot start revenge"), DA->RequestDuel(SB, Error));
	A->GetHealthComponent()->SetHealth(1000);
	DB->LastRequestTime = -1.e30;
	TestFalse(TEXT("Winner cannot restart without loser"), DB->RequestDuel(SA, Error));
	DA->LastRequestTime = -1.e30;
	TestTrue(TEXT("Revenge requires just the loser's agreement"), DA->RequestDuel(SB, Error));
	TestTrue(TEXT("Rematch is active"), A->IsDuelingWith(B));
	DA->FindMutable(SB)->LastActivity = -UMT2GameplaySettings::Get().DuelIdleTimeoutSeconds - 2;
	DA->CleanupExpiredDuels();
	TestNull(TEXT("Idle duel removed"), DA->FindDuel(SB));
	TestNull(TEXT("Idle removal mirrored"), DB->FindDuel(SA));
	DA->LastRequestTime = -1.e30;
	TestTrue(TEXT("Fresh challenge after timeout"), DA->RequestDuel(SB, Error));
	DA->CancelAllDuels();
	TestTrue(TEXT("Disconnect/teardown releases both sides"), DA->Duels.IsEmpty() && DB->Duels.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2DuelDeathTest, "Metin2.Combat.Duels.DeathPenalties",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2DuelDeathTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) { return false; }
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
	World->InitializeActorsForPlay(FURL());
	AMT2PlayerCharacter* Killer = MakeDuelPlayer(World);
	AMT2PlayerCharacter* Victim = MakeDuelPlayer(World);
	if (!TestNotNull(TEXT("Killer"), Killer) || !TestNotNull(TEXT("Victim"), Victim)) { return false; }
	AMT2PlayerState* KS = Killer->GetPlayerState<AMT2PlayerState>();
	AMT2PlayerState* VS = Victim->GetPlayerState<AMT2PlayerState>();
	KS->SetAggressiveMode(true);
	FString Error;
	TestTrue(TEXT("Challenge"), KS->GetDuelComponent()->RequestDuel(VS, Error));
	TestTrue(TEXT("Acceptance"), VS->GetDuelComponent()->RequestDuel(KS, Error));
	FScriptDelegate Death;
	Death.BindUFunction(Victim, TEXT("HandleDeath"));
	Victim->GetHealthComponent()->OnDeath.AddUnique(Death);
	const int32 Karma = KS->GetKarmaPoints();
	TestTrue(TEXT("Lethal skill applied"), Killer->GetCombatComponent()->ApplySkillDamage(Victim, 10000));
	TestTrue(TEXT("Victim died normally"), Victim->GetHealthComponent()->IsDead());
	TestEqual(TEXT("Agreed duel causes no aggressive-kill karma penalty"), KS->GetKarmaPoints(), Karma);
	const FMT2DuelEntry* Duel = VS->GetDuelComponent()->FindDuel(KS);
	TestTrue(TEXT("Real death event transitions into revenge"), Duel && Duel->Phase == EMT2DuelPhase::Revenge && Duel->bCanAccept);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2DuelButtonStateTest, "Metin2.Combat.Duels.ButtonLabels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2DuelButtonStateTest::RunTest(const FString& Parameters)
{
	auto Label = [](const FMT2DuelEntry* Entry) { return UMT2TargetInfoWidget::BuildDuelActionLabel(Entry).ToString(); };
	TestEqual(TEXT("No agreement"), Label(nullptr), FString(TEXT("Duel")));
	FMT2DuelEntry Entry;
	TestEqual(TEXT("Pending challenger"), Label(&Entry), FString(TEXT("Waiting...")));
	Entry.bCanAccept = true;
	TestEqual(TEXT("Challenge recipient"), Label(&Entry), FString(TEXT("Accept duel")));
	Entry.Phase = EMT2DuelPhase::Fighting; Entry.bCanAccept = false;
	TestEqual(TEXT("Active duel"), Label(&Entry), FString(TEXT("Fighting")));
	Entry.Phase = EMT2DuelPhase::Revenge;
	TestEqual(TEXT("Winner returns to the standard label"), Label(&Entry), FString(TEXT("Duel")));
	Entry.bCanAccept = true;
	TestEqual(TEXT("Loser retains revenge action"), Label(&Entry), FString(TEXT("Revenge")));
	return true;
}
#endif
