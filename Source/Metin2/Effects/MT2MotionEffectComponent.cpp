/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Effects/MT2MotionEffectComponent.h"

#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "TimerManager.h"

namespace
{
	// The source client's basis is -Y forward; UE's is +X. The motion parser already converts effect
	// *positions* through it (EffectPosition (x,y,z) becomes (-y,x,z), a +90 degree yaw), but the
	// orientation the effect is spawned with never went through the same conversion - which is why
	// bone-attached skill effects (Spirit Strike, Sword Strike, Bash) came out turned 90 degrees.
	const FQuat ClientToUnrealBasis = FRotator(0.0, 90.0, 0.0).Quaternion();
}

UMT2MotionEffectComponent::UMT2MotionEffectComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UMT2MotionEffectComponent::PlayMotionEffects(UAnimSequence* Animation, float PlayRate)
{
	StopPendingMotionEffects();
	if (!Animation || !GetWorld() || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	const UMT2AnimationMotionData* MotionData =
		Animation->GetAssetUserData<UMT2AnimationMotionData>();
	if (!MotionData)
	{
		return;
	}

	const float SafePlayRate = FMath::Max(FMath::Abs(PlayRate), 0.01f);
	for (const FMT2MotionEffectEvent& Event : MotionData->EffectEvents)
	{
		if (Event.Effect.IsNull())
		{
			continue;
		}
		const float Delay = FMath::Max(0.0f, Event.TimeSeconds / SafePlayRate);
		if (Delay <= UE_SMALL_NUMBER)
		{
			SpawnMotionEffect(Event);
			continue;
		}

		FTimerHandle& Handle = PendingEffectTimers.AddDefaulted_GetRef();
		GetWorld()->GetTimerManager().SetTimer(
			Handle,
			FTimerDelegate::CreateUObject(this, &UMT2MotionEffectComponent::SpawnMotionEffect, Event),
			Delay, false);
	}
}

void UMT2MotionEffectComponent::StopPendingMotionEffects()
{
	if (UWorld* World = GetWorld())
	{
		for (FTimerHandle& Handle : PendingEffectTimers)
		{
			World->GetTimerManager().ClearTimer(Handle);
		}
	}
	PendingEffectTimers.Reset();

	// Motion effects are transient. Stop emission when their owning animation ends or changes;
	// particles already alive keep their authored lifetime/alpha curve and fade naturally.
	for (UParticleSystemComponent* Effect : ActiveMotionEffects)
	{
		if (IsValid(Effect))
		{
			Effect->DeactivateSystem();
		}
	}
	ActiveMotionEffects.Reset();
}

void UMT2MotionEffectComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopPendingMotionEffects();
	Super::EndPlay(EndPlayReason);
}

USkeletalMeshComponent* UMT2MotionEffectComponent::ResolveOwnerMesh() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	return Character ? Character->GetMesh() : GetOwner()
		? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
}

void UMT2MotionEffectComponent::SpawnMotionEffect(FMT2MotionEffectEvent Event)
{
	UParticleSystem* Effect = Event.Effect.LoadSynchronous();
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	USkeletalMeshComponent* Mesh = ResolveOwnerMesh();
	if (!Effect || !Owner || !World)
	{
		return;
	}

	if (Event.bIndependent)
	{
		const FVector Location = Owner->GetActorTransform().TransformPosition(Event.RelativeLocation);
		const FQuat Rotation = Owner->GetActorQuat() * ClientToUnrealBasis;
		TrackMotionEffect(UGameplayStatics::SpawnEmitterAtLocation(
			World, Effect, Location, Rotation.Rotator(), FVector::OneVector,
			true, EPSCPoolMethod::AutoRelease, true));
		return;
	}

	if (Event.bAttach && Mesh && !Event.BoneName.IsNone())
	{
		const FQuat RelativeRotation = ClientToUnrealBasis;
		if (Event.bFollow)
		{
			TrackMotionEffect(UGameplayStatics::SpawnEmitterAttached(
				Effect, Mesh, Event.BoneName, Event.RelativeLocation, RelativeRotation.Rotator(),
				FVector::OneVector, EAttachLocation::KeepRelativeOffset,
				true, EPSCPoolMethod::AutoRelease, true));
		}
		else
		{
			const FTransform BoneTransform = Mesh->GetSocketTransform(Event.BoneName, RTS_World);
			const FQuat WorldRotation = BoneTransform.GetRotation() * RelativeRotation;
			TrackMotionEffect(UGameplayStatics::SpawnEmitterAtLocation(
				World, Effect, BoneTransform.TransformPosition(Event.RelativeLocation),
				WorldRotation.Rotator(), FVector::OneVector,
				true, EPSCPoolMethod::AutoRelease, true));
		}
		return;
	}

	USceneComponent* AttachComponent = Mesh ? Cast<USceneComponent>(Mesh) : Owner->GetRootComponent();
	if (AttachComponent)
	{
		TrackMotionEffect(UGameplayStatics::SpawnEmitterAttached(
			Effect, AttachComponent, NAME_None, Event.RelativeLocation, ClientToUnrealBasis.Rotator(),
			FVector::OneVector, EAttachLocation::KeepRelativeOffset,
			true, EPSCPoolMethod::AutoRelease, true));
	}
}

void UMT2MotionEffectComponent::TrackMotionEffect(UParticleSystemComponent* Effect)
{
	if (Effect)
	{
		ActiveMotionEffects.Add(Effect);
	}
}
