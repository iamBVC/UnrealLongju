#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "Characters/MT2PlayerCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/MT2PlayerState.h"
#include "Config/MT2GameplaySettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2ServerPresentationTest, "Metin2.Server.PresentationPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2ServerPresentationTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	auto* Player = World->SpawnActor<AMT2PlayerCharacter>();
	Player->SetAnimationSet(TEXT("onehand_sword"));
	Player->GetMesh()->SetComponentTickEnabled(true);
	Player->DisableDedicatedServerVisuals();
	TestFalse(TEXT("No skeletal tick on headless policy"), Player->GetMesh()->IsComponentTickEnabled());
	TestTrue(TEXT("No bone evaluation"), Player->GetMesh()->bNoSkeletonUpdate);
	TestNull(TEXT("No cosmetic mesh asset"), Player->GetMesh()->GetSkeletalMeshAsset());
	TestNull(TEXT("No live animation instance"), Player->GetMesh()->GetAnimInstance());
	TestEqual(TEXT("No skeletal physics body"), Player->GetMesh()->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	TestTrue(TEXT("Movement component still active"), Player->GetCharacterMovement()->IsComponentTickEnabled());
	TestTrue(TEXT("Capsule still queryable"), Player->GetCapsuleComponent()->IsQueryCollisionEnabled());
	TestEqual(TEXT("Gameplay animation set remains"), Player->GetAnimationSet(), FName(TEXT("onehand_sword")));
	TestTrue(TEXT("Movement replication retained"), Player->IsReplicatingMovement());
	auto* State = World->SpawnActor<AMT2PlayerState>();
	TestEqual(TEXT("Configured PlayerState baseline"), State->GetNetUpdateFrequency(),
		FMath::Clamp(UMT2GameplaySettings::Get().PlayerStateReplicationRate, 1.f, 30.f));
	return true;
}
#endif
