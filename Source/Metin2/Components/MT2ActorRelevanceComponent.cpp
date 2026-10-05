/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Components/MT2ActorRelevanceComponent.h"

#include "AIController.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "World/MT2PlayerSpatialGridSubsystem.h"

UMT2ActorRelevanceComponent::UMT2ActorRelevanceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

void UMT2ActorRelevanceComponent::BeginPlay()
{
	Super::BeginPlay();
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	if (UMT2PlayerSpatialGridSubsystem* Grid = GetWorld()->GetSubsystem<UMT2PlayerSpatialGridSubsystem>())
	{
		Grid->RegisterRelevanceComponent(this);
	}
}

void UMT2ActorRelevanceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UMT2PlayerSpatialGridSubsystem* Grid = World->GetSubsystem<UMT2PlayerSpatialGridSubsystem>())
		{
			Grid->UnregisterRelevanceComponent(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void UMT2ActorRelevanceComponent::RefreshSpatialRelevance(const UMT2PlayerSpatialGridSubsystem& PlayerGrid)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}
	SetSpatiallyActive(PlayerGrid.EstimatePlayersInRadius(Owner->GetActorLocation(), ActivationRadius) > 0);
}

void UMT2ActorRelevanceComponent::SetSpatiallyActive(bool bNewActive)
{
	if (bSpatiallyActive == bNewActive)
	{
		return;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	if (!bNewActive)
	{
		bSavedActorTickEnabled = Owner->IsActorTickEnabled();
		SavedComponentTickStates.Reset();
		TInlineComponentArray<UActorComponent*> Components(Owner);
		for (UActorComponent* Component : Components)
		{
			if (Component && Component != this && Component->PrimaryComponentTick.bCanEverTick)
			{
				SavedComponentTickStates.Add(Component, Component->IsComponentTickEnabled());
				Component->SetComponentTickEnabled(false);
			}
		}

		if (ACharacter* Character = Cast<ACharacter>(Owner))
		{
			Character->GetCharacterMovement()->StopMovementImmediately();
			if (AAIController* Controller = Cast<AAIController>(Character->GetController()))
			{
				Controller->StopMovement();
				Controller->SetActorTickEnabled(false);
			}
		}
		Owner->SetActorTickEnabled(false);
	}
	else
	{
		Owner->SetActorTickEnabled(bSavedActorTickEnabled);
		for (const TPair<TWeakObjectPtr<UActorComponent>, bool>& Pair : SavedComponentTickStates)
		{
			if (UActorComponent* Component = Pair.Key.Get())
			{
				Component->SetComponentTickEnabled(Pair.Value);
			}
		}
		SavedComponentTickStates.Reset();

		if (const ACharacter* Character = Cast<ACharacter>(Owner))
		{
			if (AAIController* Controller = Cast<AAIController>(Character->GetController()))
			{
				Controller->SetActorTickEnabled(true);
			}
		}
	}

	bSpatiallyActive = bNewActive;
}
