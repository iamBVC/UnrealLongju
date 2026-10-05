/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Mounts/MT2MountComponent.h"

#include "Characters/MT2PlayerCharacter.h"
#include "Components/MT2HealthComponent.h"
#include "Components/MT2MovementSpeedComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Equipment/MT2EquipmentComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Mounts/MT2MountDefinition.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

UMT2MountComponent::UMT2MountComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UMT2MountComponent::BeginPlay()
{
	Super::BeginPlay();
	if (const AMT2PlayerCharacter* Character = GetPlayerCharacter(); Character && Character->GetMesh())
	{
		StandingMeshTransform = Character->GetMesh()->GetRelativeTransform();
		if (const UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			StandingNetworkSmoothingMode = static_cast<uint8>(Movement->NetworkSmoothingMode);
		}
	}
	ApplyMountedState();
}

void UMT2MountComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(MountExpiryTimer);
	}
	if (MountMeshComponent)
	{
		MountMeshComponent->DestroyComponent();
		MountMeshComponent = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void UMT2MountComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMT2MountComponent, bMounted);
	DOREPLIFETIME(UMT2MountComponent, MountDefinition);
	DOREPLIFETIME(UMT2MountComponent, MountedKind);
}

bool UMT2MountComponent::CanAttackWhileMounted() const
{
	const UMT2MountDefinition* Definition = MountDefinition.Get();
	return !bMounted || (Definition && Definition->bCanAttack);
}

bool UMT2MountComponent::CanUseHorseSkills() const
{
	const UMT2MountDefinition* Definition = MountDefinition.Get();
	return bMounted && Definition && Definition->bCanUseHorseSkills;
}

int32 UMT2MountComponent::GetCalledSpecialMountVnum() const
{
	const UMT2MountDefinition* Definition = CalledSpecialMountDefinition.Get();
	return Definition ? Definition->Vnum : 0;
}

int32 UMT2MountComponent::GetCalledSpecialMountRemainingSeconds() const
{
	if (CalledSpecialMountDurationSeconds <= 0)
	{
		return 0;
	}
	const UWorld* World = GetWorld();
	if (bMounted && MountedKind == EMT2MountKind::SpecialMount && World)
	{
		return FMath::Max(FMath::CeilToInt(
			World->GetTimerManager().GetTimerRemaining(MountExpiryTimer)), 0);
	}
	return CalledSpecialMountDurationSeconds;
}

bool UMT2MountComponent::Mount(UMT2MountDefinition* Definition, int32 DurationSeconds)
{
	AMT2PlayerCharacter* Character = GetPlayerCharacter();
	if (!Character || !Character->HasAuthority() || !Definition || Definition->Mesh.IsNull() ||
		(Character->GetHealthComponent() && Character->GetHealthComponent()->IsDead()))
	{
		return false;
	}
	MountDefinition = Definition;
	MountedKind = Definition->MountKind;
	bMounted = true;
	ApplyMountedState();
	OnMountedStateChanged.Broadcast(true);
	GetWorld()->GetTimerManager().ClearTimer(MountExpiryTimer);
	if (DurationSeconds > 0)
	{
		GetWorld()->GetTimerManager().SetTimer(
			MountExpiryTimer, this, &UMT2MountComponent::ForceDismount,
			static_cast<float>(DurationSeconds), false);
	}
	return true;
}

void UMT2MountComponent::ServerDismount_Implementation()
{
	ForceDismount();
}

void UMT2MountComponent::ServerToggleSpecialMount_Implementation()
{
	if (bMounted && MountedKind == EMT2MountKind::SpecialMount)
	{
		ForceDismount();
		return;
	}
	if (UMT2MountDefinition* Definition = CalledSpecialMountDefinition.LoadSynchronous())
	{
		Mount(Definition, CalledSpecialMountDurationSeconds);
	}
}

void UMT2MountComponent::ServerToggleHorse_Implementation()
{
	if (bMounted && MountedKind == EMT2MountKind::Horse)
	{
		ForceDismount();
		return;
	}
	if (UMT2MountDefinition* Definition = CalledHorseDefinition.LoadSynchronous())
	{
		Mount(Definition);
	}
}

bool UMT2MountComponent::CallSpecialMount(UMT2MountDefinition* Definition, int32 DurationSeconds)
{
	if (!Definition || Definition->MountKind != EMT2MountKind::SpecialMount)
	{
		return false;
	}
	CalledSpecialMountDefinition = Definition;
	CalledSpecialMountDurationSeconds = FMath::Max(DurationSeconds, 0);
	return Mount(Definition, CalledSpecialMountDurationSeconds);
}

void UMT2MountComponent::SetCalledHorse(UMT2MountDefinition* Definition)
{
	if (GetOwner() && GetOwner()->HasAuthority() &&
		(!Definition || Definition->MountKind == EMT2MountKind::Horse))
	{
		CalledHorseDefinition = Definition;
	}
}

void UMT2MountComponent::ForceDismount()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !bMounted)
	{
		return;
	}
	bMounted = false;
	MountDefinition.Reset();
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(MountExpiryTimer);
	}
	ApplyMountedState();
	OnMountedStateChanged.Broadcast(false);
}

void UMT2MountComponent::OnRep_MountedState()
{
	ApplyMountedState();
	OnMountedStateChanged.Broadcast(bMounted);
}

void UMT2MountComponent::EnsureVisualComponent()
{
	AMT2PlayerCharacter* Character = GetPlayerCharacter();
	if (MountMeshComponent || !Character || Character->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	MountMeshComponent = NewObject<USkeletalMeshComponent>(Character, TEXT("MountMeshComponent"));
	MountMeshComponent->SetupAttachment(Character->GetRootComponent());
	MountMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MountMeshComponent->SetGenerateOverlapEvents(false);
	MountMeshComponent->SetCanEverAffectNavigation(false);
	MountMeshComponent->RegisterComponent();
}

void UMT2MountComponent::ApplyMountedState()
{
	AMT2PlayerCharacter* Character = GetPlayerCharacter();
	if (!Character)
	{
		return;
	}
	UMT2MountDefinition* Definition = bMounted ? MountDefinition.LoadSynchronous() : nullptr;
	if (bMounted && !Definition)
	{
		return;
	}
	EnsureVisualComponent();
	USkeletalMeshComponent* RiderMesh = Character->GetMesh();
	if (UCharacterMovementComponent* CharacterMovement = Character->GetCharacterMovement();
		CharacterMovement && Character->GetLocalRole() == ROLE_SimulatedProxy)
	{
		// CharacterMovement's simulated-proxy smoothing writes directly to the inherited mesh every
		// frame. Once that mesh is saddle-attached it overwrites the rider-relative rotation, which
		// is why only remote riders appeared rotated. Restore the project's normal mode on dismount.
		CharacterMovement->NetworkSmoothingMode = bMounted
			? ENetworkSmoothingMode::Disabled
			: static_cast<ENetworkSmoothingMode>(StandingNetworkSmoothingMode);
	}
	if (!bMounted && RiderMesh)
	{
		// Detach before hiding/clearing the mount. The rider is a mount child while riding, so
		// propagating the mount's hidden state first also made the player mesh stay invisible.
		RiderMesh->AttachToComponent(
			Character->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
		RiderMesh->SetRelativeTransform(StandingMeshTransform);
	}
	if (MountMeshComponent)
	{
		MountMeshComponent->SetVisibility(bMounted, false);
		MountMeshComponent->SetComponentTickEnabled(bMounted);
		MountMeshComponent->SetSkeletalMesh(bMounted ? Definition->Mesh.LoadSynchronous() : nullptr);
		FTransform MountTransform = bMounted
			? Definition->MountRelativeTransform : FTransform::Identity;
		if (bMounted && MountMeshComponent->GetSkeletalMeshAsset() && Character->GetCapsuleComponent())
		{
			// The old client gives horse and rider the exact same ground-level world position. UE's
			// character origin is at the capsule center, so derive the equivalent position from the
			// imported mesh bounds instead of carrying the mob Blueprint's unrelated capsule offset.
			const FTransform VisualTransform(
				MountTransform.GetRotation(), FVector::ZeroVector, MountTransform.GetScale3D());
			const FBox VisualBounds = MountMeshComponent->GetSkeletalMeshAsset()
				->GetBounds().GetBox().TransformBy(VisualTransform);
			if (VisualBounds.IsValid)
			{
				FVector Location = MountTransform.GetLocation();
				Location.Z = -Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
					- VisualBounds.Min.Z;
				MountTransform.SetLocation(Location);
			}
		}
		MountMeshComponent->SetRelativeTransform(MountTransform);
		MountMeshComponent->SetAnimInstanceClass(
			bMounted ? Definition->AnimationClass.LoadSynchronous() : nullptr);
	}
	if (RiderMesh)
	{
		static const FName SaddleBone(TEXT("saddle"));
		if (bMounted && MountMeshComponent && MountMeshComponent->DoesSocketExist(SaddleBone))
		{
			// Original client: __AttachHorseSaddle attaches PART_MAIN directly to the horse's
			// "saddle" bone. The bone animation supplies the correct rider height and motion.
			RiderMesh->AttachToComponent(
				MountMeshComponent,
				FAttachmentTransformRules::SnapToTargetNotIncludingScale,
				SaddleBone);
			FTransform RiderTransform = Definition->RiderRelativeTransform;
			RiderTransform.SetScale3D(
				Definition->RiderRelativeTransform.GetScale3D() * StandingMeshTransform.GetScale3D());
			RiderMesh->SetRelativeTransform(RiderTransform);
		}
		else if (bMounted)
		{
			RiderMesh->AttachToComponent(
				Character->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
			RiderMesh->SetRelativeTransform(StandingMeshTransform);
		}
	}
	if (UMT2MovementSpeedComponent* Movement = Character->GetMovementSpeedComponent())
	{
		// Definitions created before mount speed import was added serialized the old 1.0 default.
		// Keep those assets useful without forcing a full item reimport; new assets use mob_proto.
		const float MountedSpeedMultiplier = Definition &&
			!FMath::IsNearlyEqual(Definition->MovementSpeedMultiplier, 1.0f)
			? Definition->MovementSpeedMultiplier : 1.5f;
		Movement->SetExternalSpeedMultiplier(
			bMounted ? FMath::Max(MountedSpeedMultiplier, 0.01f) : 1.0f);
	}
	const UMT2EquipmentComponent* Equipment = Character->GetEquipmentComponent();
	Character->SetWeaponAnimationSet(
		Equipment ? Equipment->GetEquippedWeaponSubType() : INDEX_NONE, bMounted);
	if (Character->HasAuthority())
	{
		Character->ForceNetUpdate();
	}
}

AMT2PlayerCharacter* UMT2MountComponent::GetPlayerCharacter() const
{
	return Cast<AMT2PlayerCharacter>(GetOwner());
}
