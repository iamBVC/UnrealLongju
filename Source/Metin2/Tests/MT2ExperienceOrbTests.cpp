#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "Effects/MT2ExperienceOrbActor.h"
#include "Characters/MT2PlayerCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2ExperienceOrbCatchupTest, "Metin2.Effects.ExperienceOrbCatchup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2ExperienceOrbCatchupTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	auto* Player = World->SpawnActor<AMT2PlayerCharacter>();
	Player->SetActorLocation(FVector(1000, 0, 0));
	Player->GetCharacterMovement()->MaxWalkSpeed = 1800.f;
	Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	auto* Orb = World->SpawnActor<AMT2ExperienceOrbActor>();
	Orb->InitializeOrb(Player, FVector::ZeroVector);
	Orb->RemainingRange = 100000.f;
	Orb->MinimumVisibleTime = 100.f; // Check speed without spawning impact particles.
	for (int32 Step = 0; Step < 30; ++Step)
	{
		Player->SetActorLocation(Player->GetActorLocation() + FVector(180, 0, 0));
		Orb->Tick(.1f);
		if (Orb->ElapsedSeconds >= Orb->HomingStartTime)
			TestTrue(TEXT("Orb homing speed exceeds buffed run speed"), Orb->Velocity.Size() > 1800.f);
	}
	Player->GetCharacterMovement()->MaxWalkSpeed = 3600.f;
	Orb->Tick(.1f);
	TestTrue(TEXT("Catchup adapts when recipient speeds up"), Orb->Velocity.Size() > 3600.f);
	TestFalse(TEXT("Orbs remain client-local"), Orb->GetIsReplicated());
	return true;
}
#endif
