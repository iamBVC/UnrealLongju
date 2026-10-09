#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Components/BoxComponent.h"
#include "Mobs/MT2Mob.h"
#include "Mobs/MT2MobMovementComponent.h"
#include "World/MT2MapPresentationActor.h"
#include "World/MT2MapAttributes.h"
#include "UObject/StrongObjectPtr.h"
#include "Components/MT2ActorRelevanceComponent.h"
#include "World/MT2WorldSimulationSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2MobMoveSegmentsTest, "Metin2.World.MobMoveSegments",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2MobMoveSegmentsTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine)); GI->InitializeStandalone();
	UWorld* World = GI->GetWorld(); if (!TestNotNull(TEXT("World"), World)) { return false; }
	ON_SCOPE_EXIT { GI->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Floor = World->SpawnActor<AActor>(); auto* Box = NewObject<UBoxComponent>(Floor);
	Floor->SetRootComponent(Box); Box->SetBoxExtent(FVector(5000,5000,50));
	Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent(); Floor->SetActorLocation(FVector(0,0,-50));
	auto* Mob = World->SpawnActor<AMT2Mob>(); Mob->SetActorLocation(FVector(0,0,110));
	auto* Move = CastChecked<UMT2MobMovementComponent>(Mob->GetCharacterMovement()); Move->MaxWalkSpeed = 200.f;
	Move->SetMovementMode(MOVE_Walking);
	Move->StartMoveSegment(FVector(1000,0,110), 100.f);
	TestFalse(TEXT("Normal movement has no actor movement snapshots"), Mob->IsReplicatingMovement());
	TestTrue(TEXT("Legacy server movement cadence"), FMath::IsNearlyEqual(Move->GetComponentTickInterval(), .16f));
	TestTrue(TEXT("Segment published"), Move->MoveSegment.bMoving);
	const uint32 Serial = Move->MoveSegment.Serial;
	Move->StartMoveSegment(FVector(1000,0,110), 100.f);
	TestEqual(TEXT("Repeated chase request does not resend segment"), Move->MoveSegment.Serial, Serial);
	Move->MoveSegment.ServerStartTime = Move->GetMovementServerTime() - Move->MoveSegment.Duration * .5;
	Move->TickComponent(.16f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("Server samples timed segment midpoint"), FMath::IsNearlyEqual(Mob->GetActorLocation().X, 450., 1.));
	TestEqual(TEXT("Stepping does not publish movement packets"), Move->MoveSegment.Serial, Serial);

	auto* Proxy = World->SpawnActor<AMT2Mob>(); Proxy->SetActorLocation(FVector(0,100,110));
	Proxy->SetRole(ROLE_SimulatedProxy);
	if (!TestFalse(TEXT("Test proxy is not authoritative"), Proxy->HasAuthority())) { return false; }
	auto* ProxyMove = CastChecked<UMT2MobMovementComponent>(Proxy->GetCharacterMovement());
	ProxyMove->MoveSegment = Move->MoveSegment; ProxyMove->OnRep_MoveSegment();
	ProxyMove->TickComponent(.016f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("Late relevant proxy catches up from server start time"), FMath::IsNearlyEqual(Proxy->GetActorLocation().X, 450., 1.));
	const FVector BeforeClientCommand = ProxyMove->MoveSegment.Destination;
	ProxyMove->StartMoveSegment(FVector(4000,0,110), 0.f);
	TestEqual(TEXT("Client cannot author authoritative segments"), FVector(ProxyMove->MoveSegment.Destination), BeforeClientCommand);

	Move->StopMovementImmediately();
	TestFalse(TEXT("Stop ends the segment"), Move->MoveSegment.bMoving);
	ProxyMove->MoveSegment = Move->MoveSegment; ProxyMove->OnRep_MoveSegment();
	TestTrue(TEXT("Stop synchronizes final position"), Proxy->GetActorLocation().Equals(Mob->GetActorLocation(), .1));
	TestFalse(TEXT("Stopped proxy does not tick movement"), ProxyMove->IsComponentTickEnabled());
	Proxy->Destroy();

	auto* Map = World->SpawnActor<AMT2MapPresentationActor>();
	Map->WorldMin = FVector2D(0,-1000); Map->WorldMax = FVector2D(1000,1000);
	Map->Attributes.Size = FIntPoint(10,20); Map->Attributes.Flags.Init(0,200);
	Map->Attributes.Flags[10*10+5] = MT2MapAttribute::Block;
	World->GetSubsystem<UMT2MapAttributeSubsystem>()->RegisterMap(Map);
	Mob->SetActorLocation(FVector(300,0,110)); Move->StartMoveSegment(FVector(800,0,110), 0.f);
	Move->MoveSegment.ServerStartTime = Move->GetMovementServerTime() - Move->MoveSegment.Duration;
	Move->TickComponent(.16f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("Segment collision retains no-walk checks"), Mob->GetActorLocation().X < 400.1);
	TestFalse(TEXT("Blocked segment publishes stop"), Move->MoveSegment.bMoving);
	World->GetSubsystem<UMT2MapAttributeSubsystem>()->UnregisterMap(Map);
	auto* Wall = World->SpawnActor<AActor>(); auto* WallBox = NewObject<UBoxComponent>(Wall);
	Wall->SetRootComponent(WallBox); WallBox->SetBoxExtent(FVector(10,200,200));
	WallBox->SetCollisionProfileName(TEXT("BlockAll")); WallBox->RegisterComponent(); Wall->SetActorLocation(FVector(440,0,100));
	Mob->SetActorLocation(FVector(300,0,110)); Move->StartMoveSegment(FVector(800,0,110), 0.f);
	Move->MoveSegment.ServerStartTime = Move->GetMovementServerTime() - Move->MoveSegment.Duration;
	Move->TickComponent(.16f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("Segment sweeps retain wall collision"), Mob->GetActorLocation().X < 430.);
	TestFalse(TEXT("Wall obstruction stops segment"), Move->MoveSegment.bMoving);

	auto* Simulation = World->GetSubsystem<UMT2WorldSimulationSubsystem>(); Simulation->RegisterMob(Mob);
	auto* Relevance = Mob->FindComponentByClass<UMT2ActorRelevanceComponent>();
	Mob->SetNetDormancy(DORM_Awake); Relevance->SetSpatiallyActive(false);
	TestFalse(TEXT("Unoccupied region removes mob from frequent scheduler"), Simulation->ActiveMobs.Contains(Mob));
	TestTrue(TEXT("Inactive mob is network dormant"), Mob->NetDormancy == DORM_DormantAll);
	Relevance->SetSpatiallyActive(true);
	TestTrue(TEXT("Occupied region reactivates mob"), Simulation->ActiveMobs.Contains(Mob));
	TestTrue(TEXT("Reactivation wakes replication"), Mob->NetDormancy == DORM_Awake);

	Move->BeginExternalKnockback();
	TestTrue(TEXT("Knockback temporarily enables swept snapshot path"), Mob->IsReplicatingMovement());
	TestTrue(TEXT("Impulse uses full-rate movement"), Move->GetComponentTickInterval() == 0.f);
	Move->FinishExternalKnockback();
	TestFalse(TEXT("Impulse end restores segment networking"), Mob->IsReplicatingMovement());
	return true;
}
#endif
