/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Combat/MT2TargetIndicatorComponent.h"

#include "Combat/MT2CombatComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Config/MT2GameplaySettings.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

UMT2TargetIndicatorComponent::UMT2TargetIndicatorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Purely cosmetic: only the machine looking through this player's eyes needs it.
	SetIsReplicatedByDefault(false);
}

void UMT2TargetIndicatorComponent::BeginPlay()
{
	Super::BeginPlay();

	// The dedicated server never renders a ring. Everyone else binds; whether the ring actually
	// shows is decided per-target in ShowOn (only the locally controlled pawn does). Binding is NOT
	// gated on IsLocallyControlled here: on a client the pawn's controller arrives via replication
	// after BeginPlay, so gating here would drop the binding for good. TryBindCombat retries until
	// the combat component and controller are ready.
	if (GetNetMode() == NM_DedicatedServer)
	{
		SetComponentTickEnabled(false);
		return;
	}
	TryBindCombat();
}

void UMT2TargetIndicatorComponent::TryBindCombat()
{
	if (BoundCombat.IsValid())
	{
		return;
	}
	UMT2CombatComponent* Combat = ResolveCombatComponent();
	if (!Combat)
	{
		return;
	}
	BoundCombat = Combat;
	Combat->OnSelectedTargetChanged.AddUniqueDynamic(
		this, &UMT2TargetIndicatorComponent::HandleSelectedTargetChanged);
	// A target may already be set by the time this binds (e.g. a target that survived a respawn).
	ShowOn(Combat->GetSelectedTarget());
}

void UMT2TargetIndicatorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UMT2CombatComponent* Combat = BoundCombat.Get())
	{
		Combat->OnSelectedTargetChanged.RemoveDynamic(
			this, &UMT2TargetIndicatorComponent::HandleSelectedTargetChanged);
	}
	Super::EndPlay(EndPlayReason);
}

UMT2CombatComponent* UMT2TargetIndicatorComponent::ResolveCombatComponent() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UMT2CombatComponent>() : nullptr;
}

void UMT2TargetIndicatorComponent::HandleSelectedTargetChanged(AActor* OldTarget, AActor* NewTarget)
{
	ShowOn(NewTarget);
}

void UMT2TargetIndicatorComponent::EnsureRing()
{
	if (RingMesh)
	{
		return;
	}
	const UMT2GameplaySettings& Settings = UMT2GameplaySettings::Get();
	UStaticMesh* Mesh = Settings.TargetIndicatorMesh.LoadSynchronous();
	if (!Mesh)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[MT2TargetIndicator] TargetIndicatorMesh (%s) failed to load; no ring will show. "
				 "Set it in Project Settings -> Metin2."),
			*Settings.TargetIndicatorMesh.ToString());
		return;
	}

	RingMesh = NewObject<UStaticMeshComponent>(GetOwner());
	RingMesh->SetStaticMesh(Mesh);
	RingMesh->SetRelativeScale3D(Settings.TargetIndicatorScale);
	RingMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// The ring marks the target for the local player only; it must never occlude clicks or be lit
	// like world geometry.
	RingMesh->SetCastShadow(false);
	// Draw over the world so the flat disc at the feet isn't hidden by the ground it sits on.
	RingMesh->SetTranslucentSortPriority(1);
	RingMesh->bRenderCustomDepth = false;
	RingMesh->RegisterComponent();

	if (UMaterialInterface* Material = Settings.TargetIndicatorMaterial.LoadSynchronous())
	{
		UMaterialInstanceDynamic* Dynamic = RingMesh->CreateDynamicMaterialInstance(0, Material);
		if (Dynamic)
		{
			// Only lands if the material actually exposes a "Color" parameter; harmless otherwise.
			Dynamic->SetVectorParameterValue(TEXT("Color"), Settings.TargetIndicatorColor);
		}
	}
}

void UMT2TargetIndicatorComponent::ShowOn(AActor* Target)
{
	CurrentTarget = Target;
	// Only the machine looking through this pawn's eyes draws its ring. Checked here (not at bind
	// time) because the controller is reliably assigned by the time any target is selected.
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!Target || !OwnerPawn || !OwnerPawn->IsLocallyControlled())
	{
		Hide();
		return;
	}
	EnsureRing();
	if (!RingMesh)
	{
		return;
	}
	// Position it right away so it doesn't flash at the origin for a frame before the first tick.
	PositionRing(Target);
	RingMesh->SetVisibility(true, true);
}

void UMT2TargetIndicatorComponent::PositionRing(AActor* Target)
{
	if (!RingMesh || !Target)
	{
		return;
	}
	// Put the ring at the ground under the target plus the configured height, independent of the
	// actor's origin or capsule size - capsule bounds don't work for a big metin whose visual is
	// scaled well past its collision capsule, which left the ring buried. A ground trace is what
	// makes "always N cm above the ground, whatever the mob's Z" true. Driven in world space (not
	// attached) so target rotation never tilts the flat ring.
	const FVector TargetLocation = Target->GetActorLocation();
	UWorld* World = GetWorld();

	// Start well above the actor origin and trace down to the first static surface it stands on.
	// Object-type query against WorldStatic only, so it ignores the mob and the player themselves.
	float GroundZ = TargetLocation.Z;
	if (World)
	{
		const FVector TraceStart = TargetLocation + FVector(0.0f, 0.0f, 500.0f);
		const FVector TraceEnd = TargetLocation - FVector(0.0f, 0.0f, 5000.0f);
		FHitResult GroundHit;
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MT2TargetRingGround), false);
		if (World->LineTraceSingleByObjectType(
			GroundHit, TraceStart, TraceEnd,
			FCollisionObjectQueryParams(ECC_WorldStatic), QueryParams))
		{
			GroundZ = GroundHit.ImpactPoint.Z;
		}
	}

	const float Height = UMT2GameplaySettings::Get().TargetIndicatorHeight;
	RingMesh->SetWorldLocation(FVector(TargetLocation.X, TargetLocation.Y, GroundZ + Height));
	RingMesh->SetWorldRotation(FRotator(0.0f, CurrentSpin, 0.0f));
}

void UMT2TargetIndicatorComponent::Hide()
{
	if (RingMesh)
	{
		RingMesh->SetVisibility(false);
	}
}

void UMT2TargetIndicatorComponent::TickComponent(
	float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// Keep trying to bind until the owner's combat component is up (client controller replication).
	if (!BoundCombat.IsValid())
	{
		TryBindCombat();
	}
	if (!RingMesh || !RingMesh->IsVisible())
	{
		return;
	}
	// A target can die or be destroyed without a target-changed event (it stays "selected" until the
	// player retargets); drop the ring when that happens.
	if (!CurrentTarget.IsValid())
	{
		Hide();
		return;
	}
	CurrentSpin = FMath::Fmod(
		CurrentSpin + UMT2GameplaySettings::Get().TargetIndicatorSpinSpeed * DeltaTime, 360.0f);
	// Re-place every frame so it tracks a moving target and stays glued to the ground under it.
	PositionRing(CurrentTarget.Get());
}
