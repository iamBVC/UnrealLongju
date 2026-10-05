/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Party/MT2Party.h"

#include "Components/MT2HealthComponent.h"
#include "Config/MT2GameplaySettings.h"
#include "Core/MT2ActorUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "Player/MT2PlayerState.h"
#include "Persistence/MT2PersistenceComponent.h"
#include "Server/MT2ServerRuntimeSubsystem.h"

AMT2Party::AMT2Party()
{
	bReplicates = true;
	bAlwaysRelevant = false;
	SetNetUpdateFrequency(5.0f);
	SetMinNetUpdateFrequency(2.0f);
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickInterval = 0.2f;
}

bool AMT2Party::IsNetRelevantFor(
	const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const
{
	const AMT2PlayerState* ViewerState = MT2ActorUtils::ResolvePlayerState(RealViewer, false);
	if (!ViewerState) ViewerState = MT2ActorUtils::ResolvePlayerState(ViewTarget, false);
	return ContainsMember(ViewerState);
}

void AMT2Party::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMT2Party, LeaderState);
	DOREPLIFETIME(AMT2Party, PartyId);
	DOREPLIFETIME(AMT2Party, Members);
}

void AMT2Party::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority())
	{
		SetActorTickEnabled(false);
		return;
	}

	for (int32 Index = MemberStates.Num() - 1; Index >= 0; --Index)
	{
		if (!IsValid(MemberStates[Index]))
		{
			MemberStates.RemoveAt(Index);
		}
	}
	RefreshMemberData();
}

bool AMT2Party::InitializeParty(AMT2PlayerState* Leader, AMT2PlayerState* FirstMember)
{
	if (!HasAuthority() || !Leader || !FirstMember || Leader == FirstMember
		|| Leader->GetParty() || FirstMember->GetParty())
	{
		return false;
	}

	LeaderState = Leader;
	PartyId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	MemberStates = { Leader, FirstMember };
	Members = { MakeMemberData(Leader, true), MakeMemberData(FirstMember, false) };
	Leader->SetParty(this);
	FirstMember->SetParty(this);
	RefreshMemberData(true);
	PublishSnapshot();
	return true;
}

bool AMT2Party::AddMember(AMT2PlayerState* Member)
{
	if (!HasAuthority() || !Member || Member->GetParty() || ContainsMember(Member))
	{
		return false;
	}
	if (MemberStates.Num() >= FMath::Clamp(
		UMT2GameplaySettings::Get().PartyMaximumMembers, 2, 8))
	{
		return false;
	}

	MemberStates.Add(Member);
	Members.Add(MakeMemberData(Member, false));
	Member->SetParty(this);
	RefreshMemberData(true);
	PublishSnapshot();
	return true;
}

void AMT2Party::RemoveMember(AMT2PlayerState* Member)
{
	if (!HasAuthority() || !Member)
	{
		return;
	}
	const UMT2PersistenceComponent* Persistence = Member->GetPersistenceComponent();
	const FString CharacterId = Persistence ? Persistence->GetEntityId() : FString();
	if (!CharacterId.IsEmpty())
	{
		RemoveMemberByCharacterId(CharacterId);
		return;
	}
	MemberStates.RemoveSingle(Member);
	Members.RemoveAll([&](const FMT2PartyMemberData& Data)
	{
		return Data.PlayerState == Member || (!CharacterId.IsEmpty() && Data.CharacterId == CharacterId);
	});
	if (Member->GetParty() == this)
	{
		Member->SetParty(nullptr);
	}
	if (Members.Num() < 2)
	{
		Disband();
		return;
	}
	if (LeaderState == Member && !Members.IsEmpty())
	{
		for (FMT2PartyMemberData& Data : Members) Data.bLeader = false;
		Members[0].bLeader = true;
		LeaderState = Members[0].PlayerState;
	}
	NextLootRecipientIndex = MemberStates.IsEmpty() ? 0 : NextLootRecipientIndex % MemberStates.Num();
	RefreshMemberData(true);
	PublishSnapshot();
}

void AMT2Party::DetachLocalMemberForTravel(AMT2PlayerState* Member)
{
	if (!HasAuthority() || !Member) return;
	MemberStates.RemoveSingle(Member);
	for (FMT2PartyMemberData& Data : Members)
	{
		if (Data.PlayerState == Member)
		{
			Data.PlayerState = nullptr;
			Data.PlayerId = INDEX_NONE;
			Data.bHasWorldLocation = false;
			break;
		}
	}
	if (Member->GetParty() == this) Member->SetParty(nullptr);
	if (LeaderState == Member) LeaderState = nullptr;
	RefreshMemberData(true);
}

bool AMT2Party::RemoveMemberByCharacterId(const FString& CharacterId)
{
	if (!HasAuthority() || CharacterId.IsEmpty()) return false;
	const int32 MemberIndex = Members.IndexOfByPredicate(
		[&](const FMT2PartyMemberData& Data) { return Data.CharacterId == CharacterId; });
	if (MemberIndex == INDEX_NONE) return false;
	AMT2PlayerState* RemovedState = Members[MemberIndex].PlayerState;
	const bool bRemovedLeader = Members[MemberIndex].bLeader;
	Members.RemoveAt(MemberIndex);
	if (RemovedState)
	{
		MemberStates.RemoveSingle(RemovedState);
		if (RemovedState->GetParty() == this) RemovedState->SetParty(nullptr);
	}
	if (Members.Num() < 2)
	{
		Disband();
		return true;
	}
	if (bRemovedLeader)
	{
		for (FMT2PartyMemberData& Data : Members) Data.bLeader = false;
		Members[0].bLeader = true;
		LeaderState = Members[0].PlayerState;
	}
	NextLootRecipientIndex = MemberStates.IsEmpty() ? 0 : NextLootRecipientIndex % MemberStates.Num();
	RefreshMemberData(true);
	PublishSnapshot();
	return true;
}

void AMT2Party::Disband(bool bNotifyCoordinator)
{
	if (!HasAuthority() || IsActorBeingDestroyed())
	{
		return;
	}
	const TArray<TObjectPtr<AMT2PlayerState>> PreviousMembers = MemberStates;
	const FString PreviousPartyId = PartyId;
	MemberStates.Reset();
	Members.Reset();
	for (AMT2PlayerState* Member : PreviousMembers)
	{
		if (Member && Member->GetParty() == this)
		{
			Member->SetParty(nullptr);
		}
	}
	if (bNotifyCoordinator && !PreviousPartyId.IsEmpty())
	{
		if (UMT2ServerRuntimeSubsystem* Runtime = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UMT2ServerRuntimeSubsystem>() : nullptr)
		{
			Runtime->PublishPartyDisband(PreviousPartyId);
		}
	}
	Destroy();
}

bool AMT2Party::IsLeader(const AMT2PlayerState* Member) const
{
	if (!Member) return false;
	if (LeaderState == Member) return true;
	return Members.ContainsByPredicate([Member](const FMT2PartyMemberData& Data)
	{
		return Data.bLeader && Data.PlayerState == Member;
	});
}

bool AMT2Party::ContainsMember(const AMT2PlayerState* Member) const
{
	if (!Member)
	{
		return false;
	}
	if (HasAuthority())
	{
		return MemberStates.Contains(Member);
	}
	return Members.ContainsByPredicate([Member](const FMT2PartyMemberData& Data)
	{
		return Data.PlayerState == Member ||
			(Data.PlayerId != INDEX_NONE && Data.PlayerId == Member->GetPlayerId());
	});
}

bool AMT2Party::ContainsCharacter(const FString& CharacterId) const
{
	return !CharacterId.IsEmpty() && Members.ContainsByPredicate(
		[&](const FMT2PartyMemberData& Data) { return Data.CharacterId == CharacterId; });
}

AMT2PlayerState* AMT2Party::ClaimNextLootRecipient()
{
	if (!HasAuthority() || MemberStates.IsEmpty())
	{
		return nullptr;
	}
	NextLootRecipientIndex %= MemberStates.Num();
	AMT2PlayerState* Recipient = MemberStates[NextLootRecipientIndex];
	NextLootRecipientIndex = (NextLootRecipientIndex + 1) % MemberStates.Num();
	return Recipient;
}

void AMT2Party::RefreshMemberData(bool bForce)
{
	TArray<FMT2PartyMemberData> Updated = Members;
	for (AMT2PlayerState* State : MemberStates)
	{
		if (!State)
		{
			continue;
		}
		const UMT2PersistenceComponent* Persistence = State->GetPersistenceComponent();
		const FString CharacterId = Persistence ? Persistence->GetEntityId() : FString();
		FMT2PartyMemberData* Existing = Updated.FindByPredicate([&](const FMT2PartyMemberData& Data)
		{
			return Data.PlayerState == State || (!CharacterId.IsEmpty() && Data.CharacterId == CharacterId);
		});
		if (!Existing) Existing = &Updated.AddDefaulted_GetRef();
		FMT2PartyMemberData& Data = *Existing;
		const bool bLeader = State == LeaderState || Data.bLeader;
		Data = MakeMemberData(State, bLeader);
		if (const APawn* Pawn = State->GetPawn())
		{
			Data.WorldLocation = Pawn->GetActorLocation();
			Data.bHasWorldLocation = true;
			if (const UMT2HealthComponent* Health = Pawn->FindComponentByClass<UMT2HealthComponent>())
			{
				Data.Health = Health->GetHealth();
				Data.MaxHealth = FMath::Max(Health->GetMaxHealth(), 1.0f);
			}
		}
	}

	if (bForce || Updated != Members)
	{
		Members = MoveTemp(Updated);
		for (AMT2PlayerState* State : MemberStates)
		{
			if (State) State->SetPartyMemberSnapshot(Members);
		}
		OnRep_Members();
		ForceNetUpdate();
	}
}

FMT2PartyMemberData AMT2Party::MakeMemberData(AMT2PlayerState* State, bool bLeader) const
{
	FMT2PartyMemberData Data;
	if (!State) return Data;
	Data.PlayerState = State;
	if (const UMT2PersistenceComponent* Persistence = State->GetPersistenceComponent())
	{
		Data.CharacterId = Persistence->GetEntityId();
	}
	Data.PlayerId = State->GetPlayerId();
	Data.CharacterName = State->GetCharacterName();
	Data.Level = State->GetCharacterLevel();
	Data.Appearance = State->GetCharacterAppearance();
	Data.bLeader = bLeader;
	if (const UMT2ServerRuntimeSubsystem* Runtime = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMT2ServerRuntimeSubsystem>() : nullptr)
	{
		Data.MapId = Runtime->GetMapId();
		Data.Channel = Runtime->GetChannel();
	}
	if (const APawn* Pawn = State->GetPawn())
	{
		Data.WorldLocation = Pawn->GetActorLocation();
		Data.bHasWorldLocation = true;
		if (const UMT2HealthComponent* Health = Pawn->FindComponentByClass<UMT2HealthComponent>())
		{
			Data.Health = Health->GetHealth();
			Data.MaxHealth = FMath::Max(Health->GetMaxHealth(), 1.0f);
		}
	}
	return Data;
}

void AMT2Party::ApplyCoordinatorSnapshot(
	const FString& InPartyId, const TArray<FMT2PartyMemberData>& InMembers,
	const TArray<AMT2PlayerState*>& LocalMemberStates)
{
	if (!HasAuthority() || InPartyId.IsEmpty() || InMembers.Num() < 2) return;
	for (AMT2PlayerState* Previous : MemberStates)
	{
		if (Previous && !LocalMemberStates.Contains(Previous) && Previous->GetParty() == this)
		{
			Previous->SetParty(nullptr);
		}
	}
	PartyId = InPartyId;
	Members = InMembers;
	MemberStates.Reset();
	LeaderState = nullptr;
	for (AMT2PlayerState* LocalState : LocalMemberStates)
	{
		if (!LocalState) continue;
		const UMT2PersistenceComponent* Persistence = LocalState->GetPersistenceComponent();
		const FString CharacterId = Persistence ? Persistence->GetEntityId() : FString();
		if (FMT2PartyMemberData* Data = Members.FindByPredicate([&](const FMT2PartyMemberData& Entry)
		{
			return !CharacterId.IsEmpty() && Entry.CharacterId == CharacterId;
		}))
		{
			Data->PlayerState = LocalState;
			Data->PlayerId = LocalState->GetPlayerId();
			MemberStates.Add(LocalState);
			LocalState->SetParty(this);
			if (Data->bLeader) LeaderState = LocalState;
		}
	}
	RefreshMemberData(true);
}

void AMT2Party::PublishSnapshot() const
{
	if (!HasAuthority() || PartyId.IsEmpty() || Members.Num() < 2) return;
	if (UMT2ServerRuntimeSubsystem* Runtime = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMT2ServerRuntimeSubsystem>() : nullptr)
	{
		Runtime->PublishPartySnapshot(PartyId, Members);
	}
}

void AMT2Party::OnRep_Members()
{
	OnMembersChanged.Broadcast();
}

void AMT2Party::SetQuestFlag(FName FlagName, int32 Value)
{
	if (HasAuthority() && !FlagName.IsNone())
	{
		SetScriptFlag(FlagName.ToString(), Value);
	}
}

void AMT2Party::SetScriptFlag(const FString& FlagName, int32 Value)
{
	if (HasAuthority()) { QuestFlags.FindOrAdd(FlagName) = Value; }
}
