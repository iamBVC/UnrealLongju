#include "Dungeons/MT2DevilTowerRoom.h"
#include "Player/MT2PlayerState.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogMT2DevilTower, Log, All);

AMT2DevilTowerRoom::AMT2DevilTowerRoom()
{
	EncounterSeconds = 0.f;
#if WITH_EDITOR
	SetIsSpatiallyLoaded(false);
#endif
}

bool AMT2DevilTowerRoom::JoinPlayer(AMT2PlayerState* Player)
{
	if (!Super::JoinPlayer(Player)) return false;
	if (GetPhase() == EMT2DungeonRoomPhase::Idle && !GetWorld()->GetTimerManager().TimerExists(StartTimer))
	{
		// Defer until teleport/possession and the whole group transfer finish. No room Tick.
		StartTimer = GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (GetPhase() == EMT2DungeonRoomPhase::Idle && GetParticipantCount() > 0 && !StartEncounter())
				UE_LOG(LogMT2DevilTower, Error, TEXT("Tower floor %d could not start its encounter."), Floor);
		}));
	}
	else if (GetPhase() == EMT2DungeonRoomPhase::Completed) QueueAdvance();
	return true;
}

void AMT2DevilTowerRoom::OnEncounterCompleted() { QueueAdvance(); }

void AMT2DevilTowerRoom::QueueAdvance()
{
	if (!HasAuthority() || !IsValid(NextRoom) || GetWorld()->GetTimerManager().TimerExists(AdvanceTimer)) return;
	if (!FMath::IsFinite(TransitionSeconds) || TransitionSeconds <= 0.f) return;
	const int32 Epoch = GetRunSerial();
	GetWorld()->GetTimerManager().SetTimer(AdvanceTimer, FTimerDelegate::CreateWeakLambda(this, [this, Epoch]()
	{
		if (GetRunSerial() != Epoch || GetPhase() != EMT2DungeonRoomPhase::Completed) return;
		const auto Snapshot = GetMembers();
		for (auto Member : Snapshot) if (Member.IsValid() && !AdvancePlayer(Member.Get()))
			UE_LOG(LogMT2DevilTower, Warning, TEXT("Tower floor %d could not transfer %s; player remains in the source room."), Floor, *Member->GetName());
	}), TransitionSeconds, false);
}

void AMT2DevilTowerRoom::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorld()->GetTimerManager().ClearTimer(StartTimer);
	GetWorld()->GetTimerManager().ClearTimer(AdvanceTimer);
	Super::EndPlay(Reason);
}
