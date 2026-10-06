#pragma once

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "World/MT2MapUtils.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/ScopeExit.h"
#include "Player/MT2PlayerController.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "Characters/MT2PlayerCharacter.h"
#include "Components/BoxComponent.h"
#include "Quests/MT2QuestNode.h"
#include "Editor.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/StrongObjectPtr.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/App.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "EngineUtils.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2PIETravelGuardTest, "Metin2.World.PIETravelGuard", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2PIETravelGuardTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) { return false; }
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
	World->NextURL = TEXT("pending");
	TestFalse(TEXT("Packaged travel unaffected"), MT2MapUtils::HasPendingPIETravel(*World));
	World->NextURL.Reset();
	World->WorldType = EWorldType::PIE;
	TestFalse(TEXT("Idle PIE world accepts travel"), MT2MapUtils::HasPendingPIETravel(*World));
	World->NextURL = TEXT("pending");
	TestTrue(TEXT("Scheduled travel blocks a second request"), MT2MapUtils::HasPendingPIETravel(*World));
	World->NextURL.Reset();
	MT2MapUtils::PreparePIEClientWorldForTravel(*World, true);
	TestFalse(TEXT("Idle/non-client fixture has no partition side effects"), MT2MapUtils::HasPendingPIETravel(*World));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2PIEVisibilityQueueTest, "Metin2.World.PIEVisibilityQueue", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2PIEVisibilityQueueTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) { return false; }
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
	AMT2PlayerController* Controller = World->SpawnActor<AMT2PlayerController>();
	if (!TestNotNull(TEXT("Controller"), Controller)) { return false; }
	World->WorldType = EWorldType::PIE;
	World->NextURL = TEXT("pending");
	FUpdateLevelVisibilityLevelInfo Update;
	Update.PackageName = World->GetOutermost()->GetFName();
	Update.FileName = Update.PackageName;
	Update.bIsVisible = true;
	UFunction* Single = Controller->FindFunctionChecked(TEXT("ServerUpdateLevelVisibility"));
	FStructOnScope SingleParameters(Single);
	const FStructProperty* SingleProperty = FindFProperty<FStructProperty>(Single, TEXT("LevelVisibility"));
	if (!TestNotNull(TEXT("Single RPC schema"), SingleProperty)) { return false; }
	*SingleProperty->ContainerPtrToValuePtr<FUpdateLevelVisibilityLevelInfo>(SingleParameters.GetStructMemory()) = Update;
	Controller->ProcessEvent(Single, SingleParameters.GetStructMemory());
	TestEqual(TEXT("Early single visibility report deferred"), Controller->PendingPIELevelVisibility.Num(), 1);
	UFunction* Batch = Controller->FindFunctionChecked(TEXT("ServerUpdateMultipleLevelsVisibility"));
	FStructOnScope BatchParameters(Batch);
	const FArrayProperty* BatchProperty = FindFProperty<FArrayProperty>(Batch, TEXT("LevelVisibilities"));
	if (!TestNotNull(TEXT("Batch RPC schema"), BatchProperty)) { return false; }
	*BatchProperty->ContainerPtrToValuePtr<TArray<FUpdateLevelVisibilityLevelInfo>>(BatchParameters.GetStructMemory()) = {Update, Update};
	Controller->ProcessEvent(Batch, BatchParameters.GetStructMemory());
	TestEqual(TEXT("Batched reports retain order and contents"), Controller->PendingPIELevelVisibility.Num(), 3);
	World->NextURL.Reset();
	Controller->PostSeamlessTravel();
	TestTrue(TEXT("Deferred reports replay and release storage"), Controller->PendingPIELevelVisibility.IsEmpty());
	Controller->ProcessEvent(Single, SingleParameters.GetStructMemory());
	TestTrue(TEXT("Idle RPC dispatch does not defer"), Controller->PendingPIELevelVisibility.IsEmpty());
	TestTrue(TEXT("Native batch retains RPC validation"), Batch->HasAllFunctionFlags(FUNC_NetServer | FUNC_NetValidate));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2PIEEntryWarpTest, "Metin2.World.PIEEntryWarp", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2PIEEntryWarpTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) { return false; }
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
	AMT2PlayerCharacter* Player = World->SpawnActor<AMT2PlayerCharacter>();
	if (!TestNotNull(TEXT("Player"), Player)) { return false; }
	AActor* Ground = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Ground"), Ground)) { return false; }
	UBoxComponent* Collision = NewObject<UBoxComponent>(Ground);
	Ground->SetRootComponent(Collision);
	Collision->SetBoxExtent(FVector(10000, 10000, 10));
	Collision->SetCollisionObjectType(ECC_WorldStatic);
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Collision->SetCollisionResponseToAllChannels(ECR_Block);
	Collision->RegisterComponent();
	World->WorldType = EWorldType::PIE;
	FMT2QuestContext Context;
	Context.Player = Player;
	Context.bPreservePIESpawn = true;
	UMT2QuestNode_Warp* Warp = NewObject<UMT2QuestNode_Warp>();
	Warp->Position = FVector2D(12, 34);
	const FVector InitialLocation = Player->GetActorLocation();
	TestTrue(TEXT("Entry warp continues without relocation"), Warp->Execute(Context) == EMT2QuestNodeResult::Continue);
	TestEqual(TEXT("Editor-selected location retained"), Player->GetActorLocation(), InitialLocation);
	Context.bPreservePIESpawn = false;
	Warp->Execute(Context);
	TestEqual(TEXT("Later explicit warp still works"), FVector2D(Player->GetActorLocation()), FVector2D(1200, -3400));
	Context.bPreservePIESpawn = true;
	World->WorldType = EWorldType::Game;
	Warp->Position = FVector2D(56, 78);
	Warp->Execute(Context);
	TestEqual(TEXT("Non-PIE warp unaffected"), FVector2D(Player->GetActorLocation()), FVector2D(5600, -7800));
	return true;
}
namespace
{
	class FMT2WaitForPIESpawn : public IAutomationLatentCommand
	{
	public:
		FMT2WaitForPIESpawn(FAutomationTestBase& InTest, ULevelEditorPlaySettings* InSettings,
			const FString& InPackage, bool bInCamera, APlayerStart* InFixtureStart)
			: Test(InTest), Settings(InSettings), Package(InPackage), bCamera(bInCamera),
			  Start(FPlatformTime::Seconds()), FixtureStart(InFixtureStart) {}
		virtual bool Update() override
		{
			if (bEnding)
			{
				for (const FWorldContext& Context : GEngine->GetWorldContexts())
				{
					if (Context.WorldType == EWorldType::PIE) { return false; }
				}
				if (APlayerStart* Actor = FixtureStart.Get())
				{
					Actor->GetWorld()->DestroyActor(Actor);
					FixtureStart.Reset();
				}
				return true;
			}
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (Context.WorldType != EWorldType::PIE || !World || !World->HasBegunPlay()) { continue; }
				const FString ActualPackage = UWorld::StripPIEPrefixFromPackageName(
					World->GetOutermost()->GetName(), World->StreamingLevelsPrefix);
				if (ActualPackage != Package || MT2MapUtils::HasPendingPIETravel(*World))
				{
					Test.AddError(TEXT("PIE startup changed the selected map or scheduled travel."));
					GEditor->RequestEndPlayMap(); bEnding = true; return false;
				}
				APlayerController* Controller = World->GetFirstPlayerController();
				AMT2PlayerCharacter* Player = Controller ? Cast<AMT2PlayerCharacter>(Controller->GetPawn()) : nullptr;
				if (!Player) { continue; }
				// Isolate spawn placement from gravity and player input during entry-event timers.
				Player->GetCharacterMovement()->DisableMovement();
				const AActor* StartSpot = Controller->StartSpot.Get();
				if (!bCamera && !StartSpot)
				{
					Test.AddError(TEXT("Player Start launch did not select a start actor."));
					GEditor->RequestEndPlayMap(); bEnding = true; return false;
				}
				const FVector Expected = bCamera ? FVector(1234, 2345, 100000) : StartSpot->GetActorLocation();
				if (!FVector2D(Player->GetActorLocation()).Equals(FVector2D(Expected), 5.0))
				{
					Test.AddError(FString::Printf(TEXT("PIE spawn XY %s did not match selected start %s."),
						*Player->GetActorLocation().ToCompactString(), *Expected.ToCompactString()));
					GEditor->RequestEndPlayMap(); bEnding = true; return false;
				}
				if (StableSince == 0) { StableSince = FPlatformTime::Seconds(); }
				if (FPlatformTime::Seconds() - StableSince >= 3)
				{
					Test.AddInfo(TEXT("PIE retained the selected map/start through Login, Enter and recovery intervals."));
					GEditor->RequestEndPlayMap(); bEnding = true;
				}
			}
			if (!bEnding && FPlatformTime::Seconds() - Start > 120)
			{
				Test.AddError(TEXT("PIE did not spawn within 120 seconds."));
				GEditor->RequestEndPlayMap(); bEnding = true;
			}
			return false;
		}
	private:
		FAutomationTestBase& Test;
		TStrongObjectPtr<ULevelEditorPlaySettings> Settings;
		FString Package;
		bool bCamera;
		double Start;
		double StableSince = 0;
		bool bEnding = false;
		TWeakObjectPtr<APlayerStart> FixtureStart;
	};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FMT2PIESelectedSpawnTest, "Metin2.Editor.PIESpawn", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
void FMT2PIESelectedSpawnTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	Names.Add(TEXT("PlayerStart")); Commands.Add(TEXT("PlayerStart"));
	Names.Add(TEXT("CurrentCamera")); Commands.Add(TEXT("CurrentCamera"));
}
bool FMT2PIESelectedSpawnTest::RunTest(const FString& Parameters)
{
	if (!GEditor || GEditor->PlayWorld) { AddError(TEXT("Run from an idle editor with saved work.")); return false; }
	if (!FApp::CanEverRender()) { AddWarning(TEXT("PIE spawn integration needs a rendering editor.")); return true; }
	FString Source;
	if (!GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("EditorStartupMap"), Source, GEngineIni))
	{
		AddError(TEXT("Configure EditorStartupMap before running PIE spawn integration.")); return false;
	}
	Source = FSoftObjectPath(Source).GetLongPackageName();
	FAutomationEditorCommonUtils::LoadMap(Source);
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World || World->GetOutermost()->GetName() != Source)
	{
		AddError(TEXT("Could not load the configured editor map.")); return false;
	}
	const bool bCamera = Parameters == TEXT("CurrentCamera");
	APlayerStart* FixtureStart = nullptr;
	if (!bCamera && !TActorIterator<APlayerStart>(World))
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.ObjectFlags |= RF_Transient;
		FixtureStart = World->SpawnActor<APlayerStart>(FVector(1234, 2345, 100000), FRotator::ZeroRotator, SpawnParams);
		if (!TestNotNull(TEXT("Temporary Player Start"), FixtureStart)) { return false; }
		AddInfo(TEXT("No existing Player Start: using a transient fixture, removed after PIE without saving."));
	}
	ULevelEditorPlaySettings* Settings = NewObject<ULevelEditorPlaySettings>(GetTransientPackage(), NAME_None,
		RF_Transient, GetMutableDefault<ULevelEditorPlaySettings>());
	Settings->SetPlayNetMode(EPlayNetMode::PIE_Standalone);
	Settings->SetPlayNumberOfClients(1);
	Settings->SetRunUnderOneProcess(true);
	Settings->bLaunchSeparateServer = false;
	FRequestPlaySessionParams Params;
	Params.EditorPlaySettings = Settings;
	Params.GlobalMapOverride = Source;
	Params.bAllowOnlineSubsystem = false;
	if (bCamera) { Params.StartLocation = FVector(1234, 2345, 100000); Params.StartRotation = FRotator::ZeroRotator; }
	GEditor->RequestPlaySession(Params);
	ADD_LATENT_AUTOMATION_COMMAND(FMT2WaitForPIESpawn(*this, Settings, Source, bCamera, FixtureStart));
	return true;
}
#endif
