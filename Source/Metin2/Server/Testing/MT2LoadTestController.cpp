#include "Server/Testing/MT2LoadTestController.h"

#include "Characters/MT2PlayerCharacter.h"
#include "Combat/MT2CombatComponent.h"
#include "Components/MT2HealthComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "FramePro/FramePro.h"
#include "Mobs/MT2Mob.h"
#include "Player/MT2PlayerState.h"
#include "Server/Testing/MT2LoadTestSettings.h"
#include "Server/Testing/MT2LoadTestSubsystem.h"
#include "World/MT2PlayerSpatialGridSubsystem.h"

AMT2LoadTestController::AMT2LoadTestController()
{
	bWantsPlayerState = true;
	PrimaryActorTick.bCanEverTick = false;
}

void AMT2LoadTestController::InitPlayerState()
{
	if (!HasAuthority() || PlayerState) return;
	// Set the bot flag before BeginPlay, including admin lookup and account-related hooks.
	AMT2PlayerState* State = GetWorld()->SpawnActorDeferred<AMT2PlayerState>(
		AMT2PlayerState::StaticClass(), FTransform::Identity, this);
	if (!State) return;
	State->SetFlags(RF_Transient);
	State->SetIsABot(true);
	SetPlayerState(State);
	State->FinishSpawning(FTransform::Identity);
	// No persistence identity is configured: no account, database row or autosave timer.
}

void AMT2LoadTestController::InitializeBehavior(const FVector& Origin, float Radius)
{
	Home = Origin; RoamRadius = Radius;
	TrackedPawn = Cast<AMT2PlayerCharacter>(GetPawn());
	NextDecisionTime = GetWorld()->GetTimeSeconds() + FMath::FRandRange(0.f, .5f);
	if (auto* Grid = GetWorld()->GetSubsystem<UMT2PlayerSpatialGridSubsystem>())
	{
		Grid->RegisterSimulatedPlayer(TrackedPawn.Get());
	}
}

void AMT2LoadTestController::Think(double Now)
{
	if (Now < NextDecisionTime) return;
	FRAMEPRO_NAMED_SCOPE("MT2.LoadTest.BotDecision");
	const auto* Settings = GetDefault<UMT2LoadTestSettings>();
	NextDecisionTime = Now + FMath::Max(Settings->DecisionInterval, .1f);
	auto* BotPawn = Cast<AMT2PlayerCharacter>(GetPawn());
	if (!BotPawn) { RemoveFakePlayer(); return; }
	if (BotPawn->GetHealthComponent()->IsDead())
	{
		Target.Reset();
		if (DeadSince < 0.) DeadSince = Now;
		if (Now - DeadSince >= FMath::Max(Settings->RespawnDelay, 1.f))
		{
			BotPawn->RequestRespawn(false);
			DeadSince = -1.;
		}
		return;
	}
	DeadSince = -1.;
	auto IsCandidate = [this](const AMT2Mob* Mob)
	{
		return IsValid(Mob) && !Mob->IsActorBeingDestroyed() && Mob->GetMobType() == EMT2MobType::Monster &&
			!Mob->GetHealthComponent()->IsDead() && FVector::DistSquared2D(Home, Mob->GetActorLocation()) <= FMath::Square(RoamRadius);
	};
	if (!IsCandidate(Target.Get())) Target.Reset();
	if (!Target.IsValid())
	{
		TArray<FOverlapResult> Hits;
		FCollisionObjectQueryParams Objects; Objects.AddObjectTypesToQuery(ECC_GameTraceChannel1);
		GetWorld()->OverlapMultiByObjectType(Hits, BotPawn->GetActorLocation(), FQuat::Identity, Objects,
			FCollisionShape::MakeSphere(FMath::Clamp(Settings->TargetSearchRadius, 100.f, 5000.f)),
			FCollisionQueryParams(SCENE_QUERY_STAT(MT2FakePlayerTarget), false, BotPawn));
		float Best = TNumericLimits<float>::Max();
		for (const auto& Hit : Hits)
		{
			auto* Mob = Cast<AMT2Mob>(Hit.GetActor());
			if (!IsCandidate(Mob)) continue;
			const float Distance = FVector::DistSquared2D(BotPawn->GetActorLocation(), Mob->GetActorLocation());
			if (Distance < Best) { Best = Distance; Target = Mob; }
		}
	}
	if (Target.IsValid())
	{
		if (BotPawn->GetCombatComponent()->GetSelectedTarget() != Target.Get())
		{
			BotPawn->SetServerAutoAttackTarget(Target.Get());
		}
		return;
	}
	if (BotPawn->GetCombatComponent()->GetSelectedTarget()) BotPawn->SetServerAutoAttackTarget(nullptr);
	if (Now >= NextWanderTime)
	{
		const float Angle = FMath::FRand() * 2.f * UE_PI;
		const float Distance = FMath::Sqrt(FMath::FRand()) * RoamRadius;
		BotPawn->SetServerAutoMoveGoal(Home + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Distance);
		NextWanderTime = Now + FMath::FRandRange(3.f, 6.f);
	}
}

void AMT2LoadTestController::RemoveFakePlayer()
{
	if (!HasAuthority()) return;
	if (auto* BotPawn = TrackedPawn.Get())
	{
		BotPawn->SetServerAutoAttackTarget(nullptr);
		UnPossess(); BotPawn->Destroy();
	}
	Destroy(); // AController destroys its PlayerState as part of normal cleanup.
}

void AMT2LoadTestController::EndPlay(const EEndPlayReason::Type Reason)
{
	if (GetWorld())
	{
		if (auto* Grid = GetWorld()->GetSubsystem<UMT2PlayerSpatialGridSubsystem>()) Grid->UnregisterSimulatedPlayer(TrackedPawn.Get());
		if (auto* LoadTest = GetWorld()->GetSubsystem<UMT2LoadTestSubsystem>()) LoadTest->ForgetController(this);
	}
	Super::EndPlay(Reason);
}
