#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerController.h"
#include "Characters/MT2PlayerCharacter.h"
#include "Components/SceneComponent.h"
#include "Components/MT2HealthComponent.h"
#include "AbilitySystemComponent.h"
#include "Abilities/MT2CoreAttributeSet.h"
#include "Dungeons/MT2DungeonRoom.h"
#include "Dungeons/MT2DevilTowerRoom.h"
#include "Dungeons/MT2DungeonSubsystem.h"
#include "Mobs/MT2Mob.h"
#include "Player/MT2PlayerState.h"
#include "Persistence/MT2PersistenceComponent.h"
#include "Quests/MT2QuestExpression.h"
#include "UObject/StrongObjectPtr.h"
#include "TimerManager.h"

namespace MT2DungeonFixture
{
	struct FRig
	{
		TStrongObjectPtr<UGameInstance> Instance{NewObject<UGameInstance>(GEngine)};
		UWorld* World = nullptr;
		UWorld* OriginalWorld = GWorld;
		FRig()
		{
			Instance->InitializeStandalone(); World = Instance->GetWorld();
			World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FRig()
		{
			World->EndPlay(EEndPlayReason::Quit); Instance->Shutdown();
			World->DestroyWorld(false); GEngine->DestroyWorldContext(World); GWorld = OriginalWorld;
		}
		AMT2DungeonRoom* Room(FName Id, FVector Location = FVector::ZeroVector, TSubclassOf<AMT2DungeonRoom> Class = AMT2DungeonRoom::StaticClass())
		{
			const FTransform Transform(Location);
			auto* Result = World->SpawnActorDeferred<AMT2DungeonRoom>(Class, Transform);
			Result->RoomId = Id; Result->FinishSpawning(Transform); return Result;
		}
		AMT2PlayerState* Player(const FString& Id, FVector Location = FVector::ZeroVector, bool bCharacter = false)
		{
			auto* Controller = World->SpawnActor<APlayerController>();
			auto* State = World->SpawnActor<AMT2PlayerState>(); Controller->SetPlayerState(State);
			State->GetPersistenceComponent()->ConfigurePersistence(TEXT("character"), Id, TEXT("account"));
			APawn* Pawn = bCharacter ? World->SpawnActor<AMT2PlayerCharacter>() : World->SpawnActor<APawn>();
			if (!Pawn->GetRootComponent())
			{
				auto* Root = NewObject<USceneComponent>(Pawn); Pawn->SetRootComponent(Root); Root->RegisterComponent();
			}
			Pawn->SetActorLocation(Location); Controller->Possess(Pawn); return State;
		}
		AMT2Mob* Mob(FVector Location = FVector::ZeroVector)
		{
			auto* Mob = World->SpawnActor<AMT2Mob>(); Mob->SetActorLocation(Location);
			auto* ASC = Mob->GetAbilitySystemComponent();
			ASC->AddAttributeSetSubobject(NewObject<UMT2CoreAttributeSet>(Mob)); ASC->InitAbilityActorInfo(Mob, Mob);
			ASC->SetNumericAttributeBase(UMT2CoreAttributeSet::GetMaxHealthAttribute(), 100.f);
			ASC->SetNumericAttributeBase(UMT2CoreAttributeSet::GetHealthAttribute(), 100.f);
			Mob->GetHealthComponent()->InitializeWithAbilitySystem(ASC); return Mob;
		}
		void TimerTick(float Seconds) { ++GFrameCounter; World->GetTimerManager().Tick(Seconds); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2DungeonSharedRoomTest, "Metin2.Dungeons.SharedRoom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2DungeonSharedRoomTest::RunTest(const FString&)
{
	MT2DungeonFixture::FRig Rig;
	auto* Room = Rig.Room(TEXT("Tower.Floor1")); auto* Next = Rig.Room(TEXT("Tower.Floor2"), FVector(5000,0,0));
	auto* Registry = Rig.World->GetSubsystem<UMT2DungeonSubsystem>();
	auto* A = Rig.Player(TEXT("A")); auto* B = Rig.Player(TEXT("B"));
	if (!TestNotNull(TEXT("Possessed player pawn"), A->GetPawn())) return false;
	TestTrue(TEXT("First participant enters stage"), Room->JoinPlayer(A));
	TestTrue(TEXT("Second participant shares same stage"), Room->JoinPlayer(B));
	TestTrue(TEXT("Duplicate entry is idempotent"), Room->JoinPlayer(A));
	TestEqual(TEXT("One shared participant count"), Room->GetParticipantCount(), 2);
	TestEqual(TEXT("Same logical room for both players"), Registry->GetPlayerRoom(B), Room);
	TestFalse(TEXT("Cannot join a distant room by request alone"), Next->JoinPlayer(A));
	auto* Door = Rig.World->SpawnActor<AActor>(); Room->Doors.Add(Door);
	TestTrue(TEXT("Encounter starts once"), Room->StartEncounter());
	const int32 Epoch = Room->GetRunSerial();
	TestFalse(TEXT("Second start cannot duplicate encounter"), Room->StartEncounter());
	TestEqual(TEXT("Restart attempt preserves run serial"), Room->GetRunSerial(), Epoch);
	TestFalse(TEXT("Shared door closed during encounter"), Door->IsHidden());
	auto* Late = Rig.Player(TEXT("late")); const double Countdown = Room->GetSecondsRemaining();
	TestTrue(TEXT("Late arrivals share active encounter"), Room->JoinPlayer(Late));
	TestEqual(TEXT("Joining cannot reset shared countdown"), Room->GetSecondsRemaining(), Countdown);
	auto* First = Rig.Mob(); auto* Boss = Rig.Mob(FVector(300,0,0));
	TestTrue(TEXT("Objective registered"), Room->RegisterEnemy(First));
	TestTrue(TEXT("Boss uses same tracking"), Room->RegisterEnemy(Boss));
	TestTrue(TEXT("Duplicate registration cannot double count"), Room->RegisterEnemy(Boss));
	TestEqual(TEXT("Shared remaining count"), Room->GetRemainingEnemies(), 2);
	First->GetHealthComponent()->SetHealth(0.f);
	TestTrue(TEXT("One kill does not complete entire room"), Room->GetPhase() == EMT2DungeonRoomPhase::Active);
	Boss->GetHealthComponent()->SetHealth(0.f);
	TestTrue(TEXT("All objectives complete shared encounter"), Room->GetPhase() == EMT2DungeonRoomPhase::Completed);
	TestTrue(TEXT("Shared door opens"), Door->IsHidden());
	TestTrue(TEXT("Initial participant receives own entitlement"), Room->TryClaimCompletion(A));
	TestFalse(TEXT("Replay cannot duplicate completion entitlement"), Room->TryClaimCompletion(A));
	TestTrue(TEXT("Second participant entitlement independent"), Room->TryClaimCompletion(B));
	TestTrue(TEXT("Entry before completion receives own entitlement"), Room->TryClaimCompletion(Late));
	Room->LeavePlayer(Late); Room->JoinPlayer(Late);
	TestFalse(TEXT("Rejoining cannot duplicate a claimed entitlement"), Room->TryClaimCompletion(Late));
	auto* After = Rig.Player(TEXT("after"));
	TestTrue(TEXT("Entry after completion can share the completed room"), Room->JoinPlayer(After));
	TestFalse(TEXT("Entry after completion cannot inherit entitlement"), Room->TryClaimCompletion(After));
	TestFalse(TEXT("Occupied room cannot reset"), Room->ResetWhenEmpty());
	Room->NextRoom = Next;
	TestTrue(TEXT("Stage advance stays in same world"), Room->AdvancePlayer(A));
	TestEqual(TEXT("Stage membership transfers"), Registry->GetPlayerRoom(A), Next);
	Room->LeavePlayer(B); Room->LeavePlayer(Late); Room->LeavePlayer(After);
	TestTrue(TEXT("Only empty room resets"), Room->ResetWhenEmpty());
	TestTrue(TEXT("Room returns to idle"), Room->GetPhase() == EMT2DungeonRoomPhase::Idle);
	TestTrue(TEXT("Room-owned corpses cleaned without affecting next stage"), First->IsActorBeingDestroyed() && Boss->IsActorBeingDestroyed());
	TestEqual(TEXT("Sibling stage retains player"), Next->GetParticipantCount(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2DungeonLifetimeTest, "Metin2.Dungeons.Lifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2DungeonLifetimeTest::RunTest(const FString&)
{
	MT2DungeonFixture::FRig Rig;
	auto* Room = Rig.Room(TEXT("stage")); auto* Player = Rig.Player(TEXT("player"));
	Room->EncounterSeconds = .1f; Room->EmptyResetSeconds = .1f;
	TestTrue(TEXT("Join before timeout test"), Room->JoinPlayer(Player));
	TestTrue(TEXT("Start timed encounter"), Room->StartEncounter());
	Rig.TimerTick(.1f); Rig.TimerTick(.11f);
	TestTrue(TEXT("Room timer survives independently of player quest timers"), Room->GetPhase() == EMT2DungeonRoomPhase::Failed);
	TestFalse(TEXT("Failed room denies new admission"), Room->JoinPlayer(Rig.Player(TEXT("late"))));
	Player->GetPawn()->Destroy();
	TestEqual(TEXT("Pawn destruction removes membership even with living PlayerState"), Room->GetParticipantCount(), 0);
	Rig.TimerTick(.1f); Rig.TimerTick(.11f);
	TestTrue(TEXT("Empty failed room resets through grace timer"), Room->GetPhase() == EMT2DungeonRoomPhase::Idle);
	auto* Other = Rig.Player(TEXT("other")); Room->JoinPlayer(Other); Room->StartEncounter();
	auto* Mob = Rig.Mob(); Room->RegisterEnemy(Mob); Mob->Destroy();
	TestTrue(TEXT("Live objective despawn is failure, not completion"), Room->GetPhase() == EMT2DungeonRoomPhase::Failed);
	Room->LeavePlayer(Other); Room->ResetWhenEmpty(); Room->JoinPlayer(Other); Room->StartEncounter();
	auto* OwnedMob = Rig.Mob();
	TestTrue(TEXT("Enemy registered before local room removal"), Room->RegisterEnemy(OwnedMob));
	Room->Destroy();
	TestTrue(TEXT("Local room removal cleans owned live enemies"), OwnedMob->IsActorBeingDestroyed());
	TestNull(TEXT("Room destruction removes registry identity"), Rig.World->GetSubsystem<UMT2DungeonSubsystem>()->FindRoom(TEXT("stage")));
	TestNull(TEXT("Room destruction clears membership"), Rig.World->GetSubsystem<UMT2DungeonSubsystem>()->GetPlayerRoom(Other));
	Rig.TimerTick(10.f); // Destroyed room must have no surviving callbacks.
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2DungeonQuestAndValidationTest, "Metin2.Dungeons.QuestAndValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2DungeonQuestAndValidationTest::RunTest(const FString&)
{
	MT2DungeonFixture::FRig Rig;
	auto* Room = Rig.Room(TEXT("tower")); auto* Player = Rig.Player(TEXT("player"), FVector::ZeroVector, true);
	FMT2QuestContext Context; Context.PlayerState = Player; Context.Player = Cast<AMT2PlayerCharacter>(Player->GetPawn());
	bool bOk = false;
	TestEqual(TEXT("No dungeon context outside a room"), FMT2QuestExpression::Evaluate(TEXT("pc.in_dungeon()"), Context, bOk).Number, 0.0);
	TestTrue(TEXT("Player enters logical room"), Room->JoinPlayer(Player));
	TestEqual(TEXT("Quest condition follows room membership"), FMT2QuestExpression::Evaluate(TEXT("pc.in_dungeon()"), Context, bOk).Number, 1.0);
	TestTrue(TEXT("Dungeon expression supported"), bOk);
	Context.Player->SetActorLocation(FVector(-2000,0,0));
	TestNull(TEXT("Physical exit clears membership"), Rig.World->GetSubsystem<UMT2DungeonSubsystem>()->GetPlayerRoom(Player));
	Context.Player->SetActorLocation(FVector(-1005,0,0));
	TestFalse(TEXT("Capsule-edge entry precedes center crossing"), Room->ContainsLocation(Context.Player->GetActorLocation()));
	TestEqual(TEXT("Capsule overlap admits player without per-frame polling"), Rig.World->GetSubsystem<UMT2DungeonSubsystem>()->GetPlayerRoom(Player), Room);
	Context.Player->SetActorLocation(FVector::ZeroVector);
	AddExpectedError(TEXT("requires a unique nonempty RoomId"), EAutomationExpectedErrorFlags::Contains, 1);
	auto* Duplicate = Rig.Room(TEXT("tower"));
	TestEqual(TEXT("Duplicate cannot replace existing shared stage"), Rig.World->GetSubsystem<UMT2DungeonSubsystem>()->FindRoom(TEXT("tower")), Room);
	Duplicate->Destroy();
	TestEqual(TEXT("Rejected room teardown cannot remove original"), Rig.World->GetSubsystem<UMT2DungeonSubsystem>()->FindRoom(TEXT("tower")), Room);
	FMT2DungeonSpawn InvalidSpawn; InvalidSpawn.Vnum = -1; Room->Spawns.Add(InvalidSpawn);
	AddExpectedError(TEXT("failed encounter spawn"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Invalid spawn explicitly fails initialization"), Room->StartEncounter());
	TestTrue(TEXT("Failed spawn cannot auto-complete stage"), Room->GetPhase() == EMT2DungeonRoomPhase::Failed);
	TestFalse(TEXT("Failure cannot grant completion entitlement"), Room->TryClaimCompletion(Player));
	Room->LeavePlayer(Player);
	TestEqual(TEXT("Quest condition clears on leave"), FMT2QuestExpression::Evaluate(TEXT("pc.in_dungeon()"), Context, bOk).Number, 0.0);
	TestFalse(TEXT("Physical occupancy prevents reset even without admission"), Room->ResetWhenEmpty());
	Context.Player->SetActorLocation(FVector(-5000,0,0));
	TestTrue(TEXT("Failed room can reset after physical exit"), Room->ResetWhenEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2DevilTowerOpeningTest, "Metin2.Dungeons.DevilTowerOpening",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2DevilTowerOpeningTest::RunTest(const FString&)
{
	MT2DungeonFixture::FRig Rig;
	auto* First = Cast<AMT2DevilTowerRoom>(Rig.Room(TEXT("DevilTower.Floor1"), FVector::ZeroVector, AMT2DevilTowerRoom::StaticClass()));
	auto* Second = Cast<AMT2DevilTowerRoom>(Rig.Room(TEXT("DevilTower.Floor2"), FVector(5000,0,0), AMT2DevilTowerRoom::StaticClass()));
	auto* Third = Cast<AMT2DevilTowerRoom>(Rig.Room(TEXT("DevilTower.Floor3"), FVector(10000,0,0), AMT2DevilTowerRoom::StaticClass()));
	First->NextRoom = Second; Second->NextRoom = Third;
	First->TransitionSeconds = .1f; Second->TransitionSeconds = .1f;
	auto* A = Rig.Player(TEXT("A")); auto* B = Rig.Player(TEXT("B"));
	TestTrue(TEXT("Tower entry admits player"), First->JoinPlayer(A)); First->JoinPlayer(B);
	Rig.TimerTick(.01f);
	TestTrue(TEXT("Tower automatically starts on admission"), First->GetPhase() == EMT2DungeonRoomPhase::Active);
	TestEqual(TEXT("Opening legacy stages have no failure deadline"), First->GetSecondsRemaining(), 0.0);
	auto* Stone = Rig.Mob(); First->RegisterEnemy(Stone); Stone->GetHealthComponent()->SetHealth(0.f);
	A->GetPawn()->Destroy(); // The killer's logout must not cancel a shared transition.
	Rig.TimerTick(.1f); Rig.TimerTick(.11f); Rig.TimerTick(.01f);
	TestEqual(TEXT("Room-owned transition survives one participant disconnect"), Rig.World->GetSubsystem<UMT2DungeonSubsystem>()->GetPlayerRoom(B), static_cast<AMT2DungeonRoom*>(Second));
	TestTrue(TEXT("Next floor starts one shared encounter"), Second->GetPhase() == EMT2DungeonRoomPhase::Active);
	auto* Soldier = Rig.Mob(FVector(5000,0,0)); auto* Boss = Rig.Mob(FVector(5100,0,0));
	Second->RegisterEnemy(Soldier); Second->RegisterEnemy(Boss);
	Soldier->GetHealthComponent()->SetHealth(0.f);
	TestTrue(TEXT("Partial elimination cannot advance"), Second->GetPhase() == EMT2DungeonRoomPhase::Active);
	Boss->GetHealthComponent()->SetHealth(0.f);
	Rig.TimerTick(.1f); Rig.TimerTick(.11f); Rig.TimerTick(.01f);
	TestEqual(TEXT("Complete elimination advances to third floor"), Rig.World->GetSubsystem<UMT2DungeonSubsystem>()->GetPlayerRoom(B), static_cast<AMT2DungeonRoom*>(Third));
	TestTrue(TEXT("Third floor starts"), Third->GetPhase() == EMT2DungeonRoomPhase::Active);
	Third->CompleteEncounter(); Rig.TimerTick(5.f);
	TestEqual(TEXT("Unconfigured later stages cannot silently teleport players"), Rig.World->GetSubsystem<UMT2DungeonSubsystem>()->GetPlayerRoom(B), static_cast<AMT2DungeonRoom*>(Third));
	return true;
}
#endif
