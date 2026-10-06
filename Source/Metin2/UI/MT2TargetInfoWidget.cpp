/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2TargetInfoWidget.h"
#include "Config/MT2PathSettings.h"

#include "UI/MT2HUD.h"
#include "Messenger/MT2MessengerComponent.h"

#include "Characters/MT2CharacterBase.h"
#include "Characters/MT2PlayerCharacter.h"
#include "Combat/MT2CombatComponent.h"
#include "Duel/MT2DuelComponent.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/MT2HealthComponent.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Mobs/MT2Mob.h"
#include "Party/MT2Party.h"
#include "Party/MT2PartyTypes.h"
#include "Player/MT2PlayerController.h"
#include "Player/MT2PlayerState.h"
#include "Guild/MT2GuildComponent.h"
#include "UI/MT2UIStyle.h"

namespace
{
	// small_thin_button_01/02/03.sub regions inside Public.dds (see uitarget.py TargetBoard).
	const FMT2AtlasRegion SmallThinNormal(114, 202, 174, 222);
	const FMT2AtlasRegion SmallThinHovered(174, 202, 234, 222);
	const FMT2AtlasRegion SmallThinPressed(0, 232, 60, 252);
}

void UMT2TargetInfoWidget::NativeConstruct()
{
	Super::NativeConstruct();
	CloseButton->OnClicked.AddUniqueDynamic(this, &UMT2TargetInfoWidget::HandleCloseClicked);
	EnsurePlayerActionRow();
	SetVisibility(ESlateVisibility::Collapsed);
	BindCombatComponent();
	BindLocalPartyState();
}

void UMT2TargetInfoWidget::EnsurePlayerActionRow()
{
	UCanvasPanel* RootCanvas = PanelBackground
		? Cast<UCanvasPanel>(PanelBackground->GetParent()) : nullptr;
	if (PlayerActionRow || !RootCanvas || !WidgetTree)
	{
		return;
	}

	PlayerActionRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	UTexture2D* Public = FMT2UIStyle::LoadTexture(UMT2PathSettings::Path(TEXT("UI_PublicAtlas")));
	struct FActionButton
	{
		const TCHAR* Label;
		FName HandlerName;  // UFUNCTION name (AddDynamic needs a literal, so bind by name)
	};
	// Old TargetBoard default player set (whisper/exchange/fight) plus the conditional
	// party/friend/guild entries. Visibility conditions (leader, guild authority, already a
	// friend...) come with their subsystems - for now the full row shows for any player target.
	const FActionButton Buttons[] = {
		{TEXT("Message"), GET_FUNCTION_NAME_CHECKED(UMT2TargetInfoWidget, HandleWhisperClicked)},
		{TEXT("Trade"), GET_FUNCTION_NAME_CHECKED(UMT2TargetInfoWidget, HandleTradeClicked)},
		{TEXT("Duel"), GET_FUNCTION_NAME_CHECKED(UMT2TargetInfoWidget, HandleDuelClicked)},
		{TEXT("Invite to party"), GET_FUNCTION_NAME_CHECKED(UMT2TargetInfoWidget, HandlePartyClicked)},
		{TEXT("Friend"), GET_FUNCTION_NAME_CHECKED(UMT2TargetInfoWidget, HandleFriendClicked)},
		{TEXT("Guild"), GET_FUNCTION_NAME_CHECKED(UMT2TargetInfoWidget, HandleGuildClicked)},
	};
	for (const FActionButton& Entry : Buttons)
	{
		UButton* Button = FMT2UIStyle::AtlasButton(
			*WidgetTree, Public, SmallThinNormal, SmallThinHovered, SmallThinPressed);
		Button->AddChild(FMT2UIStyle::Label(*WidgetTree, FText::FromString(Entry.Label), 8));
		if (Entry.HandlerName == GET_FUNCTION_NAME_CHECKED(UMT2TargetInfoWidget, HandleDuelClicked))
		{
			DuelButton = Button;
			DuelLabel = Cast<UTextBlock>(Button->GetContent());
		}
		FScriptDelegate ClickDelegate;
		ClickDelegate.BindUFunction(this, Entry.HandlerName);
		Button->OnClicked.AddUnique(ClickDelegate);
		if (Entry.HandlerName == GET_FUNCTION_NAME_CHECKED(
			UMT2TargetInfoWidget, HandlePartyClicked))
		{
			PartyButton = Button;
		}
		UHorizontalBoxSlot* ButtonSlot = PlayerActionRow->AddChildToHorizontalBox(Button);
		ButtonSlot->SetPadding(FMargin(1.0f, 0.0f));
	}

	// Centered directly under the 48px board, like the old client's button strip.
	UCanvasPanelSlot* RowSlot = RootCanvas->AddChildToCanvas(PlayerActionRow);
	RowSlot->SetAnchors(FAnchors(0.5f, 0.0f));
	RowSlot->SetAlignment(FVector2D(0.5f, 0.0f));
	RowSlot->SetPosition(FVector2D(0.0f, 50.0f));
	RowSlot->SetAutoSize(true);
	PlayerActionRow->SetVisibility(ESlateVisibility::Collapsed);
}

void UMT2TargetInfoWidget::NativeDestruct()
{
	if (UMT2DuelComponent* Duel = BoundDuels.Get()) { Duel->OnDuelsChanged.RemoveDynamic(this, &UMT2TargetInfoWidget::HandleDuelsChanged); }
	BoundDuels.Reset();
	UnbindTargetPlayerState();
	UnbindLocalPartyState();
	UnbindTargetHealth();
	UnbindCombatComponent();
	Super::NativeDestruct();
}

void UMT2TargetInfoWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	BindCombatComponent();
	BindLocalPartyState();
	BindLocalDuels();
	if (UMT2CombatComponent* Combat = BoundCombatComponent.Get())
	{
		AActor* SelectedTarget = Combat->GetSelectedTarget();
		if (SelectedTarget != TargetActor.Get() || (!SelectedTarget && GetVisibility() != ESlateVisibility::Collapsed))
		{
			SetTarget(SelectedTarget);
		}
		else if (SelectedTarget && !TargetPlayerState.IsValid()
			&& Cast<AMT2PlayerCharacter>(SelectedTarget))
		{
			BindTargetPlayerState();
			RefreshTargetInfo();
		}
	}
}

void UMT2TargetInfoWidget::RefreshCombatBinding()
{
	BindCombatComponent();
}

void UMT2TargetInfoWidget::BindCombatComponent()
{
	const AMT2CharacterBase* Character = Cast<AMT2CharacterBase>(GetOwningPlayerPawn());
	UMT2CombatComponent* Combat = Character ? Character->GetCombatComponent() : nullptr;
	if (BoundCombatComponent.Get() == Combat)
	{
		return;
	}

	UnbindCombatComponent();
	BoundCombatComponent = Combat;
	if (Combat)
	{
		Combat->OnSelectedTargetChanged.AddUniqueDynamic(this, &UMT2TargetInfoWidget::HandleSelectedTargetChanged);
		SetTarget(Combat->GetSelectedTarget());
	}
}

void UMT2TargetInfoWidget::UnbindCombatComponent()
{
	if (UMT2CombatComponent* Combat = BoundCombatComponent.Get())
	{
		Combat->OnSelectedTargetChanged.RemoveDynamic(this, &UMT2TargetInfoWidget::HandleSelectedTargetChanged);
	}
	BoundCombatComponent.Reset();
}

void UMT2TargetInfoWidget::SetTarget(AActor* NewTarget)
{
	if (TargetActor.Get() == NewTarget && NewTarget)
	{
		RefreshTargetInfo();
		return;
	}

	UnbindTargetHealth();
	UnbindTargetPlayerState();
	if (AActor* OldTarget = TargetActor.Get())
	{
		OldTarget->OnDestroyed.RemoveDynamic(this, &UMT2TargetInfoWidget::HandleTargetDestroyed);
	}
	TargetActor = NewTarget;

	if (!NewTarget)
	{
		TargetNameText->SetText(FText::GetEmpty());
		HealthBar->SetPercent(0.0f);
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	NewTarget->OnDestroyed.AddUniqueDynamic(this, &UMT2TargetInfoWidget::HandleTargetDestroyed);
	BindTargetHealth();
	BindTargetPlayerState();
	RefreshTargetInfo();
	SetVisibility(ESlateVisibility::Visible);
}

void UMT2TargetInfoWidget::BindTargetPlayerState()
{
	const AMT2PlayerCharacter* Player = Cast<AMT2PlayerCharacter>(TargetActor.Get());
	AMT2PlayerState* State = Player ? Player->GetPlayerState<AMT2PlayerState>() : nullptr;
	TargetPlayerState = State;
	if (State)
	{
		State->OnPartyMembershipChanged.AddUniqueDynamic(
			this, &UMT2TargetInfoWidget::HandleTargetPartyMembershipChanged);
	}
}

void UMT2TargetInfoWidget::UnbindTargetPlayerState()
{
	if (AMT2PlayerState* State = TargetPlayerState.Get())
	{
		State->OnPartyMembershipChanged.RemoveDynamic(
			this, &UMT2TargetInfoWidget::HandleTargetPartyMembershipChanged);
	}
	TargetPlayerState.Reset();
}

void UMT2TargetInfoWidget::BindLocalDuels()
{
	AMT2PlayerState* State = GetOwningPlayer() ? GetOwningPlayer()->GetPlayerState<AMT2PlayerState>() : nullptr;
	UMT2DuelComponent* Duel = State ? State->GetDuelComponent() : nullptr;
	if (BoundDuels.Get() == Duel) { return; }
	if (UMT2DuelComponent* Old = BoundDuels.Get()) { Old->OnDuelsChanged.RemoveDynamic(this, &UMT2TargetInfoWidget::HandleDuelsChanged); }
	BoundDuels = Duel;
	if (Duel) { Duel->OnDuelsChanged.AddUniqueDynamic(this, &UMT2TargetInfoWidget::HandleDuelsChanged); }
	RefreshTargetInfo();
}

void UMT2TargetInfoWidget::HandleDuelsChanged() { RefreshTargetInfo(); }

FText UMT2TargetInfoWidget::BuildDuelActionLabel(const FMT2DuelEntry* Duel)
{
	if (!Duel) { return NSLOCTEXT("MT2Duel", "Challenge", "Duel"); }
	if (Duel->Phase == EMT2DuelPhase::Fighting) { return NSLOCTEXT("MT2Duel", "Fighting", "Fighting"); }
	if (Duel->Phase == EMT2DuelPhase::Revenge)
	{
		return Duel->bCanAccept ? NSLOCTEXT("MT2Duel", "Revenge", "Revenge")
			: NSLOCTEXT("MT2Duel", "Challenge", "Duel");
	}
	return Duel->bCanAccept ? NSLOCTEXT("MT2Duel", "Accept", "Accept duel")
		: NSLOCTEXT("MT2Duel", "Waiting", "Waiting...");
}

void UMT2TargetInfoWidget::BindLocalPartyState()
{
	AMT2PlayerState* State = GetOwningPlayerState<AMT2PlayerState>();
	if (LocalPartyPlayerState.Get() == State)
	{
		return;
	}
	UnbindLocalPartyState();
	LocalPartyPlayerState = State;
	if (State)
	{
		State->OnPartyMembershipChanged.AddUniqueDynamic(
			this, &UMT2TargetInfoWidget::HandleLocalPartyMembershipChanged);
		State->OnPartyMemberSnapshotChanged.AddUniqueDynamic(
			this, &UMT2TargetInfoWidget::HandleLocalPartySnapshotChanged);
	}
}

void UMT2TargetInfoWidget::UnbindLocalPartyState()
{
	if (AMT2PlayerState* State = LocalPartyPlayerState.Get())
	{
		State->OnPartyMembershipChanged.RemoveDynamic(
			this, &UMT2TargetInfoWidget::HandleLocalPartyMembershipChanged);
		State->OnPartyMemberSnapshotChanged.RemoveDynamic(
			this, &UMT2TargetInfoWidget::HandleLocalPartySnapshotChanged);
	}
	LocalPartyPlayerState.Reset();
}

void UMT2TargetInfoWidget::BindTargetHealth()
{
	UMT2HealthComponent* Health = TargetActor.IsValid()
		? TargetActor->FindComponentByClass<UMT2HealthComponent>() : nullptr;
	TargetHealthComponent = Health;
	if (Health)
	{
		Health->OnValueChanged.AddUniqueDynamic(this, &UMT2TargetInfoWidget::HandleTargetResourceChanged);
		Health->OnMaxValueChanged.AddUniqueDynamic(this, &UMT2TargetInfoWidget::HandleTargetResourceChanged);
		Health->OnDeath.AddUniqueDynamic(this, &UMT2TargetInfoWidget::HandleTargetDeath);
	}
}

void UMT2TargetInfoWidget::UnbindTargetHealth()
{
	if (UMT2HealthComponent* Health = TargetHealthComponent.Get())
	{
		Health->OnValueChanged.RemoveDynamic(this, &UMT2TargetInfoWidget::HandleTargetResourceChanged);
		Health->OnMaxValueChanged.RemoveDynamic(this, &UMT2TargetInfoWidget::HandleTargetResourceChanged);
		Health->OnDeath.RemoveDynamic(this, &UMT2TargetInfoWidget::HandleTargetDeath);
	}
	TargetHealthComponent.Reset();
}

void UMT2TargetInfoWidget::RefreshTargetInfo()
{
	AActor* Target = TargetActor.Get();
	if (!Target)
	{
		return;
	}

	TargetNameText->SetText(BuildTargetLabel(Target));

	// Old TargetBoard: another player shows the action-button strip and no HP gauge (the gauge
	// only ever appeared for attackable targets); mobs/NPCs show the gauge and no buttons.
	const bool bPlayerTarget =
		Target != GetOwningPlayerPawn() && Cast<AMT2PlayerCharacter>(Target) != nullptr;
	if (PlayerActionRow)
	{
		PlayerActionRow->SetVisibility(
			bPlayerTarget ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (HealthBar)
	{
		HealthBar->SetVisibility(
			bPlayerTarget ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
	if (bPlayerTarget)
	{
		const AMT2PlayerCharacter* TargetPlayer = Cast<AMT2PlayerCharacter>(Target);
		const AMT2PlayerState* TargetState = TargetPlayer
			? TargetPlayer->GetPlayerState<AMT2PlayerState>() : nullptr;
		const AMT2PlayerState* LocalState = GetOwningPlayerState<AMT2PlayerState>();
		const bool bLocalPartyLeader = LocalState && LocalState->IsInParty() &&
			(LocalState->GetParty() ? LocalState->GetParty()->IsLeader(LocalState)
				: LocalState->GetPartyMemberSnapshot().ContainsByPredicate(
					[LocalState](const FMT2PartyMemberData& Member)
					{
						return Member.bLeader && (Member.PlayerState == LocalState ||
							(Member.PlayerId != INDEX_NONE &&
								Member.PlayerId == LocalState->GetPlayerId()));
					}));
		const bool bCanInvite = TargetState && !TargetState->IsInParty()
			&& (!LocalState || !LocalState->IsInParty() || bLocalPartyLeader);
		if (DuelButton && DuelLabel)
		{
			const FMT2DuelEntry* Duel = LocalState ? LocalState->GetDuelComponent()->FindDuel(TargetState) : nullptr;
			DuelButton->SetIsEnabled(LocalState && TargetState && (!Duel || Duel->bCanAccept));
			DuelLabel->SetText(BuildDuelActionLabel(Duel));
			DuelButton->SetToolTipText(Duel && Duel->Phase == EMT2DuelPhase::Revenge && !Duel->bCanAccept
				? NSLOCTEXT("MT2Duel", "WonTooltip", "You won. Your opponent can request revenge after respawning.")
				: FText::GetEmpty());
		}
		if (PartyButton)
		{
			PartyButton->SetVisibility(
				bCanInvite ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		}
		return;
	}

	if (const UMT2HealthComponent* Health = TargetHealthComponent.Get())
	{
		const float CurrentHealth = Health->GetHealth();
		const float MaxHealth = Health->GetMaxHealth();
		HealthBar->SetPercent(MaxHealth > 0.0f ? FMath::Clamp(CurrentHealth / MaxHealth, 0.0f, 1.0f) : 0.0f);
		HealthBar->SetToolTipText(FText::Format(NSLOCTEXT("MT2TargetInfo", "HealthTooltip", "HP: {0} / {1}"),
			FText::AsNumber(FMath::RoundToInt(CurrentHealth)), FText::AsNumber(FMath::RoundToInt(MaxHealth))));
	}
	else
	{
		HealthBar->SetPercent(1.0f);
		HealthBar->SetToolTipText(FText::GetEmpty());
	}
}

FText UMT2TargetInfoWidget::BuildTargetLabel(const AActor* Target) const
{
	if (const AMT2Mob* Mob = Cast<AMT2Mob>(Target))
	{
		const FString Name = Mob->GetMobDisplayName();
		const EMT2MobType MobType = Mob->GetMobType();
		const bool bNpc = MobType == EMT2MobType::NPC || MobType == EMT2MobType::Warp ||
			MobType == EMT2MobType::Goto;
		if (bNpc)
		{
			return FText::FromString(Name);
		}
		return FText::Format(NSLOCTEXT("MT2TargetInfo", "MobLabel", "Lv. {0} (Grade {1}) {2}"),
			FText::AsNumber(Mob->GetMobLevel()),
			FText::AsNumber(static_cast<int32>(Mob->GetMobRank()) + 1),
			FText::FromString(Name));
	}

	if (const AMT2PlayerCharacter* Player = Cast<AMT2PlayerCharacter>(Target))
	{
		if (const AMT2PlayerState* State = Player->GetPlayerState<AMT2PlayerState>())
		{
			return FText::Format(NSLOCTEXT("MT2TargetInfo", "PlayerLabel", "Lv. {0} {1}"),
				FText::AsNumber(State->GetCharacterLevel()), FText::FromString(State->GetCharacterName()));
		}
	}

	return FText::FromString(Target->GetActorNameOrLabel());
}

void UMT2TargetInfoWidget::HandleSelectedTargetChanged(AActor*, AActor* NewTarget)
{
	SetTarget(NewTarget);
}

void UMT2TargetInfoWidget::HandleTargetResourceChanged(float, float)
{
	RefreshTargetInfo();
}

void UMT2TargetInfoWidget::HandleTargetDeath()
{
	HandleCloseClicked();
}

void UMT2TargetInfoWidget::HandleTargetDestroyed(AActor*)
{
	HandleCloseClicked();
}

void UMT2TargetInfoWidget::HandleTargetPartyMembershipChanged(bool)
{
	RefreshTargetInfo();
}

void UMT2TargetInfoWidget::HandleLocalPartyMembershipChanged(bool)
{
	RefreshTargetInfo();
}

void UMT2TargetInfoWidget::HandleLocalPartySnapshotChanged()
{
	RefreshTargetInfo();
}

void UMT2TargetInfoWidget::RequestPlayerAction(FName Action)
{
	AActor* Target = TargetActor.Get();
	if (!Target)
	{
		return;
	}
	OnPlayerActionRequested.Broadcast(Action, Target);
	if (Action == TEXT("Duel"))
	{
		if (AMT2PlayerController* PC = Cast<AMT2PlayerController>(GetOwningPlayer()))
		{
			PC->RequestDuel(Cast<AMT2PlayerCharacter>(Target));
		}
		return;
	}

	// Message opens the messenger on a private conversation with this player - the old client's
	// whisper window. The character id is resolved server-side, so only the actor travels from here.
	if (Action == TEXT("Whisper"))
	{
		if (AMT2HUD* HUD = GetOwningPlayer() ? GetOwningPlayer()->GetHUD<AMT2HUD>() : nullptr)
		{
			HUD->OpenMessengerConversation(Target);
			return;
		}
	}

	// Friend asks that player to be friends; they answer a prompt, exactly as the old messenger's
	// RequestToAdd -> messenger_auth handshake did.
	if (Action == TEXT("Friend"))
	{
		AMT2PlayerState* State = GetOwningPlayer()
			? GetOwningPlayer()->GetPlayerState<AMT2PlayerState>() : nullptr;
		if (UMT2MessengerComponent* Messenger = State ? State->GetMessengerComponent() : nullptr)
		{
			Messenger->RequestAddFriendPlayer(Target);
			return;
		}
	}
	if (Action == TEXT("Trade"))
	{
		if (AMT2PlayerController* Controller = Cast<AMT2PlayerController>(GetOwningPlayer()))
		{
			Controller->RequestTrade(Cast<AMT2PlayerCharacter>(Target));
			return;
		}
	}

	// The remaining actions have no subsystem yet; answer like the old client's info chat so the
	// click visibly does something.
	if (AMT2PlayerController* Controller = Cast<AMT2PlayerController>(GetOwningPlayer()))
	{
		Controller->AddInfoChatLine(
			FString::Printf(TEXT("%s: this feature is not available yet."), *Action.ToString()));
	}
}

void UMT2TargetInfoWidget::HandleWhisperClicked() { RequestPlayerAction(TEXT("Whisper")); }
void UMT2TargetInfoWidget::HandleTradeClicked() { RequestPlayerAction(TEXT("Trade")); }
void UMT2TargetInfoWidget::HandleDuelClicked() { RequestPlayerAction(TEXT("Duel")); }
void UMT2TargetInfoWidget::HandlePartyClicked()
{
	if (AMT2PlayerController* Controller = Cast<AMT2PlayerController>(GetOwningPlayer()))
	{
		Controller->RequestPartyInvite(Cast<AMT2PlayerCharacter>(TargetActor.Get()));
	}
}
void UMT2TargetInfoWidget::HandleFriendClicked() { RequestPlayerAction(TEXT("Friend")); }
void UMT2TargetInfoWidget::HandleGuildClicked()
{
	const AMT2PlayerState* LocalState = GetOwningPlayerState<AMT2PlayerState>();
	if (UMT2GuildComponent* Guild = LocalState ? LocalState->GetGuildComponent() : nullptr)
	{
		Guild->InvitePlayer(TargetActor.Get());
	}
}

void UMT2TargetInfoWidget::HandleCloseClicked()
{
	if (UMT2CombatComponent* Combat = BoundCombatComponent.Get())
	{
		Combat->SetSelectedTarget(nullptr);
	}
	else
	{
		SetTarget(nullptr);
	}
}
