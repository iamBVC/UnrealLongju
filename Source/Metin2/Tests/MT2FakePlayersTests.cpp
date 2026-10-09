#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"
#include "Components/BoxComponent.h"
#include "Components/MT2HealthComponent.h"
#include "Characters/MT2PlayerCharacter.h"
#include "Combat/MT2CombatComponent.h"
#include "Items/MT2InventoryComponent.h"
#include "Persistence/MT2PersistenceComponent.h"
#include "Player/MT2PlayerController.h"
#include "Player/MT2PlayerState.h"
#include "Mobs/MT2Mob.h"
#include "AbilitySystemComponent.h"
#include "Abilities/MT2CoreAttributeSet.h"
#include "Server/Testing/MT2LoadTestController.h"
#include "Server/Testing/MT2LoadTestSubsystem.h"
#include "Server/Testing/MT2LoadTestSettings.h"
#include "World/MT2PlayerSpatialGridSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2FakePlayersTest, "Metin2.Server.FakePlayers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2FakePlayersTest::RunTest(const FString&)
{
	auto* Settings = GetMutableDefault<UMT2LoadTestSettings>();
	TGuardValue<bool> Enabled(Settings->bEnabled, true);
	TGuardValue<int32> Limit(Settings->MaximumPlayers, 3);
	TGuardValue<int32> Batch(Settings->SpawnBatchSize, 1);
	TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine)); GI->InitializeStandalone();
	UWorld* World = GI->GetWorld(); if (!TestNotNull(TEXT("World"), World)) return false;
	ON_SCOPE_EXIT { GI->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Floor = World->SpawnActor<AActor>(); auto* Box = NewObject<UBoxComponent>(Floor);
	Floor->SetRootComponent(Box); Box->SetBoxExtent(FVector(5000,5000,50));
	Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent(); Floor->SetActorLocation(FVector(0,0,-50));
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	auto* Human = World->SpawnActor<AMT2PlayerController>();
	auto* State = World->SpawnActor<AMT2PlayerState>(); Human->SetPlayerState(State);
	auto* Requester = World->SpawnActor<AMT2PlayerCharacter>(); Human->Possess(Requester);
	auto* Harness = World->GetSubsystem<UMT2LoadTestSubsystem>();
	FString Message;
	State->bIsAdmin = false;
	TestFalse(TEXT("Non-admin cannot spawn test players"), Harness->QueuePlayers(Requester, 1, 500.f, 1, Message));
	State->bIsAdmin = true;
	TestFalse(TEXT("Invalid count rejected"), Harness->QueuePlayers(Requester, 0, 500.f, 1, Message));
	TestTrue(TEXT("Authorized batch queued"), Harness->QueuePlayers(Requester, 3, 500.f, 1, Message));
	TestFalse(TEXT("Queued requests reserve capacity"), Harness->QueuePlayers(Requester, 1, 500.f, 1, Message));
	Harness->Tick(.03f);
	TestEqual(TEXT("Creation is staggered"), Harness->GetQueuedCount(), 2);
	Harness->Tick(.03f); Harness->Tick(.03f);
	if (!TestEqual(TEXT("Three fake players spawned"), Harness->GetPlayerCount(), 3)) return false;
	TArray<TWeakObjectPtr<AMT2PlayerCharacter>> Pawns;
	TArray<TWeakObjectPtr<AMT2PlayerState>> States;
	for (const auto& Entry : Harness->Controllers)
	{
		auto* Controller = Entry.Get(); auto* Pawn = Cast<AMT2PlayerCharacter>(Controller->GetPawn());
		auto* BotState = Controller->GetPlayerState<AMT2PlayerState>();
		if (!TestNotNull(TEXT("Bot pawn"), Pawn) || !TestNotNull(TEXT("Bot PlayerState"), BotState)) return false;
		Pawns.Add(Pawn); States.Add(BotState);
		TestTrue(TEXT("Bot identity"), BotState->IsABot());
		TestFalse(TEXT("Bot never has admin rights"), BotState->IsAdmin());
		TestEqual(TEXT("Warrior appearance"), BotState->GetCharacterAppearance().Race, EMT2CharacterRace::Warrior);
		TestTrue(TEXT("No database identity"), BotState->GetPersistenceComponent()->GetEntityId().IsEmpty());
		TestTrue(TEXT("Avatar replicates normally to real observers"), Pawn->GetIsReplicated() && Pawn->IsReplicatingMovement());
		bool bSword = false;
		for (const auto& Slot : Pawn->GetInventoryComponent()->GetEquipment()) bSword |= Slot.Vnum == 19 && !Slot.IsEmpty();
		TestTrue(TEXT("Sword +9 equipped using real inventory"), bSword);
	}
	auto* Grid = World->GetSubsystem<UMT2PlayerSpatialGridSubsystem>(); Grid->RebuildPlayerGrid();
	TArray<APawn*> Nearby; Grid->GetPlayerPawnsInRadius(FVector::ZeroVector, 10000.f, Nearby);
	for (const auto& Pawn : Pawns) TestTrue(TEXT("Fake avatar participates in spatial queries"), Nearby.Contains(Pawn.Get()));
	auto* Mob = World->SpawnActor<AMT2Mob>(); Mob->SetActorLocation(Pawns[0]->GetActorLocation() + FVector(200,0,0));
	auto* ASC = Mob->GetAbilitySystemComponent();
	ASC->AddAttributeSetSubobject(NewObject<UMT2CoreAttributeSet>(Mob)); ASC->InitAbilityActorInfo(Mob, Mob);
	ASC->SetNumericAttributeBase(UMT2CoreAttributeSet::GetMaxHealthAttribute(), 100.f);
	ASC->SetNumericAttributeBase(UMT2CoreAttributeSet::GetHealthAttribute(), 100.f);
	Mob->GetHealthComponent()->InitializeWithAbilitySystem(ASC);
	auto* First = CastChecked<AMT2LoadTestController>(Pawns[0]->GetController()); First->Think(1.);
	TestEqual(TEXT("AI selects a living monster through native combat"), Pawns[0]->GetCombatComponent()->GetSelectedTarget(), static_cast<AActor*>(Mob));
	TestEqual(TEXT("Clear removes only fake actors"), Harness->ClearPlayers(), 3);
	Grid->RebuildPlayerGrid();
	TestEqual(TEXT("No fake actors remain"), Harness->GetPlayerCount(), 0);
	TestTrue(TEXT("Real admin remains"), IsValid(Requester) && !Requester->IsActorBeingDestroyed());
	for (const auto& Pawn : Pawns) TestTrue(TEXT("Fake pawn destroyed"), !Pawn.IsValid() || Pawn->IsActorBeingDestroyed());
	for (const auto& BotState : States) TestTrue(TEXT("Fake PlayerState destroyed"), !BotState.IsValid() || BotState->IsActorBeingDestroyed());
	TestTrue(TEXT("Another request can queue"), Harness->QueuePlayers(Requester, 3, 500.f, 1, Message));
	Harness->ClearPlayers(); TestEqual(TEXT("Clear cancels queued creation"), Harness->GetQueuedCount(), 0);
	return true;
}
#endif
