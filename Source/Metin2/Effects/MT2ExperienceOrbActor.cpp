/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Effects/MT2ExperienceOrbActor.h"

#include "Components/MaterialBillboardComponent.h"
#include "Components/SceneComponent.h"
#include "Config/MT2GameplaySettings.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogMT2ExperienceOrb, Log, All);

AMT2ExperienceOrbActor::AMT2ExperienceOrbActor()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
	SetActorEnableCollision(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;
	Particle = CreateDefaultSubobject<UParticleSystemComponent>(TEXT("Particle"));
	Particle->SetupAttachment(SceneRoot);
	Particle->bAutoActivate = false;
	OrbSprite = CreateDefaultSubobject<UMaterialBillboardComponent>(TEXT("OrbSprite"));
	OrbSprite->SetupAttachment(SceneRoot);
	OrbSprite->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	OrbSprite->SetHiddenInGame(false);
	OrbSprite->SetVisibility(true);
}

void AMT2ExperienceOrbActor::SpawnOrbs(
	UWorld* World, const FVector& SourceLocation, AActor* Recipient,
	int64 ExperienceAmount)
{
	if (!World || !Recipient || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	const UMT2GameplaySettings& Settings = UMT2GameplaySettings::Get();
	UParticleSystem* Effect = Settings.ExperienceOrbEffect.LoadSynchronous();
	UMaterialInterface* SpriteMaterial = Settings.ExperienceOrbMaterial.LoadSynchronous();
	if (!Effect && !SpriteMaterial)
	{
		UE_LOG(LogMT2ExperienceOrb, Warning,
			TEXT("Experience orb visuals could not be loaded: effect=%s material=%s"),
			*Settings.ExperienceOrbEffect.ToSoftObjectPath().ToString(),
			*Settings.ExperienceOrbMaterial.ToSoftObjectPath().ToString());
		return;
	}

	// One orb per decimal magnitude: 10-99 EXP = 1, 100-999 = 2, 1,000-9,999 = 3.
	// Values below 10 still get one orb. Integer thresholds avoid floating-point boundary errors.
	int32 Count = 1;
	for (int64 Threshold = 100;
		ExperienceAmount >= Threshold && Threshold <= MAX_int64 / 10;
		Threshold *= 10)
	{
		++Count;
	}
	Count = FMath::Clamp(Count, 1, FMath::Clamp(Settings.ExperienceOrbMaximumCount, 1, 12));
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMT2ExperienceOrbActor* Orb = World->SpawnActor<AMT2ExperienceOrbActor>(
			SourceLocation, FRotator::ZeroRotator, Parameters);
		if (!Orb)
		{
			UE_LOG(LogMT2ExperienceOrb, Warning,
				TEXT("Failed to spawn experience orb actor."));
			continue;
		}
		if (Effect)
		{
			Orb->Particle->SetTemplate(Effect);
			Orb->Particle->SetRelativeScale3D(FVector(Settings.ExperienceOrbScale));
		}
		if (SpriteMaterial)
		{
			const float SpriteSize = FMath::Max(Settings.ExperienceOrbSpriteSize, 1.0f);
			Orb->OrbSprite->AddElement(
				SpriteMaterial, nullptr, false, SpriteSize, SpriteSize, nullptr);
		}
		Orb->InitializeOrb(Recipient, SourceLocation);
	}
}

void AMT2ExperienceOrbActor::InitializeOrb(
	AActor* Recipient, const FVector& SourceLocation)
{
	TargetActor = Recipient;
	const UMT2GameplaySettings& Settings = UMT2GameplaySettings::Get();
	FVector TargetOrigin;
	FVector TargetExtent;
	Recipient->GetActorBounds(true, TargetOrigin, TargetExtent);
	const FVector TargetLocation =
		TargetOrigin + FVector(0.0f, 0.0f, TargetExtent.Z * 0.55f);
	const FVector TowardTarget = (TargetLocation - SourceLocation).GetSafeNormal();
	const FVector AwayFromTarget = TowardTarget.IsNearlyZero()
		? FVector::UpVector : -TowardTarget;
	const float ConeRadians = FMath::DegreesToRadians(
		FMath::Clamp(Settings.ExperienceOrbSpreadConeAngle, 0.0f, 180.0f));
	const FVector LaunchDirection = FMath::VRandCone(AwayFromTarget, ConeRadians);
	Velocity = LaunchDirection * FMath::Max(Settings.ExperienceOrbInitialSpeed, 0.0f);
	Acceleration = TowardTarget * FMath::Max(Settings.ExperienceOrbAcceleration, 0.0f);
	MaximumSpeed = FMath::Max(Settings.ExperienceOrbMaximumSpeed, 1.0f);
	ElapsedSeconds = 0.0f;
	HomingStartTime = FMath::Max(Settings.ExperienceOrbHomingStartTime, 0.0f);
	HomingTurnRate = FMath::Max(Settings.ExperienceOrbHomingTurnRate, 0.0f);
	RemainingRange = FMath::Max(Settings.ExperienceOrbMaximumRange, 1.0f);
	HitRadius = FMath::Max(Settings.ExperienceOrbHitRadius, 1.0f);
	MinimumVisibleTime = FMath::Max(Settings.ExperienceOrbMinimumVisibleTime, 0.0f);
	VisualOffsetPeriod = FMath::Max(Settings.ExperienceOrbVisualOffsetPeriod, 0.01f);
	VisualOffsetAmplitude = FMath::Max(Settings.ExperienceOrbVisualOffsetAmplitude, 0.0f);
	VisualAngularVelocity = Settings.ExperienceOrbVisualAngularVelocity;
	SetActorLocation(SourceLocation);
	if (Particle->Template)
	{
		Particle->Activate(true);
	}
}

void AMT2ExperienceOrbActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AActor* Target = TargetActor.Get();
	if (!Target)
	{
		Destroy();
		return;
	}

	FVector TargetOrigin;
	FVector TargetExtent;
	Target->GetActorBounds(true, TargetOrigin, TargetExtent);
	const FVector TargetLocation =
		TargetOrigin + FVector(0.0f, 0.0f, TargetExtent.Z * 0.55f);
	const FVector OldLocation = GetActorLocation();
	ElapsedSeconds += DeltaSeconds;

	// Preserve the turning arc, but guarantee a homing speed above the recipient's run speed.
	if (ElapsedSeconds >= HomingStartTime && !Velocity.IsNearlyZero())
	{
		const FVector CurrentDirection = Velocity.GetSafeNormal();
		const FVector DesiredDirection = (TargetLocation - OldLocation).GetSafeNormal();
		if (!DesiredDirection.IsNearlyZero())
		{
			const FQuat FullTurn = FQuat::FindBetweenNormals(CurrentDirection, DesiredDirection);
			const float FullAngle = FullTurn.GetAngle();
			const float MaximumAngle = FMath::DegreesToRadians(HomingTurnRate * DeltaSeconds);
			const float TurnAlpha = FullAngle > UE_SMALL_NUMBER
				? FMath::Min(MaximumAngle / FullAngle, 1.0f) : 1.0f;
			const FQuat AppliedTurn = FQuat::Slerp(FQuat::Identity, FullTurn, TurnAlpha);
			Velocity = AppliedTurn.RotateVector(Velocity);
			Acceleration = AppliedTurn.RotateVector(Acceleration);
		}
	}

	float CatchupSpeed = 0.f;
	if (ElapsedSeconds >= HomingStartTime)
	{
		float TargetSpeed = Target->GetVelocity().Size();
		if (const auto* Character = Cast<ACharacter>(Target))
		{
			TargetSpeed = FMath::Max(TargetSpeed, Character->GetCharacterMovement()->GetMaxSpeed());
		}
		CatchupSpeed = TargetSpeed * FMath::Max(UMT2GameplaySettings::Get().ExperienceOrbCatchupSpeedMultiplier, 1.05f);
	}
	Velocity = (Velocity + Acceleration * DeltaSeconds).GetClampedToMaxSize(FMath::Max(MaximumSpeed, CatchupSpeed));
	if (Velocity.SizeSquared() < FMath::Square(CatchupSpeed))
	{
		const FVector Direction = Velocity.IsNearlyZero() ? (TargetLocation - OldLocation).GetSafeNormal() : Velocity.GetSafeNormal();
		Velocity = Direction * CatchupSpeed;
	}
	const FVector NewLocation = OldLocation + Velocity * DeltaSeconds;
	RemainingRange -= FVector::Distance(OldLocation, NewLocation);
	if (RemainingRange <= 0.0f)
	{
		Destroy();
		return;
	}

	// Swept point test prevents a fast orb from stepping through the player between two frames.
	if (ElapsedSeconds >= MinimumVisibleTime &&
		FMath::PointDistToSegment(TargetLocation, OldLocation, NewLocation) <= HitRadius)
	{
		const UMT2GameplaySettings& Settings = UMT2GameplaySettings::Get();
		if (UParticleSystem* Impact = Settings.ExperienceOrbImpactEffect.LoadSynchronous())
		{
			UGameplayStatics::SpawnEmitterAtLocation(
				GetWorld(), Impact, TargetLocation, FRotator::ZeroRotator,
				FVector(Settings.ExperienceOrbScale), true, EPSCPoolMethod::AutoRelease, true);
		}
		Destroy();
		return;
	}
	SetActorLocation(NewLocation);

	// FLY_ATTACH_TYPE_EXP: a decaying exponential offset, spun around the flight direction by
	// AngularVelocity. The imported particle supplies the original yellow sprite trail.
	const FVector FlightDirection = Velocity.GetSafeNormal();
	FVector OffsetAxis = FVector::CrossProduct(FlightDirection, FVector::UpVector).GetSafeNormal();
	if (OffsetAxis.IsNearlyZero())
	{
		OffsetAxis = FVector::RightVector;
	}
	OffsetAxis = FQuat(FlightDirection,
		FMath::DegreesToRadians(VisualAngularVelocity * ElapsedSeconds)).RotateVector(OffsetAxis);
	const float OffsetTime = ElapsedSeconds / VisualOffsetPeriod;
	const float OffsetMagnitude =
		VisualOffsetAmplitude * FMath::Exp(-OffsetTime) * OffsetTime;
	Particle->SetWorldLocation(NewLocation + OffsetAxis * OffsetMagnitude);
}
