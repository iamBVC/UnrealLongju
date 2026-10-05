/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2NameplateComponent.h"

#include "Characters/MT2PlayerCharacter.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Items/MT2WorldItem.h"
#include "Mobs/MT2Mob.h"
#include "Player/MT2PlayerState.h"
#include "Voice/MT2VoiceChatClientSubsystem.h"
#include "UI/MT2GMMarkComponent.h"
#include "UI/MT2NameplateWidget.h"
#include "Guild/MT2GuildComponent.h"
#include "Guild/MT2GuildMarkCacheSubsystem.h"
#include "World/MT2WorldSimulationSubsystem.h"

UMT2NameplateComponent::UMT2NameplateComponent()
{
	NameplateWidgetClass = TSoftClassPtr<UMT2NameplateWidget>(
		FSoftObjectPath(TEXT("/Game/UI/MT2Nameplate.MT2Nameplate_C")));
	SetWidgetSpace(EWidgetSpace::Screen);
	SetDrawSize(FVector2D(420.0f, 72.0f));
	SetPivot(FVector2D(0.5f, 1.0f));
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	SetWindowFocusable(false);
	SetCullDistance(MaxRenderDistance);
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UMT2NameplateComponent::SetMaxRenderDistance(float NewDistance)
{
	MaxRenderDistance = FMath::Max(NewDistance, 0.0f);
	SetCullDistance(MaxRenderDistance);
}

void UMT2NameplateComponent::BeginPlay()
{
	Super::BeginPlay();
	SetCullDistance(MaxRenderDistance);
	if (!GetOwner() || GetOwner()->GetNetMode() == NM_DedicatedServer)
	{
		SetHiddenInGame(true);
		return;
	}
	SetHiddenInGame(true);
	SetComponentTickEnabled(false);
	if (UMT2WorldSimulationSubsystem* Simulation = GetWorld()->GetSubsystem<UMT2WorldSimulationSubsystem>())
	{
		Simulation->RegisterNameplate(this);
	}
	RefreshFromSimulationManager();
}

bool UMT2NameplateComponent::EnsureWidgetInitialized()
{
	if (GetUserWidgetObject()) return true;
	const TSubclassOf<UMT2NameplateWidget> NameplateClass = NameplateWidgetClass.LoadSynchronous();
	if (!NameplateClass)
	{
		UE_LOG(LogTemp, Error, TEXT("Missing required Widget Blueprint /Game/UI/MT2Nameplate"));
		return false;
	}
	SetWidgetClass(NameplateClass);
	InitWidget();
	// Character plates stay click-through. Ground-item plates are deliberately hit-testable so one
	// item can be picked without collecting every nearby drop.
	if (UMT2NameplateWidget* NameplateWidgetObject =
		Cast<UMT2NameplateWidget>(GetUserWidgetObject()))
	{
		const bool bWorldItem = GetOwner() && GetOwner()->IsA<AMT2WorldItem>();
		NameplateWidgetObject->SetVisibility(bWorldItem
			? ESlateVisibility::Visible : ESlateVisibility::HitTestInvisible);
		if (bWorldItem)
		{
			NameplateWidgetObject->OnItemClicked.AddUniqueDynamic(
				this, &UMT2NameplateComponent::HandleItemNameplateClicked);
		}
	}
	return GetUserWidgetObject() != nullptr;
}

void UMT2NameplateComponent::ShowChatMessage(const FString& Message, bool bWorldBroadcast)
{
	if (EnsureWidgetInitialized())
	{
		if (UMT2NameplateWidget* NameplateWidget = Cast<UMT2NameplateWidget>(GetUserWidgetObject()))
		{
			NameplateWidget->ShowChatMessage(Message, bWorldBroadcast);
		}
	}
}

void UMT2NameplateComponent::HandleItemNameplateClicked()
{
	AMT2WorldItem* WorldItem = Cast<AMT2WorldItem>(GetOwner());
	APlayerController* Controller = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	AMT2PlayerCharacter* Character = Controller
		? Cast<AMT2PlayerCharacter>(Controller->GetPawn()) : nullptr;
	if (WorldItem && Character)
	{
		Character->RequestPickupWorldItem(WorldItem);
	}
}

void UMT2NameplateComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UMT2WorldSimulationSubsystem* Simulation = World->GetSubsystem<UMT2WorldSimulationSubsystem>())
		{
			Simulation->UnregisterNameplate(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void UMT2NameplateComponent::RefreshFromSimulationManager()
{
	const UWorld* World = GetWorld();
	const APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;

	FVector ViewLocation = FVector::ZeroVector;
	if (Controller)
	{
		FRotator ViewRotation;
		Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	}
	RefreshFromSimulationManager(ViewLocation, Controller != nullptr);
}

void UMT2NameplateComponent::RefreshFromSimulationManager(
	const FVector& ViewLocation,
	bool bHasViewLocation)
{
	const bool bInRange = bHasViewLocation
		&& (MaxRenderDistance <= 0.0f
			|| FVector::DistSquared(ViewLocation, GetComponentLocation())
				<= FMath::Square(MaxRenderDistance));
	const bool bShouldShow = bInRange && bVisibleThroughOcclusion;

	SetHiddenInGame(!bShouldShow);
	if (bShouldShow)
	{
		// Showing: SetHiddenInGame(false) already re-enabled the tick; make sure of it.
		SetComponentTickEnabled(true);
		bPendingTickDisable = false;
	}
	else
	{
		// Hiding: a screen-space widget component only pulls its on-screen widget off in TickComponent
		// (via UpdateWidgetOnScreen), so we must NOT disable the tick the same moment we hide it, or the
		// plate freezes on screen. Keep ticking one cycle, then stop once it has been removed.
		if (bPendingTickDisable)
		{
			SetComponentTickEnabled(false);
			bPendingTickDisable = false;
		}
		else if (IsComponentTickEnabled())
		{
			bPendingTickDisable = true;
		}
	}

	if (UMT2GMMarkComponent* GMMark = GetOwner()
		? GetOwner()->FindComponentByClass<UMT2GMMarkComponent>() : nullptr)
	{
		GMMark->RefreshFromNameplateVisibility(bShouldShow);
	}
	if (bShouldShow && EnsureWidgetInitialized()) RefreshNameplate();
}

void UMT2NameplateComponent::RefreshMobOcclusionFromSimulationManager(
	const FVector& ViewLocation,
	const FCollisionQueryParams& QueryParams)
{
	const AMT2Mob* Mob = Cast<AMT2Mob>(GetOwner());
	if (!Mob)
	{
		bVisibleThroughOcclusion = true;
		return;
	}

	const EMT2MobType MobType = Mob->GetMobType();
	if (MobType == EMT2MobType::NPC || MobType == EMT2MobType::Warp
		|| MobType == EMT2MobType::Goto)
	{
		bVisibleThroughOcclusion = true;
		return;
	}

	if (MaxRenderDistance > 0.0f
		&& FVector::DistSquared(ViewLocation, GetComponentLocation())
			> FMath::Square(MaxRenderDistance))
	{
		return;
	}

	const UWorld* World = GetWorld();
	bVisibleThroughOcclusion = World
		&& !World->LineTraceTestByChannel(
			ViewLocation,
			Mob->GetActorLocation(),
			ECC_Visibility,
			QueryParams);
}

void UMT2NameplateComponent::RefreshNameplate()
{
	UMT2NameplateWidget* Nameplate = Cast<UMT2NameplateWidget>(GetUserWidgetObject());
	if (!Nameplate)
	{
		return;
	}

	if (const AMT2WorldItem* WorldItem = Cast<AMT2WorldItem>(GetOwner()))
	{
		SetRelativeLocation(FVector(0.0f, 0.0f, 40.0f));
		Nameplate->SetItemInfo(
			WorldItem->GetItemDisplayName(), WorldItem->GetOwnershipDisplayName());
		return;
	}

	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		return;
	}

	if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
	{
		SetRelativeLocation(FVector(0.0f, 0.0f, Capsule->GetScaledCapsuleHalfHeight() + 10.0f));
	}

	if (const AMT2Mob* Mob = Cast<AMT2Mob>(Character))
	{
		const FString DisplayName = Mob->GetMobDisplayName();
		const EMT2MobType MobType = Mob->GetMobType();
		const bool bNpcStyle = MobType == EMT2MobType::NPC ||
			MobType == EMT2MobType::Warp || MobType == EMT2MobType::Goto;
		if (bNpcStyle) Nameplate->SetNpcInfo(DisplayName);
		else Nameplate->SetMobInfo(Mob->GetMobLevel(), DisplayName);
		return;
	}

	if (const AMT2PlayerCharacter* Player = Cast<AMT2PlayerCharacter>(Character))
	{
		if (const AMT2PlayerState* State = Player->GetPlayerState<AMT2PlayerState>())
		{
			if (State->GetGuildId() > 0)
			{
				const APlayerController* LocalController = GetWorld()->GetFirstPlayerController();
				const AMT2PlayerState* LocalState = LocalController ? LocalController->GetPlayerState<AMT2PlayerState>() : nullptr;
				if (UMT2GuildComponent* LocalGuild = LocalState ? LocalState->GetGuildComponent() : nullptr)
					LocalGuild->RequestGuildMark(State->GetGuildId(), 0);
			}
			bool bPartyMember = false;
			const APlayerController* LocalController = GetWorld()
				? GetWorld()->GetFirstPlayerController() : nullptr;
			const AMT2PlayerState* LocalState = LocalController
				? LocalController->GetPlayerState<AMT2PlayerState>() : nullptr;
			if (LocalState)
			{
				for (const FMT2PartyMemberData& Member : LocalState->GetPartyMemberSnapshot())
				{
					if (Member.PlayerState == State ||
						(Member.PlayerId != INDEX_NONE &&
							Member.PlayerId == State->GetPlayerId()))
					{
						bPartyMember = true;
						break;
					}
				}
			}
			Nameplate->SetPlayerInfo(State->GetGuildId(), State->GetGuildName(), State->GetCharacterLevel(),
				State->GetDisplayedKarmaPoints(), State->GetCharacterName(), State->GetEmpire(),
				State->IsAggressiveMode(), bPartyMember);

			// Voice-chat indicator: lit while this player's stream is audible locally (or, on your
			// own plate, while you're transmitting).
			const UMT2VoiceChatClientSubsystem* Voice =
				GetWorld() ? GetWorld()->GetSubsystem<UMT2VoiceChatClientSubsystem>() : nullptr;
			Nameplate->SetSpeaking(Voice && Voice->IsPlayerSpeaking(State->GetPlayerId()));
		}
	}
}
