/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2PartyPanelWidget.h"
#include "Config/MT2PathSettings.h"

#include "Components/Button.h"
#include "Components/VerticalBox.h"
#include "Party/MT2Party.h"
#include "Player/MT2PlayerState.h"
#include "Player/MT2PlayerController.h"
#include "UI/MT2PartyMemberWidget.h"

UMT2PartyPanelWidget::UMT2PartyPanelWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	MemberWidgetClass = TSoftClassPtr<UMT2PartyMemberWidget>(
		FSoftObjectPath(UMT2PathSettings::Path(TEXT("UI_MT2PartyMember"))));
}

void UMT2PartyPanelWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// Keep the widget alive and ticking while visually absent. A Collapsed widget can miss the first
	// replicated PlayerState/Party assignment and never bind itself afterwards.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	SetRenderOpacity(0.0f);
	DisbandButton->OnClicked.AddUniqueDynamic(
		this, &UMT2PartyPanelWidget::HandleDisbandClicked);
	LeavePartyButton->OnClicked.AddUniqueDynamic(
		this, &UMT2PartyPanelWidget::HandleLeaveClicked);
	RefreshPartyBinding();
	// BoundParty starts null, so a solo player does not enter the pointer-change branch above.
	// Always perform the initial population/collapse explicitly.
	RebuildMembers();
}

void UMT2PartyPanelWidget::NativeDestruct()
{
	UnbindParty();
	if (AMT2PlayerState* State = BoundPlayerState.Get())
	{
		State->OnPartyChanged.RemoveDynamic(this, &UMT2PartyPanelWidget::HandlePartyChanged);
		State->OnPartyMemberSnapshotChanged.RemoveDynamic(
			this, &UMT2PartyPanelWidget::HandlePlayerSnapshotChanged);
		State->OnPartyMembershipChanged.RemoveDynamic(
			this, &UMT2PartyPanelWidget::HandleMembershipChanged);
	}
	BoundPlayerState.Reset();
	MemberRows.Reset();
	Super::NativeDestruct();
}

void UMT2PartyPanelWidget::NativeTick(
	const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshPartyBinding();
}

void UMT2PartyPanelWidget::RefreshPartyBinding()
{
	AMT2PlayerState* State = GetOwningPlayerState<AMT2PlayerState>();
	if (BoundPlayerState.Get() != State)
	{
		if (AMT2PlayerState* Previous = BoundPlayerState.Get())
		{
			Previous->OnPartyChanged.RemoveDynamic(
				this, &UMT2PartyPanelWidget::HandlePartyChanged);
			Previous->OnPartyMemberSnapshotChanged.RemoveDynamic(
				this, &UMT2PartyPanelWidget::HandlePlayerSnapshotChanged);
			Previous->OnPartyMembershipChanged.RemoveDynamic(
				this, &UMT2PartyPanelWidget::HandleMembershipChanged);
		}
		BoundPlayerState = State;
		if (State)
		{
			State->OnPartyChanged.AddUniqueDynamic(
				this, &UMT2PartyPanelWidget::HandlePartyChanged);
			State->OnPartyMemberSnapshotChanged.AddUniqueDynamic(
				this, &UMT2PartyPanelWidget::HandlePlayerSnapshotChanged);
			State->OnPartyMembershipChanged.AddUniqueDynamic(
				this, &UMT2PartyPanelWidget::HandleMembershipChanged);
		}
	}

	AMT2Party* Party = State ? State->GetParty() : nullptr;
	if (BoundParty.Get() != Party)
	{
		UnbindParty();
		BoundParty = Party;
		if (Party)
		{
			Party->OnMembersChanged.AddUniqueDynamic(
				this, &UMT2PartyPanelWidget::HandleMembersChanged);
		}
		RebuildMembers();
	}
}

void UMT2PartyPanelWidget::UnbindParty()
{
	if (AMT2Party* Party = BoundParty.Get())
	{
		Party->OnMembersChanged.RemoveDynamic(
			this, &UMT2PartyPanelWidget::HandleMembersChanged);
	}
	BoundParty.Reset();
}

void UMT2PartyPanelWidget::RebuildMembers()
{
	AMT2Party* Party = BoundParty.Get();
	AMT2PlayerState* LocalState = GetOwningPlayerState<AMT2PlayerState>();
	const TArray<FMT2PartyMemberData>* Members = Party && Party->GetMembers().Num() >= 2
		? &Party->GetMembers() : (LocalState ? &LocalState->GetPartyMemberSnapshot() : nullptr);
	if (!LocalState || !LocalState->IsInParty() || !Members || Members->Num() < 2)
	{
		MemberList->ClearChildren();
		MemberRows.Reset();
		SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		SetRenderOpacity(0.0f);
		return;
	}

	const TSubclassOf<UMT2PartyMemberWidget> RowClass = MemberWidgetClass.LoadSynchronous();
	if (!RowClass)
	{
		SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		SetRenderOpacity(0.0f);
		return;
	}

	const bool bLocalLeader = Party ? Party->IsLeader(LocalState)
		: Members->ContainsByPredicate([LocalState](const FMT2PartyMemberData& Member)
		{
			return Member.bLeader && (Member.PlayerState == LocalState
				|| (Member.PlayerId != INDEX_NONE && Member.PlayerId == LocalState->GetPlayerId()));
		});
	DisbandButton->SetVisibility(bLocalLeader
		? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	LeavePartyButton->SetVisibility(bLocalLeader
		? ESlateVisibility::Collapsed : ESlateVisibility::Visible);

	TSet<FString> ActiveKeys;
	for (const FMT2PartyMemberData& Member : *Members)
	{
		const FString Key = !Member.CharacterId.IsEmpty() ? Member.CharacterId
			: (!Member.CharacterName.IsEmpty() ? Member.CharacterName : FString::FromInt(Member.PlayerId));
		ActiveKeys.Add(Key);
	}
	for (auto It = MemberRows.CreateIterator(); It; ++It)
	{
		if (!ActiveKeys.Contains(It.Key()))
		{
			if (It.Value()) It.Value()->RemoveFromParent();
			It.RemoveCurrent();
		}
	}

	// Keep row widget instances stable while replicated HP/location snapshots update.
	MemberList->ClearChildren();
	for (const FMT2PartyMemberData& Member : *Members)
	{
		const FString Key = !Member.CharacterId.IsEmpty() ? Member.CharacterId
			: (!Member.CharacterName.IsEmpty() ? Member.CharacterName : FString::FromInt(Member.PlayerId));
		TObjectPtr<UMT2PartyMemberWidget>& Row = MemberRows.FindOrAdd(Key);
		if (!Row)
		{
			Row = CreateWidget<UMT2PartyMemberWidget>(GetOwningPlayer(), RowClass);
		}
		if (Row)
		{
			const bool bIsLocalMember = Member.PlayerState == LocalState
				|| (Member.PlayerId != INDEX_NONE && Member.PlayerId == LocalState->GetPlayerId());
			const bool bCanKick = bLocalLeader && !bIsLocalMember;
			Row->SetMember(Member, bCanKick);
			MemberList->AddChildToVerticalBox(Row);
		}
	}
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	SetRenderOpacity(Members->Num() < 2 ? 0.0f : 1.0f);
}

void UMT2PartyPanelWidget::HandlePartyChanged(AMT2Party*, AMT2Party*)
{
	RefreshPartyBinding();
}

void UMT2PartyPanelWidget::HandleMembersChanged()
{
	RebuildMembers();
}

void UMT2PartyPanelWidget::HandlePlayerSnapshotChanged()
{
	RebuildMembers();
}

void UMT2PartyPanelWidget::HandleMembershipChanged(bool)
{
	RebuildMembers();
}

void UMT2PartyPanelWidget::HandleDisbandClicked()
{
	if (AMT2PlayerController* Controller = Cast<AMT2PlayerController>(GetOwningPlayer()))
	{
		Controller->RequestDisbandParty();
	}
}

void UMT2PartyPanelWidget::HandleLeaveClicked()
{
	if (AMT2PlayerController* Controller = Cast<AMT2PlayerController>(GetOwningPlayer()))
	{
		Controller->RequestLeaveParty();
	}
}
