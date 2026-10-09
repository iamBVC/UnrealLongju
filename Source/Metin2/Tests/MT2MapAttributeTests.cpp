#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "World/MT2MapAttributes.h"
#include "World/MT2MapPresentationActor.h"
#include "Engine/World.h"
#include "Characters/MT2PlayerCharacter.h"
#include "Player/MT2PlayerState.h"
#include "Characters/MT2CharacterMovementComponent.h"
#include "Mobs/MT2Mob.h"
#include "Mobs/MT2MobMovementComponent.h"
#include "Components/CapsuleComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2SafeZoneTest, "Metin2.World.SafeZones", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2SafeZoneTest::RunTest(const FString& Parameters)
{
	FMT2MapAttributes Data;
	Data.Size = FIntPoint(2, 2);
	Data.Flags = {4, 0, 0, 6};
	const FVector2D Minimum(0, -100), Maximum(100, 0);
	uint8 Flags = 0;
	TestTrue(TEXT("Source origin maps to flipped upper-right edge"), Data.Query(FVector(100, 0, 0), Minimum, Maximum, Flags) && Flags == 4);
	TestTrue(TEXT("East-west flip"), Data.Query(FVector(25, -25, 0), Minimum, Maximum, Flags) && Flags == 0);
	TestTrue(TEXT("Southward source rows"), Data.Query(FVector(25, -75, 0), Minimum, Maximum, Flags) && Flags == 6);
	TestFalse(TEXT("Outside source half-open extent"), Data.Query(FVector(0, -25, 0), Minimum, Maximum, Flags));
	TestFalse(TEXT("Outside Y extent"), Data.Query(FVector(25, -100, 0), Minimum, Maximum, Flags));
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AMT2MapPresentationActor* Map = World->SpawnActor<AMT2MapPresentationActor>();
	Map->WorldMin = Minimum; Map->WorldMax = Maximum; Map->Attributes = Data;
	UMT2MapAttributeSubsystem* Subsystem = World->GetSubsystem<UMT2MapAttributeSubsystem>();
	Subsystem->RegisterMap(Map);
	AMT2PlayerCharacter* Attacker = World->SpawnActor<AMT2PlayerCharacter>();
	AMT2PlayerCharacter* Defender = World->SpawnActor<AMT2PlayerCharacter>();
	AMT2PlayerState* StateA = World->SpawnActor<AMT2PlayerState>();
	AMT2PlayerState* StateB = World->SpawnActor<AMT2PlayerState>();
	Attacker->SetPlayerState(StateA); Defender->SetPlayerState(StateB);
	StateA->SetAggressiveMode(true);
	Attacker->SetActorLocation(FVector(25, -25, 1000)); Defender->SetActorLocation(FVector(30, -25, 1000));
	TestTrue(TEXT("Existing aggressive PvP outside safezone"), Attacker->IsPvPEnabledAgainst(Defender));
	Defender->SetActorLocation(FVector(75, -25, 1000));
	TestFalse(TEXT("Safe defender cannot be attacked"), Attacker->IsPvPEnabledAgainst(Defender));
	Defender->SetActorLocation(FVector(25, -25, 1000)); Attacker->SetActorLocation(FVector(75, -25, 1000));
	TestFalse(TEXT("Safe attacker cannot attack outside"), Attacker->IsPvPEnabledAgainst(Defender));
	Attacker->SetActorLocation(FVector(25, -25, 1000));
	TestTrue(TEXT("Leaving immediately restores PvP policy"), Attacker->IsPvPEnabledAgainst(Defender));
	Subsystem->UnregisterMap(Map);
	TestFalse(TEXT("Removed map no longer protects cells"), Subsystem->IsSafeZone(FVector(75, -25, 0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2NoWalkTest, "Metin2.World.NoWalk", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2NoWalkTest::RunTest(const FString& Parameters)
{
	FMT2MapAttributes Data;
	Data.Size = FIntPoint(5, 3);
	Data.Flags.Init(0, 15);
	Data.Flags[2] = MT2MapAttribute::Block;
	Data.Flags[7] = MT2MapAttribute::Object;
	Data.Flags[12] = MT2MapAttribute::Water | MT2MapAttribute::BanPK;
	const FVector2D Minimum(0, -300), Maximum(500, 0);
	double Time;
	FVector Normal;
	TestTrue(TEXT("Long step detects intervening BLOCK despite clear endpoint"),
		Data.TraceBlockedSegment(FVector(450, -50, 0), FVector(50, -50, 0), Minimum, Maximum, Time, Normal));
	TestEqual(TEXT("Mirrored X entry fraction"), Time, 0.375);
	TestEqual(TEXT("Wall normal opposes movement"), Normal, FVector(1, 0, 0));
	TestTrue(TEXT("OBJECT blocks in reverse direction"),
		Data.TraceBlockedSegment(FVector(50, -150, 0), FVector(450, -150, 0), Minimum, Maximum, Time, Normal));
	TestEqual(TEXT("Reverse wall normal"), Normal, FVector(-1, 0, 0));
	TestFalse(TEXT("Water and safezone alone remain walkable"),
		Data.TraceBlockedSegment(FVector(450, -250, 0), FVector(50, -250, 0), Minimum, Maximum, Time, Normal));
	TestFalse(TEXT("Vertical motion is unaffected"),
		Data.TraceBlockedSegment(FVector(250, -50, 0), FVector(250, -50, 100), Minimum, Maximum, Time, Normal));
	TestFalse(TEXT("Short motion within a clear cell stays clear"),
		Data.TraceBlockedSegment(FVector(450, -50, 0), FVector(440, -55, 0), Minimum, Maximum, Time, Normal));
	TestFalse(TEXT("Short motion inside an initially blocked cell can escape"),
		Data.TraceBlockedSegment(FVector(250, -50, 0), FVector(240, -55, 0), Minimum, Maximum, Time, Normal));
	TestFalse(TEXT("Invalid authored blocked spawn can escape"),
		Data.TraceBlockedSegment(FVector(250, -50, 0), FVector(450, -50, 0), Minimum, Maximum, Time, Normal));
	TestTrue(TEXT("Entering a map from outside still checks its blocked cells"),
		Data.TraceBlockedSegment(FVector(600, -50, 0), FVector(50, -50, 0), Minimum, Maximum, Time, Normal));
	TestFalse(TEXT("Outside segment is not blocked"),
		Data.TraceBlockedSegment(FVector(600, -50, 0), FVector(700, -50, 0), Minimum, Maximum, Time, Normal));
	TestFalse(TEXT("Exclusive map boundary is not a spurious wall"),
		Data.TraceBlockedSegment(FVector(0, -50, 0), FVector(-100, -50, 0), Minimum, Maximum, Time, Normal));
	FMT2MapAttributes Corner;
	Corner.Size = FIntPoint(2, 2); Corner.Flags = {0, MT2MapAttribute::Block, 0, 0};
	TestTrue(TEXT("Diagonal corner cutting cannot skip a blocked neighboring cell"),
		Corner.TraceBlockedSegment(FVector(75, -25, 0), FVector(25, -75, 0), FVector2D(0, -100), FVector2D(100, 0), Time, Normal));
	FMT2MapAttributes Invalid = Data; Invalid.Flags.Pop();
	TestFalse(TEXT("Incomplete grids do not manufacture collision"),
		Invalid.TraceBlockedSegment(FVector(450, -50, 0), FVector(50, -50, 0), Minimum, Maximum, Time, Normal));

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AMT2MapPresentationActor* Map = World->SpawnActor<AMT2MapPresentationActor>();
	Map->WorldMin = Minimum; Map->WorldMax = Maximum; Map->Attributes = Data;
	UMT2MapAttributeSubsystem* Subsystem = World->GetSubsystem<UMT2MapAttributeSubsystem>();
	Subsystem->RegisterMap(Map);
	TestTrue(TEXT("BLOCK query"), Subsystem->IsBlocked(FVector(250, -50, 0)));
	TestTrue(TEXT("OBJECT query"), Subsystem->IsBlocked(FVector(250, -150, 0)));
	TestFalse(TEXT("BANPK does not block walking"), Subsystem->IsBlocked(FVector(250, -250, 0)));
	TestTrue(TEXT("BANPK still protects PvP"), Subsystem->IsSafeZone(FVector(250, -250, 0)));
	AMT2PlayerCharacter* Player = World->SpawnActor<AMT2PlayerCharacter>();
	AMT2Mob* Mob = World->SpawnActor<AMT2Mob>();
	TestNotNull(TEXT("Player uses attribute-aware movement"), Cast<UMT2CharacterMovementComponent>(Player->GetCharacterMovement()));
	TestNotNull(TEXT("Mob retains its specialized movement"), Cast<UMT2MobMovementComponent>(Mob->GetCharacterMovement()));
	TestNotNull(TEXT("Mob movement inherits attribute constraints"), Cast<UMT2CharacterMovementComponent>(Mob->GetCharacterMovement()));
	Player->SetActorLocation(FVector(450, -50, 1000));
	FHitResult Hit;
	Player->GetCharacterMovement()->MoveUpdatedComponent(FVector(-400, 0, 0), Player->GetActorQuat(), true, &Hit);
	TestTrue(TEXT("Authoritative movement stops before blocked strip"), Player->GetActorLocation().X > 300 && Player->GetActorLocation().X < 301);
	TestTrue(TEXT("Virtual wall is a valid blocking hit"), Hit.IsValidBlockingHit());
	TestFalse(TEXT("Character stopped in clear cell"), Subsystem->IsBlocked(Player->GetActorLocation()));
	Player->GetCharacterMovement()->MoveUpdatedComponent(FVector(0, -20, 0), Player->GetActorQuat(), true, &Hit);
	TestEqual(TEXT("Movement along boundary remains possible"), Player->GetActorLocation().Y, -70.0);
	Player->GetCharacterMovement()->MoveUpdatedComponent(FVector(0, 0, -20), Player->GetActorQuat(), true, &Hit);
	TestEqual(TEXT("Height correction remains possible"), Player->GetActorLocation().Z, 980.0);
	Mob->SetActorLocation(FVector(450, -150, 2000));
	Mob->GetCharacterMovement()->MoveUpdatedComponent(FVector(-400, 0, 0), Mob->GetActorQuat(), true, &Hit);
	TestTrue(TEXT("Authoritative mob movement obeys OBJECT"), Mob->GetActorLocation().X > 300 && Mob->GetActorLocation().X < 301);
	Subsystem->UnregisterMap(Map);
	Player->GetCharacterMovement()->MoveUpdatedComponent(FVector(-200, 0, 0), Player->GetActorQuat(), true, &Hit);
	TestTrue(TEXT("Unregistered attributes no longer constrain movement"), Player->GetActorLocation().X < 300);
	return true;
}
#endif
