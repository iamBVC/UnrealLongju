#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Mobs/MT2MetinStone.h"
#include "Mobs/MT2MobMovementComponent.h"
#include "Mobs/MT2MobAIComponent.h"
#include "Core/MT2VnumRegistrySubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Particles/ParticleSystem.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2MetinStoneTest, "Metin2.World.MetinAnchoringAndSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2MetinStoneTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine)); GI->InitializeStandalone();
	UWorld* World = GI->GetWorld(); if (!World) return false;
	ON_SCOPE_EXIT { GI->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Stone = World->SpawnActor<AMT2MetinStone>();
	Stone->SetActorLocationAndRotation(FVector(100,200,300), FRotator(0,75,0));
	const FTransform Anchor = Stone->GetActorTransform();
	auto* Movement = CastChecked<UMT2MobMovementComponent>(Stone->GetCharacterMovement());
	TestEqual(TEXT("Stone starts with disabled movement"), int32(Movement->MovementMode), int32(MOVE_None));
	Movement->StartMoveSegment(FVector(1000,0,0), 0.f);
	Movement->BeginExternalKnockback(FVector::ForwardVector, 200.f, 1.f);
	Movement->Velocity = FVector(100,100,100); Movement->TickComponent(.16f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("Stone rejects segment and shove"), Movement->GetMoveSegment().bMoving);
	TestTrue(TEXT("Stone transform stays anchored"), Stone->GetActorTransform().Equals(Anchor));
	TestFalse(TEXT("Stone never orients to velocity"), Movement->bOrientRotationToMovement);
	TestFalse(TEXT("Stone never rotates toward controller"), Movement->bUseControllerDesiredRotation);
	TestEqual(TEXT("Intact smoke stage"), AMT2MetinStone::GetSmokeStage(100.f), 0);
	TestEqual(TEXT("First damaged smoke stage"), AMT2MetinStone::GetSmokeStage(85.f), 1);
	TestEqual(TEXT("Second damaged smoke stage"), AMT2MetinStone::GetSmokeStage(45.f), 2);
	TestEqual(TEXT("Critical smoke stage"), AMT2MetinStone::GetSmokeStage(10.f), 3);
	for (int32 Vnum : {8001, 8002, 8003, 8051})
	{
		const auto Class = GI->GetSubsystem<UMT2VnumRegistrySubsystem>()->ResolveMobClass(Vnum);
		const auto* Defaults = Class ? Cast<AMT2MetinStone>(Class->GetDefaultObject()) : nullptr;
		if (!TestNotNull(FString::Printf(TEXT("Imported Metin %d"), Vnum), Defaults)) continue;
		const bool bClassic = Vnum != 8051;
		TestEqual(TEXT("Classic four-stage smoke / persistent coloured ambient smoke"),
			bClassic ? Defaults->SmokeStages.Num() : Defaults->AmbientSmoke.Num(), bClassic ? 4 : 1);
		const auto& Attachments = bClassic ? Defaults->SmokeStages : Defaults->AmbientSmoke;
		for (const auto& Attachment : Attachments)
		{
			TestNotNull(TEXT("Assigned imported particle loads"), Attachment.Effect.LoadSynchronous());
			TestTrue(TEXT("Legacy attachment bone exists"), Defaults->GetMesh()->DoesSocketExist(Attachment.Bone));
		}
	}
	return true;
}
#endif
