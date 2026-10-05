#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "World/MT2MapAttributes.h"
#include "World/MT2MapPresentationActor.h"
#include "Engine/World.h"
#include "Characters/MT2PlayerCharacter.h"
#include "Player/MT2PlayerState.h"

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
#endif
