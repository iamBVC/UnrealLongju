#include "Duel/MT2DuelComponent.h"
#include "Characters/MT2PlayerCharacter.h"
#include "Components/MT2HealthComponent.h"
#include "Config/MT2GameplaySettings.h"
#include "Player/MT2PlayerState.h"
#include "Player/MT2PlayerController.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

namespace
{
	void Notify(AMT2PlayerState* State, const FString& Text)
	{
		if (AMT2PlayerController* PC = State ? Cast<AMT2PlayerController>(State->GetOwner()) : nullptr)
		{
			PC->SendSystemChatMessage(Text);
		}
	}
}

UMT2DuelComponent::UMT2DuelComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UMT2DuelComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMT2DuelComponent, Duels);
}

AMT2PlayerState* UMT2DuelComponent::PlayerState() const { return Cast<AMT2PlayerState>(GetOwner()); }

const FMT2DuelEntry* UMT2DuelComponent::FindDuel(const AMT2PlayerState* Opponent) const
{
	return Duels.FindByPredicate([Opponent](const FMT2DuelEntry& Entry) { return Entry.Opponent == Opponent; });
}

FMT2DuelEntry* UMT2DuelComponent::FindMutable(const AMT2PlayerState* Opponent)
{
	return Duels.FindByPredicate([Opponent](const FMT2DuelEntry& Entry) { return Entry.Opponent == Opponent; });
}

bool UMT2DuelComponent::IsFighting(const AMT2PlayerState* Opponent) const
{
	const AMT2PlayerState* Self = PlayerState();
	if (!IsValid(Self) || !IsValid(Opponent) || Self == Opponent || Self->GetWorld() != Opponent->GetWorld() ||
		(Self->GetParty() && Self->GetParty() == Opponent->GetParty())) { return false; }
	const FMT2DuelEntry* Entry = FindDuel(Opponent);
	return Entry && Entry->Phase == EMT2DuelPhase::Fighting;
}

bool UMT2DuelComponent::RequestDuel(AMT2PlayerState* Opponent, FString& Error)
{
	AMT2PlayerState* Self = PlayerState();
	AMT2PlayerCharacter* Pawn = Self ? Cast<AMT2PlayerCharacter>(Self->GetPawn()) : nullptr;
	AMT2PlayerCharacter* OtherPawn = IsValid(Opponent) ? Cast<AMT2PlayerCharacter>(Opponent->GetPawn()) : nullptr;
	if (!IsValid(Self) || !Self->HasAuthority() || !IsValid(Pawn) || !IsValid(OtherPawn) || Self == Opponent ||
		Opponent->IsActorBeingDestroyed() || Pawn->GetWorld() != OtherPawn->GetWorld() ||
		Pawn->IsActorBeingDestroyed() || OtherPawn->IsActorBeingDestroyed())
	{
		Error = TEXT("Duel request failed: invalid player."); return false;
	}
	if (Pawn->GetHealthComponent()->IsDead() || OtherPawn->GetHealthComponent()->IsDead()) { Error = TEXT("Both players must be alive to agree to a duel."); return false; }
	if (Self->GetParty() && Self->GetParty() == Opponent->GetParty())
	{
		Error = TEXT("You cannot duel a member of your party."); return false;
	}
	const UMT2GameplaySettings& Settings = UMT2GameplaySettings::Get();
	if (FVector::DistSquared(Pawn->GetActorLocation(), OtherPawn->GetActorLocation()) >
		FMath::Square(FMath::Max(Settings.DuelRequestRange, 1.f)))
	{
		Error = TEXT("That player is too far away to agree to a duel."); return false;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - LastRequestTime < FMath::Max(Settings.DuelRequestCooldownSeconds, 0.f))
	{
		Error = TEXT("Please wait before sending another duel request."); return false;
	}
	LastRequestTime = Now;
	CleanupExpiredDuels();
	UMT2DuelComponent* Other = Opponent->GetDuelComponent();
	FMT2DuelEntry* Entry = FindMutable(Opponent);
	if (Entry)
	{
		FMT2DuelEntry* Mirror = Other->FindMutable(Self);
		if (!Mirror || Mirror->Phase != Entry->Phase) { CancelDuel(Opponent); Error = TEXT("That duel is no longer valid."); return false; }
		if (Entry->Phase == EMT2DuelPhase::Fighting || !Entry->bCanAccept)
		{
			Error = TEXT("The duel is already active or waiting for the other player."); return false;
		}
		Entry->Phase = Mirror->Phase = EMT2DuelPhase::Fighting;
		Entry->bCanAccept = Mirror->bCanAccept = false;
		Entry->LastActivity = Mirror->LastActivity = Now;
		Changed(); Other->Changed();
		Notify(Self, FString::Printf(TEXT("Duel with %s started."), *Opponent->GetCharacterName()));
		Notify(Opponent, FString::Printf(TEXT("Duel with %s started."), *Self->GetCharacterName()));
		return true;
	}
	const int32 Limit = FMath::Clamp(Settings.MaximumDuelAgreements, 1, 128);
	if (Duels.Num() >= Limit || Other->Duels.Num() >= Limit)
	{
		Error = TEXT("Too many pending or active duel agreements."); return false;
	}
	FMT2DuelEntry Local; Local.Opponent = Opponent; Local.LastActivity = Now;
	FMT2DuelEntry Remote; Remote.Opponent = Self; Remote.LastActivity = Now; Remote.bCanAccept = true;
	Duels.Add(Local); Other->Duels.Add(Remote);
	Changed(); Other->Changed();
	Notify(Self, FString::Printf(TEXT("Duel challenge sent to %s."), *Opponent->GetCharacterName()));
	Notify(Opponent, FString::Printf(TEXT("%s challenged you. Select them and click Accept duel."), *Self->GetCharacterName()));
	return true;
}

void UMT2DuelComponent::RecordHit(AMT2PlayerState* Opponent)
{
	if (!GetOwner()->HasAuthority() || !IsFighting(Opponent)) { return; }
	FMT2DuelEntry* Entry = FindMutable(Opponent);
	FMT2DuelEntry* Mirror = Opponent->GetDuelComponent()->FindMutable(PlayerState());
	if (Entry && Mirror) { Entry->LastActivity = Mirror->LastActivity = GetWorld()->GetTimeSeconds(); }
}

bool UMT2DuelComponent::HandleDefeat(AMT2PlayerState* Killer)
{
	if (!GetOwner()->HasAuthority() || !IsValid(Killer)) { return false; }
	FMT2DuelEntry* Entry = FindMutable(Killer);
	FMT2DuelEntry* Mirror = Killer->GetDuelComponent()->FindMutable(PlayerState());
	if (!Entry || !Mirror) { return false; }
	const double Now = GetWorld()->GetTimeSeconds();
	if (Entry->Phase == EMT2DuelPhase::Revenge) { return Now - Entry->LastActivity <= 15.; }
	if (!IsFighting(Killer) || Mirror->Phase != EMT2DuelPhase::Fighting) { return false; }
	Entry->Phase = Mirror->Phase = EMT2DuelPhase::Revenge;
	Entry->bCanAccept = true; Mirror->bCanAccept = false;
	Entry->LastActivity = Mirror->LastActivity = Now;
	Changed(); Killer->GetDuelComponent()->Changed();
	Notify(PlayerState(), FString::Printf(TEXT("You lost the duel against %s. After respawning, select them and click Revenge."), *Killer->GetCharacterName()));
	Notify(Killer, FString::Printf(TEXT("You won the duel against %s."), *PlayerState()->GetCharacterName()));
	return true;
}

bool UMT2DuelComponent::CancelDuel(AMT2PlayerState* Opponent)
{
	if (!GetOwner()->HasAuthority()) { return false; }
	const bool bRemoved = Duels.RemoveAll([Opponent](const FMT2DuelEntry& Entry) { return Entry.Opponent == Opponent; }) > 0;
	if (IsValid(Opponent))
	{
		UMT2DuelComponent* Other = Opponent->GetDuelComponent();
		if (Other->Duels.RemoveAll([Self=PlayerState()](const FMT2DuelEntry& Entry) { return Entry.Opponent == Self; }) > 0)
		{
			Other->Changed();
		}
	}
	if (bRemoved) { Changed(); }
	return bRemoved;
}

void UMT2DuelComponent::CancelAllDuels()
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) { return; }
	const TArray<FMT2DuelEntry> Snapshot = Duels;
	for (const FMT2DuelEntry& Entry : Snapshot) { CancelDuel(Entry.Opponent); }
}

void UMT2DuelComponent::CleanupExpiredDuels()
{
	if (!GetOwner()->HasAuthority()) { return; }
	const double Now = GetWorld()->GetTimeSeconds();
	const double Timeout = FMath::Max(UMT2GameplaySettings::Get().DuelIdleTimeoutSeconds, 1.f);
	const TArray<FMT2DuelEntry> Snapshot = Duels;
	for (const FMT2DuelEntry& Entry : Snapshot)
	{
		if (!IsValid(Entry.Opponent) || Entry.Opponent->IsActorBeingDestroyed() ||
			Entry.Opponent->GetWorld() != GetWorld() || Now - Entry.LastActivity > Timeout)
		{
			CancelDuel(Entry.Opponent);
		}
	}
}

void UMT2DuelComponent::Changed()
{
	GetOwner()->ForceNetUpdate();
	if (Duels.IsEmpty()) { GetWorld()->GetTimerManager().ClearTimer(CleanupTimer); }
	else if (!GetWorld()->GetTimerManager().IsTimerActive(CleanupTimer))
	{
		GetWorld()->GetTimerManager().SetTimer(CleanupTimer, this, &UMT2DuelComponent::CleanupExpiredDuels, 1.f, true);
	}
	OnDuelsChanged.Broadcast();
}

void UMT2DuelComponent::OnRep_Duels() { OnDuelsChanged.Broadcast(); }

void UMT2DuelComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	CancelAllDuels();
	if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(CleanupTimer); }
	Super::EndPlay(Reason);
}
