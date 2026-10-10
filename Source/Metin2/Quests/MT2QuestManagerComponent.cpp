/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Quests/MT2QuestManagerComponent.h"

#include "Characters/MT2PlayerCharacter.h"
#include "Dungeons/MT2DungeonSubsystem.h"
#include "Dungeons/MT2DevilTowerRoom.h"
#include "Items/MT2InventoryComponent.h"
#include "EngineUtils.h"
#include "Mobs/MT2Mob.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Persistence/MT2PersistenceComponent.h"
#include "Player/MT2PlayerState.h"
#include "Quests/MT2Quest.h"
#include "Quests/MT2QuestExpression.h"
#include "Quests/MT2QuestComponent.h"
#include "Quests/MT2QuestCondition.h"
#include "Quests/MT2QuestNode.h"
#include "Quests/MT2QuestRegistrySubsystem.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY(LogMT2QuestRuntime);

bool FMT2QuestTargetMarker::MatchesActor(const AActor* Actor) const
{
	if (!IsValid(Actor) || Actor->IsActorBeingDestroyed()) { return false; }
	if (bTracksActor) { return TargetActor == Actor; }
	const AMT2Mob* Mob = Cast<AMT2Mob>(Actor);
	return Mob && Vnum > 0 && Mob->GetMobVnum() == Vnum;
}

namespace
{
	const FName StartStateName(TEXT("start"));

	// Quest text carries two kinds of token, both written by the importer:
	//   {name}      - the player's name (the old say_pc_name())
	//   {=<expr>}   - a complete quest expression, evaluated once (including string.format calls).
	FString ExpandTokens(const FText& Text, const FMT2QuestContext& Context)
	{
		const FString Source = Text.ToString();
		FString Result;
		auto AppendPlainText = [&](FString PlainText)
		{
			if (PlainText.Contains(TEXT("{name}")))
			{
				const FString PlayerName = Context.PlayerState ? Context.PlayerState->GetCharacterName() : FString();
				PlainText = PlainText.Replace(TEXT("{name}"), *PlayerName, ESearchCase::IgnoreCase);
			}
			if (PlainText.Contains(TEXT("{npc}")))
			{
				const AMT2Mob* Mob = Cast<AMT2Mob>(Context.TargetActor);
				const FString NpcName = Mob ? Mob->GetMobDisplayName() : FString();
				PlainText = PlainText.Replace(TEXT("{npc}"), *NpcName, ESearchCase::IgnoreCase);
			}
			Result += PlainText;
		};

		// Evaluate every {=expr}. Scanned left to right so nested braces inside an expression are safe.
		int32 Position = 0;
		int32 Start = Source.Find(TEXT("{="));
		while (Start != INDEX_NONE)
		{
			int32 Depth = 1;
			TCHAR Quote = 0;
			int32 Cursor = Start + 2;
			for (; Cursor < Source.Len() && Depth > 0; ++Cursor)
			{
				const TCHAR Character = Source[Cursor];
				if (Quote)
				{
					if (Character == TEXT('\\') && Cursor + 1 < Source.Len()) { ++Cursor; }
					else if (Character == Quote) { Quote = 0; }
					continue;
				}
				if (Character == TEXT('"') || Character == TEXT('\'')) { Quote = Character; continue; }
				if (Character == TEXT('{')) { ++Depth; }
				else if (Character == TEXT('}')) { --Depth; }
			}
			if (Depth != 0)
			{
				break; // unbalanced; leave the rest of the text alone
			}
			const int32 End = Cursor - 1;
			AppendPlainText(Source.Mid(Position, Start - Position));
			const FString Expression = Source.Mid(Start + 2, End - Start - 2);

			bool bOk = true;
			const FMT2QuestValue Value = FMT2QuestExpression::Evaluate(Expression, Context, bOk);
			FString Replacement;
			if (bOk)
			{
				// Whole numbers print without a decimal tail, which is what the old %d formats expect.
				Replacement = Value.bIsNil ? TEXT("nil") : Value.bIsBoolean ? (Value.AsBool() ? TEXT("true") : TEXT("false")) : Value.bIsText
					? Value.Text
					: (FMath::IsNearlyEqual(Value.Number, FMath::RoundToDouble(Value.Number))
						? FString::FromInt(Value.AsInt()) : FString::SanitizeFloat(Value.Number));
			}
			else
			{
				UE_LOG(LogMT2QuestRuntime, Warning, TEXT("Quest text expression failed evaluation."));
			}
			Result += Replacement;
			// Returned/input text is data, not another expression to execute.
			Position = End + 1;
			Start = Source.Find(TEXT("{="), ESearchCase::CaseSensitive, ESearchDir::FromStart, Position);
		}
		AppendPlainText(Source.Mid(Position));
		return Result;
	}

	bool PassesAll(const TArray<TObjectPtr<UMT2QuestCondition>>& Conditions, const FMT2QuestContext& Context)
	{
		const uint64 LockCheckpoint = Context.Manager ? Context.Manager->GetNpcLockCheckpoint() : 0;
		for (const TObjectPtr<UMT2QuestCondition>& Condition : Conditions)
		{
			if (Condition && !Condition->Evaluate(Context))
			{
				if (Context.Manager) { Context.Manager->ReleaseNpcLocksAfter(LockCheckpoint); }
				return false;
			}
		}
		return true;
	}
}

UMT2QuestManagerComponent::UMT2QuestManagerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// Quest logic is server-authoritative, but the journal and target markers are replicated to the
	// owning client so the quest window and map markers can be driven locally.
	SetIsReplicatedByDefault(true);
}

void UMT2QuestManagerComponent::BeginPlay()
{
	Super::BeginPlay();
	// Server only: the entry events start quests and build the journal, which then replicates down.
	if (!GetOwner() || !GetOwner()->HasAuthority() || !GetWorld())
	{
		return;
	}
	// The pawn and the restored quest flags both arrive after this component does, so poll briefly
	// until the player is actually in the world.
	GetWorld()->GetTimerManager().SetTimer(
		EntryEventTimer, FTimerDelegate::CreateUObject(this, &UMT2QuestManagerComponent::TryFireEntryEvents),
		0.5f, true, 0.5f);

	// Quest markers are reached by walking to them, so the arrival check runs on its own poll.
	GetWorld()->GetTimerManager().SetTimer(
		TargetArrivalTimer,
		FTimerDelegate::CreateUObject(this, &UMT2QuestManagerComponent::CheckTargetArrivals),
		0.5f, true, 1.0f);
}

void UMT2QuestManagerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CancelConversation();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(EntryEventTimer);
		World->GetTimerManager().ClearTimer(TargetArrivalTimer);
		for (FQuestTimer& Timer : QuestTimers)
		{
			World->GetTimerManager().ClearTimer(Timer.Handle);
		}
	}
	QuestTimers.Reset();
	Super::EndPlay(EndPlayReason);
}

void UMT2QuestManagerComponent::TryFireEntryEvents()
{
	const AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner());
	if (bEntryEventsFired || !State || !State->GetPawn())
	{
		return;
	}
	bEntryEventsFired = true;
	UE_LOG(LogMT2QuestRuntime, Log, TEXT("[Quest] Player=%s firing Login and Enter events."),
		*State->GetCharacterName());
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(EntryEventTimer);
	}

	// Login/Enter start or advance quests for this session; Letter then has every quest declare the
	// journal entry for the state it is now in.
#if WITH_EDITOR
	TGuardValue<bool> PreserveSpawn(bPreservePIESpawnDuringDispatch,
		GetWorld() && GetWorld()->WorldType == EWorldType::PIE);
#endif
	DispatchEvent(EMT2QuestEvent::Login);
	DispatchEvent(EMT2QuestEvent::Enter);
	RefreshQuestJournal();
}

void UMT2QuestManagerComponent::CheckTargetArrivals()
{
	const AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner());
	const APawn* Pawn = State ? State->GetPawn() : nullptr;
	UWorld* World = GetWorld();
	if (!State || !State->HasAuthority() || !Pawn || !World || TargetMarkers.IsEmpty() ||
		bRunning || HasPendingConversation())
	{
		return;
	}
	// Roughly the old game's arrival radius - close enough to be standing with the NPC.
	constexpr double ArrivalRadius = 500.0;
	const FVector PlayerLocation = Pawn->GetActorLocation();

	// Copied: dispatching runs quest script that can add or remove markers mid-iteration.
	const TArray<FMT2QuestTargetMarker> Markers = TargetMarkers;
	for (const FMT2QuestTargetMarker& Marker : Markers)
	{
		if (!TargetMarkers.ContainsByPredicate([&Marker](const FMT2QuestTargetMarker& Current)
			{ return Current.TargetName == Marker.TargetName && Current.QuestId == Marker.QuestId; }))
		{
			continue; // an earlier arrival script may have removed another marker in this snapshot
		}
		if (Marker.bTracksActor && (!IsValid(Marker.TargetActor) || Marker.TargetActor->IsActorBeingDestroyed()))
		{
			DispatchEventInternal(EMT2QuestEvent::TargetDie, 0, Marker.TargetName, nullptr, -1, Marker.QuestId);
			ClearTargetMarker(Marker.TargetName, Marker.QuestId);
			if (HasPendingConversation()) { return; }
			continue;
		}
		if (Marker.TargetName.IsNone())
		{
			continue;
		}

		// Old vnum-only markers have no unique actor position; retain their click behavior until
		// reimport. VID targets can subscribe to arrival as well as click, as in the source.
		if (Marker.Vnum > 0 && !Marker.bTracksActor)
		{
			continue;
		}
		const FVector2D Position = Marker.bTracksActor
			? FVector2D(Marker.TargetActor->GetActorLocation()) : Marker.WorldPosition;
		const double DX = FMath::Abs(FMath::TruncToDouble(PlayerLocation.X) - FMath::TruncToDouble(Position.X));
		const double DY = FMath::Abs(FMath::TruncToDouble(PlayerLocation.Y) - FMath::TruncToDouble(Position.Y));
		const double Distance = FMath::FloorToDouble((246.0 * FMath::Max(DX, DY) + 102.0 * FMath::Min(DX, DY)) / 256.0);
		if (Distance <= ArrivalRadius)
		{
			// Retry while the marker remains: a failed gate must not consume the arrival.
			if (!DispatchEventInternal(EMT2QuestEvent::Arrive, 0, Marker.TargetName,
				Marker.TargetActor, -1, Marker.QuestId))
			{
				DispatchEventInternal(EMT2QuestEvent::Target, 0, Marker.TargetName,
					Marker.TargetActor, -1, Marker.QuestId); // pre-reimport assets
			}
			if (HasPendingConversation()) { return; }
		}
	}
}

void UMT2QuestManagerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// Another player's quest log is private, so these are owner-only.
	DOREPLIFETIME_CONDITION(UMT2QuestManagerComponent, JournalEntries, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UMT2QuestManagerComponent, TargetMarkers, COND_OwnerOnly);
}

void UMT2QuestManagerComponent::OnRep_QuestPresentation()
{
	OnJournalChanged.Broadcast();
}

bool UMT2QuestManagerComponent::IsQuestTargetActor(const AActor* Actor) const
{
	return TargetMarkers.ContainsByPredicate([Actor](const FMT2QuestTargetMarker& Marker) { return Marker.MatchesActor(Actor); });
}

bool UMT2QuestManagerComponent::IsQuestTargetVnum(int32 Vnum) const
{
	if (Vnum <= 0)
	{
		return false;
	}
	return TargetMarkers.ContainsByPredicate(
		[Vnum](const FMT2QuestTargetMarker& Marker) { return !Marker.bTracksActor && Marker.Vnum == Vnum; });
}

FName UMT2QuestManagerComponent::ResolveFlagKey(FName FlagName, const UMT2Quest* OwningQuest) const
{
	if (FlagName.IsNone())
	{
		return NAME_None;
	}
	// A dotted name is already fully qualified (one quest reading another's flag).
	FString Name = FlagName.ToString();
	if (Name.Contains(TEXT(".")))
	{
		return FlagName;
	}
	const FName QuestId = OwningQuest ? OwningQuest->GetQuestId() : NAME_None;
	if (QuestId.IsNone())
	{
		return FlagName;
	}
	return FName(*(QuestId.ToString() + TEXT(".") + Name));
}

int32 UMT2QuestManagerComponent::GetQuestFlag(FName FlagName, const UMT2Quest* OwningQuest) const
{
	const FName Key = ResolveFlagKey(FlagName, OwningQuest);
	const int32* Value = QuestFlags.Find(Key);
	return Value ? *Value : 0;
}

bool UMT2QuestManagerComponent::HasQuestFlag(FName FlagName, const UMT2Quest* OwningQuest) const
{
	return QuestFlags.Contains(ResolveFlagKey(FlagName, OwningQuest));
}

void UMT2QuestManagerComponent::SetQuestFlag(FName FlagName, int32 Value, const UMT2Quest* OwningQuest)
{
	const FName Key = ResolveFlagKey(FlagName, OwningQuest);
	if (Key.IsNone())
	{
		return;
	}
	const int32 PreviousValue = GetQuestFlag(FlagName, OwningQuest);
	if (PreviousValue == Value)
	{
		return;
	}
	if (Value == 0)
	{
		QuestFlags.Remove(Key); // 0 is the default, so don't persist it
	}
	else
	{
		QuestFlags.Add(Key, Value);
	}
	const AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner());
	UE_LOG(LogMT2QuestRuntime, Log, TEXT("[Quest] Player=%s Quest=%s Flag=%s changed %d -> %d."),
		State ? *State->GetCharacterName() : TEXT("Unknown"),
		OwningQuest ? *OwningQuest->GetQuestId().ToString() : TEXT("Global"),
		*Key.ToString(), PreviousValue, Value);
	if (State && State->GetPersistenceComponent())
	{
		State->GetPersistenceComponent()->MarkDirty();
	}
}

FName UMT2QuestManagerComponent::GetQuestState(const UMT2Quest* Quest) const
{
	if (!Quest)
	{
		return StartStateName;
	}
	const FName* State = QuestStates.Find(Quest->GetQuestId());
	if (!State || Quest->FindState(*State))
	{
		return State ? *State : StartStateName;
	}

	// Older runtime builds incorrectly translated q.done() into an undeclared __COMPLETE__ state.
	// Resolve any stale state that no longer exists in the quest definition to its real start state.
	return Quest->FindState(StartStateName) || Quest->States.IsEmpty()
		? StartStateName
		: Quest->States[0].StateName;
}

void UMT2QuestManagerComponent::SetQuestState(const UMT2Quest* Quest, FName StateName)
{
	if (!Quest || StateName.IsNone())
	{
		return;
	}
	// Only a real change is worth reacting to: a state's own letter block may call set_state again, and
	// refreshing on a no-op write would bounce between the two forever.
	const FName PreviousState = GetQuestState(Quest);
	QuestStates.Add(Quest->GetQuestId(), StateName);
	const int32 StateIndex = Quest->States.IndexOfByPredicate(
		[StateName](const FMT2QuestState& State) { return State.StateName == StateName; });
	if (StateIndex != INDEX_NONE)
	{
		SetQuestFlag(FName(*(Quest->GetQuestId().ToString() + TEXT(".__status"))), StateIndex, nullptr);
	}
	if (PreviousState == StateName)
	{
		return;
	}
	const AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner());
	UE_LOG(LogMT2QuestRuntime, Log, TEXT("[Quest] Player=%s Quest=%s state changed %s -> %s."),
		State ? *State->GetCharacterName() : TEXT("Unknown"),
		*Quest->GetQuestId().ToString(), *PreviousState.ToString(), *StateName.ToString());
	if (State && State->GetPersistenceComponent())
	{
		State->GetPersistenceComponent()->MarkDirty();
	}

	// Markers belong to the state that set them: drop them so a finished step stops showing an arrow
	// and a flashing dot. The new state's letter/target block re-declares whatever it still needs.
	ClearTargetMarker(NAME_None, Quest->GetQuestId());
	PendingStateEntryQuestIds.Add(Quest->GetQuestId());
	if (bPreservePIESpawnDuringDispatch || (bRunning && ActiveContext.bPreservePIESpawn))
	{
		PendingPIESpawnPreservingQuestIds.Add(Quest->GetQuestId());
	}

	// The old quest runtime enters the new state before refreshing its letter. Many scripts create
	// their journal entry and target from `when enter or login`, so skipping Enter makes those quests
	// silently disappear after set_state(). Defer both operations because we may still be mid-block.
	if (UWorld* World = GetWorld())
	{
		if (!bJournalRefreshQueued)
		{
			bJournalRefreshQueued = true;
			TWeakObjectPtr<UMT2QuestManagerComponent> WeakThis(this);
			World->GetTimerManager().SetTimerForNextTick([WeakThis]()
			{
				if (WeakThis.IsValid())
				{
					WeakThis->bJournalRefreshQueued = false;
					const TSet<FName> QuestIds = MoveTemp(WeakThis->PendingStateEntryQuestIds);
					WeakThis->PendingStateEntryQuestIds.Reset();
					const TSet<FName> PreserveSpawnQuestIds = MoveTemp(WeakThis->PendingPIESpawnPreservingQuestIds);
					WeakThis->PendingPIESpawnPreservingQuestIds.Reset();
					const AMT2PlayerState* PlayerState = Cast<AMT2PlayerState>(WeakThis->GetOwner());
					UGameInstance* GameInstance = PlayerState ? PlayerState->GetGameInstance() : nullptr;
					UMT2QuestRegistrySubsystem* Registry = GameInstance
						? GameInstance->GetSubsystem<UMT2QuestRegistrySubsystem>() : nullptr;
					if (Registry)
					{
						for (const FName QuestId : QuestIds)
						{
							if (const UMT2Quest* ChangedQuest = Registry->FindQuest(QuestId))
							{
								TGuardValue<bool> PreserveSpawn(WeakThis->bPreservePIESpawnDuringDispatch,
									PreserveSpawnQuestIds.Contains(QuestId));
								WeakThis->DispatchEventToQuest(ChangedQuest, EMT2QuestEvent::Enter);
							}
						}
					}
					TGuardValue<bool> PreserveJournalSpawn(WeakThis->bPreservePIESpawnDuringDispatch,
						!PreserveSpawnQuestIds.IsEmpty());
					WeakThis->RefreshQuestJournal();
				}
			});
		}
	}
}

bool UMT2QuestManagerComponent::DispatchEvent(
	EMT2QuestEvent Event, int32 Vnum, AActor* TargetActor, int32 ItemSlot)
{
	return DispatchEventInternal(Event, Vnum, NAME_None, TargetActor, ItemSlot);
}

bool UMT2QuestManagerComponent::DispatchNpcInteraction(int32 NpcVnum, AActor* Npc, bool bNpcHasShop)
{
	AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner());
	if (!State || !State->HasAuthority() || bRunning || HasPendingConversation() || NpcVnum <= 0)
	{
		return false;
	}
	UGameInstance* GameInstance = State->GetGameInstance();
	UMT2QuestRegistrySubsystem* Registry =
		GameInstance ? GameInstance->GetSubsystem<UMT2QuestRegistrySubsystem>() : nullptr;
	if (!Registry)
	{
		return false;
	}

	FMT2QuestContext Context;
	Context.Manager = this;
	Context.PlayerState = State;
	Context.Player = Cast<AMT2PlayerCharacter>(State->GetPawn());
	// Named target clicks pre-empt the ordinary NPC menu (legacy CQuestManager::Click).
	// Copy because execution can delete/reissue markers.
	for (const FMT2QuestTargetMarker& Marker : TArray<FMT2QuestTargetMarker>(TargetMarkers))
	{
		if (!Marker.MatchesActor(Npc)) { continue; }
		if (DispatchEventInternal(EMT2QuestEvent::TargetClick, NpcVnum, Marker.TargetName, Npc, -1, Marker.QuestId) ||
			DispatchEventInternal(EMT2QuestEvent::Target, NpcVnum, Marker.TargetName, Npc, -1, Marker.QuestId))
		{
			return true;
		}
		break; // source selects the first target attached to this entity
	}
	Context.TargetActor = Npc;
	Context.EventVnum = NpcVnum;

	// Everything this NPC can offer becomes one menu, so a shop never pre-empts a quest and several
	// quests on the same NPC can be chosen between. Options are gathered first, then presented.
	TArray<FText> Options;
	TArray<FMT2QuestChoiceBranch> Branches;

	if (bNpcHasShop)
	{
		Options.Add(NSLOCTEXT("MT2Quest", "OpenShop", "Open Shop"));
		FMT2QuestChoiceBranch& ShopBranch = Branches.AddDefaulted_GetRef();
		ShopBranch.Nodes.Add(NewObject<UMT2QuestNode_OpenShop>(this));
	}

	for (const TObjectPtr<const UMT2Quest>& Quest : Registry->GetQuests())
	{
		if (!Quest)
		{
			continue;
		}
		Context.Quest = Quest;
		const FMT2QuestState* QuestState = Quest->FindState(GetQuestState(Quest));
		if (!QuestState)
		{
			continue;
		}

		for (const FMT2QuestTrigger& Trigger : QuestState->Triggers)
		{
			if (Trigger.Nodes.IsEmpty())
			{
				continue;
			}
			Context.Event = Trigger.Event;

			// Target clicks are dispatched before this menu; arrivals never become click options.
			const bool bNpcTrigger = Trigger.Vnum == NpcVnum &&
				(Trigger.Event == EMT2QuestEvent::Chat || Trigger.Event == EMT2QuestEvent::Click);
			if (!bNpcTrigger || !PassesTrigger(Trigger, Context))
			{
				continue;
			}

			// Chat triggers carry their own menu label. Anything else is named after the quest it
			// continues, which only reads as a menu entry while that quest is in the log - a script
			// name like "npc_talk" is an identifier, never something to show the player, so an
			// ambient click block is offered as a plain conversation instead.
			FText Label = Trigger.ChatOption;
			if (Label.IsEmpty())
			{
				const FMT2QuestJournalEntry* Entry = JournalEntries.FindByPredicate(
					[&Quest](const FMT2QuestJournalEntry& Candidate)
					{ return Candidate.QuestId == Quest->GetQuestId(); });
				Label = (Entry && !Entry->Title.IsEmpty())
					? Entry->Title
					: NSLOCTEXT("MT2Quest", "TalkToNpc", "Talk");
			}
			Options.Add(Label);
			FMT2QuestChoiceBranch& Branch = Branches.AddDefaulted_GetRef();
			Branch.Nodes = Trigger.Nodes;
			Branch.Quest = Quest;
		}
	}

	if (Options.IsEmpty())
	{
		ReleaseNpcLocksAfter(0);
		return false;
	}

	auto BeginRun = [this, &Context](const UMT2Quest* Quest)
	{
		Context.Quest = Quest;
		ActiveContext = Context;
		CallStack.Reset();
		PendingChoiceBranches.Reset();
		PendingTitle = FText::GetEmpty();
		PendingLines.Reset();
		ScriptVariables.Reset();
		LoopIterations.Reset();
		if (!Quest)
		{
			return;
		}
		for (const FMT2QuestConstant& Constant : Quest->Constants)
		{
			if (Constant.Name.IsNone())
			{
				continue;
			}
			bool bConstantOk = true;
			const FMT2QuestValue Value =
				FMT2QuestExpression::Evaluate(Constant.Expression, ActiveContext, bConstantOk);
			if (bConstantOk)
			{
				ScriptVariables.Add(Constant.Name, Value);
			}
		}
	};

	// A single option needs no menu - a plain vendor should just open its shop on click.
	if (Options.Num() == 1)
	{
		BeginRun(Branches[0].Quest);
		if (!Branches[0].ResolvesTargetName.IsNone() && Branches[0].Quest)
		{
			ClearTargetMarker(Branches[0].ResolvesTargetName, Branches[0].Quest->GetQuestId());
		}
		PushFrame(Branches[0].Nodes);
		RunPendingFrames();
		return true;
	}

	// Always give the player a way out of the menu - clicking an NPC by accident should not force them
	// to open a shop or start a quest. Only needed when a menu is actually shown (the single-option
	// path above runs straight away and never traps anyone).
	Options.Add(NSLOCTEXT("MT2Quest", "CloseDialog", "Close"));
	FMT2QuestChoiceBranch& CloseBranch = Branches.AddDefaulted_GetRef();
	CloseBranch.Nodes.Add(NewObject<UMT2QuestNode_Close>(this));

	const AMT2Mob* NpcMob = Cast<AMT2Mob>(Npc);
	BeginRun(Branches[0].Quest);
	AppendSay(FText::FromString(NpcMob ? NpcMob->GetMobDisplayName() : FString()), {});
	PresentChoices(Options, Branches);
	return true;
}

bool UMT2QuestManagerComponent::DispatchNamedEvent(
	EMT2QuestEvent Event, FName EventName, AActor* TargetActor)
{
	return DispatchEventInternal(Event, 0, EventName, TargetActor);
}

bool UMT2QuestManagerComponent::DispatchEventInternal(
	EMT2QuestEvent Event, int32 Vnum, FName EventName, AActor* TargetActor, int32 ItemSlot, FName QuestId)
{
	AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner());
	if (!State || !State->HasAuthority() || bRunning)
	{
		// bRunning guards re-entry: a node must not start another quest block mid-run.
		return false;
	}
	if (HasPendingConversation())
	{
		if (Event == EMT2QuestEvent::Click || Event == EMT2QuestEvent::Chat || Event == EMT2QuestEvent::ItemTake ||
			Event == EMT2QuestEvent::TargetClick || Event == EMT2QuestEvent::Arrive ||
			Event == EMT2QuestEvent::TargetDie || Event == EMT2QuestEvent::Target)
		{
			return false;
		}
	}
	UGameInstance* GameInstance = State->GetGameInstance();
	UMT2QuestRegistrySubsystem* Registry =
		GameInstance ? GameInstance->GetSubsystem<UMT2QuestRegistrySubsystem>() : nullptr;
	if (!Registry)
	{
		return false;
	}

	FMT2QuestContext Context;
	Context.Manager = this;
	Context.PlayerState = State;
	Context.Player = Cast<AMT2PlayerCharacter>(State->GetPawn());
	Context.TargetActor = TargetActor;
	Context.Event = Event;
	Context.EventVnum = Vnum;
	Context.EventItemSlot = ItemSlot;
	Context.bPreservePIESpawn = bPreservePIESpawnDuringDispatch;
	const auto* Dungeon = State->GetWorld()->GetSubsystem<UMT2DungeonSubsystem>();
	const bool bNativeTower = Dungeon && Cast<AMT2DevilTowerRoom>(Dungeon->FindRoom(TEXT("DevilTower.Floor1")));
	if (bNativeTower && Context.Player && (Event == EMT2QuestEvent::Login || Event == EMT2QuestEvent::Enter))
		State->GetWorld()->GetSubsystem<UMT2DungeonSubsystem>()->ReconcilePlayer(State);
	if (HasPendingConversation())
	{
		// Broadcasts currently replace the suspended run. End it explicitly so its
		// callbacks and NPC leases cannot survive into the replacement execution.
		CancelConversation();
	}
	if (Event == EMT2QuestEvent::ItemTake)
	{
		// An item offer cannot replace an already suspended conversation.
		const UMT2InventoryComponent* Inventory = Context.Player ? Context.Player->GetInventoryComponent() : nullptr;
		if (!CallStack.IsEmpty() || !Inventory || !Inventory->GetSlots().IsValidIndex(ItemSlot) ||
			Inventory->GetSlots()[ItemSlot].IsEmpty()) { return false; }
		Context.EventItemSnapshot = Inventory->GetSlots()[ItemSlot];
		Context.bHasEventItemSnapshot = true;
	}

	// Interaction events belong to exactly one quest - the first that claims the NPC owns the
	// conversation. Everything else (login, enter, letter, kill, level-up, timers) is a broadcast: every
	// quest must get a chance, because each one registers its own journal entry and progress.
	const bool bExclusive = Event == EMT2QuestEvent::Click || Event == EMT2QuestEvent::Chat ||
		Event == EMT2QuestEvent::ItemTake || Event == EMT2QuestEvent::TargetClick;
	bool bHandledAny = false;

	for (const TObjectPtr<const UMT2Quest>& Quest : Registry->GetQuests())
	{
		if (!Quest || (!QuestId.IsNone() && Quest->GetQuestId() != QuestId))
		{
			continue;
		}
		// This map's shared rooms own progression. Do not run private-map timers or the old
		// login guard that would eject everybody above floor one. Entrance dialogue outside stays intact.
		if (bNativeTower && Quest->GetQuestId() == TEXT("deviltower_zone")) continue;
		Context.Quest = Quest;
		if (Event == EMT2QuestEvent::Enter)
		{
			// A broadcast Enter (initial login/map entry) satisfies a pending state Enter too. Consume it
			// before execution so a state change made by that Enter can queue the next state normally.
			PendingStateEntryQuestIds.Remove(Quest->GetQuestId());
			PendingPIESpawnPreservingQuestIds.Remove(Quest->GetQuestId());
		}

		// The Blueprint hook runs first and can swallow the event - this is where converted complex
		// scripts implement logic the declarative nodes can't express.
		if (const_cast<UMT2Quest*>(Quest.Get())->OnQuestEvent(Context))
		{
			return true;
		}

		const FMT2QuestState* QuestState = Quest->FindState(GetQuestState(Quest));
		if (!QuestState)
		{
			continue;
		}
		for (const FMT2QuestTrigger& Trigger : QuestState->Triggers)
		{
			if (Trigger.Event != Event || (!Trigger.TriggerName.IsNone() && Trigger.TriggerName != EventName))
			{
				continue;
			}
			// Vnum 0 on the trigger means "any"; otherwise it must match the event's vnum.
			if (Trigger.Vnum != 0 && Trigger.Vnum != Vnum)
			{
				continue;
			}
			// ...except for NPC interaction, where "any" is never what the author meant. The old scripts
			// often name the NPC instead of its vnum, which the importer cannot resolve; letting those
			// through would make one NPC's dialog fire on every NPC in the game. They stay inert until a
			// vnum is filled in on the trigger (the import report lists them).
			if ((Event == EMT2QuestEvent::Click || Event == EMT2QuestEvent::Chat || Event == EMT2QuestEvent::ItemTake) && Trigger.Vnum <= 0)
			{
				continue;
			}
			if (!PassesTrigger(Trigger, Context))
			{
				continue;
			}
			if (Trigger.Nodes.IsEmpty())
			{
				continue;
			}

			ActiveContext = Context;
			CallStack.Reset();
			PendingChoiceBranches.Reset();
			PendingTitle = FText::GetEmpty();
			PendingLines.Reset();
			ScriptVariables.Reset(); // locals belong to one conversation
			LoopIterations.Reset();
			// Seed the quest's scope constants (NPC vnums, item ids) so expressions can name them.
			for (const FMT2QuestConstant& Constant : Quest->Constants)
			{
				if (Constant.Name.IsNone())
				{
					continue;
				}
				bool bConstantOk = true;
				const FMT2QuestValue Value =
					FMT2QuestExpression::Evaluate(Constant.Expression, Context, bConstantOk);
				if (bConstantOk)
				{
					ScriptVariables.Add(Constant.Name, Value);
				}
			}
			PushFrame(Trigger.Nodes);
			UE_LOG(LogMT2QuestRuntime, Log,
				TEXT("[Quest] Player=%s Quest=%s State=%s handling Event=%s Vnum=%d."),
				*State->GetCharacterName(), *Quest->GetQuestId().ToString(),
				*QuestState->StateName.ToString(),
				*StaticEnum<EMT2QuestEvent>()->GetNameStringByValue(static_cast<int64>(Event)), Vnum);
			RunPendingFrames();
			bHandledAny = true;

			if (bExclusive)
			{
				return true;
			}
			// A broadcast block that opened a dialog owns the screen; stop before the next quest
			// clobbers the suspended conversation.
			if (!CallStack.IsEmpty())
			{
				return true;
			}
			break; // at most one trigger per quest per event, then on to the next quest
		}
	}
	ReleaseNpcLocksIfIdle();
	return bHandledAny;
}

bool UMT2QuestManagerComponent::DispatchEventForActor(
	AActor* PlayerActor, EMT2QuestEvent Event, int32 Vnum, AActor* TargetActor)
{
	const APawn* Pawn = Cast<APawn>(PlayerActor);
	AMT2PlayerState* State = Pawn
		? Pawn->GetPlayerState<AMT2PlayerState>() : Cast<AMT2PlayerState>(PlayerActor);
	UMT2QuestManagerComponent* QuestManager = State ? State->GetQuestManagerComponent() : nullptr;
	return QuestManager && QuestManager->DispatchEvent(Event, Vnum, TargetActor);
}

void UMT2QuestManagerComponent::ServerOpenQuestDialog_Implementation(FName QuestId)
{
	AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner());
	UGameInstance* GameInstance = State ? State->GetGameInstance() : nullptr;
	UMT2QuestRegistrySubsystem* Registry =
		GameInstance ? GameInstance->GetSubsystem<UMT2QuestRegistrySubsystem>() : nullptr;
	const UMT2Quest* Quest = Registry ? Registry->FindQuest(QuestId) : nullptr;
	if (!Quest)
	{
		return;
	}
	// Scoped to the clicked quest so one quest's log entry can't trigger another's dialog.
	// Old scripts put the description under `button`; some use `info` instead.
	if (!DispatchEventToQuest(Quest, EMT2QuestEvent::Button))
	{
		DispatchEventToQuest(Quest, EMT2QuestEvent::Info);
	}
}

bool UMT2QuestManagerComponent::DispatchEventToQuest(const UMT2Quest* Quest, EMT2QuestEvent Event)
{
	AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner());
	if (!Quest || !State || !State->HasAuthority() || bRunning || HasPendingConversation())
	{
		return false;
	}

	FMT2QuestContext Context;
	Context.Manager = this;
	Context.PlayerState = State;
	Context.Player = Cast<AMT2PlayerCharacter>(State->GetPawn());
	Context.Event = Event;
	Context.Quest = Quest;
	Context.bPreservePIESpawn = bPreservePIESpawnDuringDispatch;

	const FMT2QuestState* QuestState = Quest->FindState(GetQuestState(Quest));
	if (!QuestState)
	{
		return false;
	}
	for (const FMT2QuestTrigger& Trigger : QuestState->Triggers)
	{
		if (Trigger.Event != Event || Trigger.Nodes.IsEmpty() || !PassesTrigger(Trigger, Context))
		{
			continue;
		}
		ActiveContext = Context;
		CallStack.Reset();
		PendingChoiceBranches.Reset();
		PendingTitle = FText::GetEmpty();
		PendingLines.Reset();
		ScriptVariables.Reset();
		LoopIterations.Reset();
		for (const FMT2QuestConstant& Constant : Quest->Constants)
		{
			if (Constant.Name.IsNone())
			{
				continue;
			}
			bool bConstantOk = true;
			const FMT2QuestValue Value =
				FMT2QuestExpression::Evaluate(Constant.Expression, Context, bConstantOk);
			if (bConstantOk)
			{
				ScriptVariables.Add(Constant.Name, Value);
			}
		}
		PushFrame(Trigger.Nodes);
		UE_LOG(LogMT2QuestRuntime, Log, TEXT("[Quest] Player=%s Quest=%s State=%s handling Event=%s."),
			*State->GetCharacterName(), *Quest->GetQuestId().ToString(),
			*QuestState->StateName.ToString(),
			*StaticEnum<EMT2QuestEvent>()->GetNameStringByValue(static_cast<int64>(Event)));
		RunPendingFrames();
		return true;
	}
	return false;
}

void UMT2QuestManagerComponent::RefreshQuestJournal()
{
	// `letter` is the old system's "declare yourself" event: each quest's letter block calls
	// send_letter/q.start to (re)register its journal entry for the state it is now in.
	DispatchEvent(EMT2QuestEvent::Letter);
}

void UMT2QuestManagerComponent::StartQuestTimer(
	FName TimerName, float Seconds, bool bServerTimer, bool bLooping,
	const FMT2QuestValue& Argument, const UMT2Quest* OwningQuest)
{
	UWorld* World = GetWorld();
	if (!World || TimerName.IsNone() || Seconds <= 0.0f)
	{
		return;
	}
	const FName QuestId = OwningQuest ? OwningQuest->GetQuestId() : NAME_None;

	// Re-scheduling the same name replaces the pending timer (old scripts restart timers by name).
	ClearQuestTimer(TimerName, OwningQuest);

	FQuestTimer& Timer = QuestTimers.AddDefaulted_GetRef();
	Timer.TimerName = TimerName;
	Timer.QuestId = QuestId;
	Timer.bServerTimer = bServerTimer;
	Timer.bLooping = bLooping;
	Timer.Seconds = Seconds;
	Timer.Argument = Argument;

	TWeakObjectPtr<UMT2QuestManagerComponent> WeakThis(this);
	World->GetTimerManager().SetTimer(
		Timer.Handle,
		FTimerDelegate::CreateWeakLambda(this, [WeakThis, TimerName, QuestId]()
		{
			if (WeakThis.IsValid())
			{
				WeakThis->HandleQuestTimerFired(TimerName, QuestId);
			}
		}),
		Seconds, bLooping);
}

void UMT2QuestManagerComponent::ClearQuestTimer(FName TimerName, const UMT2Quest* OwningQuest)
{
	UWorld* World = GetWorld();
	const FName QuestId = OwningQuest ? OwningQuest->GetQuestId() : NAME_None;
	for (int32 Index = QuestTimers.Num() - 1; Index >= 0; --Index)
	{
		FQuestTimer& Timer = QuestTimers[Index];
		// An empty name clears every timer the quest owns.
		const bool bMatches = Timer.QuestId == QuestId &&
			(TimerName.IsNone() || Timer.TimerName == TimerName);
		if (!bMatches)
		{
			continue;
		}
		if (World)
		{
			World->GetTimerManager().ClearTimer(Timer.Handle);
		}
		QuestTimers.RemoveAt(Index);
	}
}

void UMT2QuestManagerComponent::HandleQuestTimerFired(FName TimerName, FName QuestId)
{
	// Capture the timer's argument so get_server_timer_arg() can read it inside the handler.
	bool bServerTimer = false;
	int32 FoundIndex = INDEX_NONE;
	for (int32 Index = 0; Index < QuestTimers.Num(); ++Index)
	{
		if (QuestTimers[Index].TimerName == TimerName && QuestTimers[Index].QuestId == QuestId)
		{
			FoundIndex = Index;
			break;
		}
	}
	if (FoundIndex == INDEX_NONE)
	{
		return;
	}
	ActiveTimerArgument = QuestTimers[FoundIndex].Argument;
	bServerTimer = QuestTimers[FoundIndex].bServerTimer;
	const bool bLooping = QuestTimers[FoundIndex].bLooping;

	// A one-shot is done once it fires; a looping timer stays scheduled until cleared.
	if (!bLooping)
	{
		QuestTimers.RemoveAt(FoundIndex);
	}

	DispatchNamedEvent(
		bServerTimer ? EMT2QuestEvent::ServerTimer : EMT2QuestEvent::Timer, TimerName);
	ActiveTimerArgument = FMT2QuestValue();
}

FMT2QuestValue UMT2QuestManagerComponent::GetScriptVariable(FName VariableName) const
{
	const FMT2QuestValue* Value = ScriptVariables.Find(VariableName);
	if (!Value && ActiveContext.Quest)
	{
		const FName ScopedName(*FString::Printf(TEXT("%s.%s"),
			*ActiveContext.Quest->GetQuestId().ToString(), *VariableName.ToString()));
		Value = QuestScriptVariables.Find(ScopedName);
	}
	return Value ? *Value : FMT2QuestValue();
}

void UMT2QuestManagerComponent::SetScriptVariable(FName VariableName, const FMT2QuestValue& Value)
{
	ScriptVariables.Add(VariableName, Value.Scalar());
}

void UMT2QuestManagerComponent::SetQuestScriptVariable(
	FName VariableName, const FMT2QuestValue& Value)
{
	if (!ActiveContext.Quest || VariableName.IsNone())
	{
		return;
	}
	const FName ScopedName(*FString::Printf(TEXT("%s.%s"),
		*ActiveContext.Quest->GetQuestId().ToString(), *VariableName.ToString()));
	QuestScriptVariables.Add(ScopedName, Value.Scalar());
}

FString UMT2QuestManagerComponent::ExpandQuestText(
	const FText& Text, const FMT2QuestContext& Context) const
{
	return ExpandTokens(Text, Context);
}

FMT2QuestJournalEntry& UMT2QuestManagerComponent::FindOrAddJournalEntry(FName QuestId)
{
	for (FMT2QuestJournalEntry& Entry : JournalEntries)
	{
		if (Entry.QuestId == QuestId)
		{
			return Entry;
		}
	}
	FMT2QuestJournalEntry& Entry = JournalEntries.AddDefaulted_GetRef();
	Entry.QuestId = QuestId;
	return Entry;
}

void UMT2QuestManagerComponent::RemoveJournalEntry(FName QuestId)
{
	if (JournalEntries.RemoveAll(
		[QuestId](const FMT2QuestJournalEntry& Entry) { return Entry.QuestId == QuestId; }) > 0)
	{
		NotifyJournalChanged();
	}
}

void UMT2QuestManagerComponent::NotifyJournalChanged()
{
	OnJournalChanged.Broadcast();
}

void UMT2QuestManagerComponent::SetTargetMarker(const FMT2QuestTargetMarker& Marker)
{
	// One marker per (quest, name): re-issuing it moves the existing waypoint.
	for (FMT2QuestTargetMarker& Existing : TargetMarkers)
	{
		if (Existing.TargetName == Marker.TargetName && Existing.QuestId == Marker.QuestId)
		{
			Existing = Marker;
			NotifyJournalChanged();
			return;
		}
	}
	TargetMarkers.Add(Marker);
	NotifyJournalChanged();
}

void UMT2QuestManagerComponent::ClearTargetMarker(FName TargetName, FName QuestId)
{
	// An empty name clears every marker the quest owns (the scripts' bulk cleanup).
	const int32 Removed = TargetMarkers.RemoveAll(
		[TargetName, QuestId](const FMT2QuestTargetMarker& Marker)
		{
			const bool bMatches = Marker.QuestId == QuestId &&
				(TargetName.IsNone() || Marker.TargetName == TargetName);
			return bMatches;
		});
	if (Removed > 0)
	{
		NotifyJournalChanged();
	}
}

void UMT2QuestManagerComponent::PushFrame(const TArray<TObjectPtr<UMT2QuestNode>>& Nodes)
{
	if (Nodes.IsEmpty())
	{
		return;
	}
	FQuestFrame& Frame = CallStack.AddDefaulted_GetRef();
	Frame.Nodes = Nodes;
	Frame.Index = 0;
}

void UMT2QuestManagerComponent::PushFunctionFrame(
	const TArray<TObjectPtr<UMT2QuestNode>>& Nodes, FName ResultVariable, bool bResultQuestScoped)
{
	if (Nodes.IsEmpty())
	{
		if (!ResultVariable.IsNone())
		{
			if (bResultQuestScoped) { SetQuestScriptVariable(ResultVariable, FMT2QuestValue()); }
			else { SetScriptVariable(ResultVariable, FMT2QuestValue()); }
		}
		return;
	}
	if (CallStack.Num() >= 64)
	{
		UE_LOG(LogMT2QuestRuntime, Error, TEXT("Quest function nesting exceeded 64 frames."));
		return;
	}
	FQuestFrame& Frame = CallStack.AddDefaulted_GetRef();
	Frame.Nodes = Nodes;
	Frame.bFunctionFrame = true;
	Frame.ResultVariable = ResultVariable;
	Frame.bResultQuestScoped = bResultQuestScoped;
}

bool UMT2QuestManagerComponent::BeginScriptFunction(
	const TArray<TObjectPtr<UMT2QuestNode>>& Nodes, FName ResultVariable, bool bResultQuestScoped,
	const TArray<FName>& Parameters, const TArray<FMT2QuestValue>& Arguments,
	const TArray<FName>& ResultVariables, const TArray<bool>& ResultQuestScopes)
{
	if (Parameters.Num() != Arguments.Num() || CallStack.Num() >= 64 || ResultVariables.Num() != ResultQuestScopes.Num() ||
		(!ResultVariables.IsEmpty() && !ResultVariable.IsNone()) || ResultVariables.Contains(NAME_None))
	{
		UE_LOG(LogMT2QuestRuntime, Warning, TEXT("Quest function frame rejected: invalid arguments or nesting limit."));
		return false;
	}
	if (Nodes.IsEmpty())
	{
		PublishFunctionResults(ResultVariable, bResultQuestScoped, ResultVariables, ResultQuestScopes, FMT2QuestValue::Results({}));
		return true;
	}
	PushFunctionFrame(Nodes, ResultVariable, bResultQuestScoped);
	FQuestFrame& Frame = CallStack.Last();
	Frame.ResultVariables = ResultVariables;
	Frame.ResultQuestScopes = ResultQuestScopes;
	for (int32 Index = 0; Index < Parameters.Num(); ++Index)
	{
		const FName Name = Parameters[Index];
		// The same unwind path restores iterator locals and function parameters, including
		// cancellation, runtime failure and early returns from nested branches.
		if (!Frame.SavedIteratorLocals.Contains(Name) && !Frame.AbsentIteratorLocals.Contains(Name))
		{
			if (const FMT2QuestValue* Previous = ScriptVariables.Find(Name)) { Frame.SavedIteratorLocals.Add(Name, *Previous); }
			else { Frame.AbsentIteratorLocals.Add(Name); }
		}
		SetScriptVariable(Name, Arguments[Index]);
	}
	return true;
}

void UMT2QuestManagerComponent::PublishFunctionResults(FName ResultVariable, bool bQuestScoped,
	const TArray<FName>& ResultVariables, const TArray<bool>& ResultQuestScopes, const FMT2QuestValue& Value)
{
	if (!ResultVariable.IsNone())
	{
		if (bQuestScoped) { SetQuestScriptVariable(ResultVariable, Value.ToRuntimeTable()); }
		else { SetScriptVariable(ResultVariable, Value.ToRuntimeTable()); }
	}
	for (int32 Index = 0; Index < ResultVariables.Num(); ++Index)
	{
		const FMT2QuestValue Result = Value.CallResults
			? (Value.CallResults->IsValidIndex(Index) ? (*Value.CallResults)[Index] : FMT2QuestValue())
			: (Index == 0 ? Value : FMT2QuestValue());
		if (ResultQuestScopes[Index]) { SetQuestScriptVariable(ResultVariables[Index], Result.ToRuntimeTable()); }
		else { SetScriptVariable(ResultVariables[Index], Result.ToRuntimeTable()); }
	}
}

bool UMT2QuestManagerComponent::ReturnFromFunction(const FMT2QuestValue& Value)
{
	for (int32 Index = CallStack.Num() - 1; Index >= 0; --Index)
	{
		if (!CallStack[Index].bFunctionFrame) { continue; }
		const FName ResultVariable = CallStack[Index].ResultVariable;
		const bool bResultQuestScoped = CallStack[Index].bResultQuestScoped;
		const TArray<FName> ResultVariables = CallStack[Index].ResultVariables;
		const TArray<bool> ResultQuestScopes = CallStack[Index].ResultQuestScopes;
		const bool bCallbackInvocation = CallStack[Index].bCallbackInvocation;
		for (int32 Unwind = CallStack.Num() - 1; Unwind >= Index; --Unwind)
		{
			if (CallStack[Unwind].LoopNode) { LoopIterations.Remove(CallStack[Unwind].LoopNode); }
			RestoreIteratorLocals(CallStack[Unwind]);
		}
		CallStack.SetNum(Index);
		if (bCallbackInvocation && !CallStack.IsEmpty() && CallStack.Last().bCallbackLoop)
		{
			CallStack.Last().CallbackReturn = Value;
			return true;
		}
		PublishFunctionResults(ResultVariable, bResultQuestScoped, ResultVariables, ResultQuestScopes, Value);
		return true;
	}
	return false;
}

bool UMT2QuestManagerComponent::IsExecutingLoop(const UMT2QuestNode* LoopNode) const
{
	return !CallStack.IsEmpty() && CallStack.Last().LoopNode == LoopNode;
}

void UMT2QuestManagerComponent::PushLoopFrame(
	const TArray<TObjectPtr<UMT2QuestNode>>& Nodes, UMT2QuestNode* LoopNode)
{
	// The tail loop node repeats in the same frame, avoiding one allocation/frame per iteration.
	if (IsExecutingLoop(LoopNode))
	{
		CallStack.Last().Index = 0;
		return;
	}
	PushFrame(Nodes);
	CallStack.Last().LoopNode = LoopNode;
}

bool UMT2QuestManagerComponent::BreakLoop()
{
	for (int32 Index = CallStack.Num() - 1; Index >= 0; --Index)
	{
		const FQuestFrame& Frame = CallStack[Index];
		if (Frame.bFunctionFrame) { break; } // Lua break cannot escape a called function.
		if (!Frame.LoopNode && !Frame.IteratorTable.IsTableRef()) { continue; }
		if (Frame.LoopNode) { LoopIterations.Remove(Frame.LoopNode); }
		for (int32 Unwind = CallStack.Num() - 1; Unwind >= Index; --Unwind)
		{
			RestoreIteratorLocals(CallStack[Unwind]);
		}
		CallStack.SetNum(Index);
		return true;
	}
	UE_LOG(LogMT2QuestRuntime, Error, TEXT("Quest break executed outside a loop."));
	return false;
}

bool UMT2QuestManagerComponent::ConsumeLoopIteration(
	const UMT2QuestNode* LoopNode, int32 MaximumIterations)
{
	if (!LoopNode)
	{
		return false;
	}
	int32& Count = LoopIterations.FindOrAdd(LoopNode);
	++Count;
	if (Count <= FMath::Max(MaximumIterations, 1))
	{
		return true;
	}
	UE_LOG(LogMT2QuestRuntime, Error, TEXT("Quest '%s' exceeded %d iterations in loop node '%s'."),
		ActiveContext.Quest ? *ActiveContext.Quest->GetQuestId().ToString() : TEXT("None"),
		MaximumIterations, *LoopNode->GetName());
	return false;
}

bool UMT2QuestManagerComponent::PushIpairsFrame(
	const TArray<TObjectPtr<UMT2QuestNode>>& Nodes, const FMT2QuestValue& Table,
	FName IndexVariable, FName ValueVariable, int32 MaximumIterations)
{
	if (!Table.IsTableRef() || IndexVariable.IsNone() || IndexVariable == ValueVariable || CallStack.Num() >= 64)
	{
		UE_LOG(LogMT2QuestRuntime, Error, TEXT("Invalid ipairs table/bindings or excessive quest nesting."));
		return false;
	}
	FQuestFrame& Frame = CallStack.AddDefaulted_GetRef();
	Frame.Nodes = Nodes;
	Frame.IteratorTable = Table;
	if (!Table.RuntimeTable)
	{
		Frame.IteratorTable.OwnedTable = MakeShared<FMT2QuestTable>(*Table.Table);
		Frame.IteratorTable.Table = Frame.IteratorTable.OwnedTable.Get();
	}
	Frame.IteratorIndexVariable = IndexVariable;
	Frame.IteratorValueVariable = ValueVariable;
	Frame.MaximumIterations = FMath::Max(MaximumIterations, 1);
	for (FName Name : {IndexVariable, ValueVariable})
	{
		if (Name.IsNone()) { continue; }
		if (const FMT2QuestValue* Previous = ScriptVariables.Find(Name))
		{
			Frame.SavedIteratorLocals.Add(Name, *Previous);
		}
		else { Frame.AbsentIteratorLocals.Add(Name); }
	}
	return true;
}

bool UMT2QuestManagerComponent::AdvanceIpairsFrame(FQuestFrame& Frame)
{
	const int32 Next = Frame.IteratorIndex + 1;
	const FMT2QuestValue Child = Frame.IteratorTable.GetTableValue(FMT2QuestValue(Next));
	// ipairs stops at the first absent integer key, not at the total number of table entries.
	if (Child.bIsNil) { return false; }
	if (Next > Frame.MaximumIterations)
	{
		UE_LOG(LogMT2QuestRuntime, Error, TEXT("Quest ipairs exceeded %d iterations."), Frame.MaximumIterations);
		Frame.bIteratorFailed = true;
		return false;
	}
	Frame.IteratorIndex = Next;
	Frame.Index = 0;
	SetScriptVariable(Frame.IteratorIndexVariable, FMT2QuestValue(Next));
	if (!Frame.IteratorValueVariable.IsNone())
	{
		SetScriptVariable(Frame.IteratorValueVariable, Child);
	}
	return true;
}

bool UMT2QuestManagerComponent::PushTableCallbackFrame(
	const TArray<TObjectPtr<UMT2QuestNode>>& Nodes, const FMT2QuestValue& Table,
	FName KeyVariable, FName ValueVariable, bool bSequence, const TArray<FName>& LocalVariables,
	FName ResultVariable, bool bResultQuestScoped, int32 MaximumIterations)
{
	if (!Table.IsTableRef() || KeyVariable.IsNone() || KeyVariable == ValueVariable || CallStack.Num() >= 63)
	{
		UE_LOG(LogMT2QuestRuntime, Error, TEXT("Invalid table callback or excessive quest nesting."));
		return false;
	}
	FQuestFrame& Frame = CallStack.AddDefaulted_GetRef();
	Frame.Nodes = Nodes;
	Frame.IteratorTable = Table;
	if (!Table.RuntimeTable)
	{
		Frame.IteratorTable.OwnedTable = MakeShared<FMT2QuestTable>(*Table.Table);
		Frame.IteratorTable.Table = Frame.IteratorTable.OwnedTable.Get();
	}
	Frame.bCallbackLoop = true;
	Frame.bSequenceCallback = bSequence;
	Frame.IteratorIndexVariable = KeyVariable;
	Frame.IteratorValueVariable = ValueVariable;
	Frame.ResultVariable = ResultVariable;
	Frame.bResultQuestScoped = bResultQuestScoped;
	Frame.MaximumIterations = FMath::Max(MaximumIterations, 1);
	if (bSequence)
	{
		const int32 Length = Table.GetTableLength();
		for (int32 Key = 1; Key <= Length; ++Key) { Frame.CallbackKeys.Add(FMT2QuestValue(Key)); }
	}
	else { Frame.CallbackKeys = Table.GetTableKeys(); }
	TArray<FName> Names = LocalVariables;
	Names.Add(KeyVariable);
	Names.Add(ValueVariable);
	for (FName Name : Names)
	{
		if (Name.IsNone()) { continue; }
		if (const FMT2QuestValue* Previous = ScriptVariables.Find(Name)) { Frame.SavedIteratorLocals.Add(Name, *Previous); }
		else { Frame.AbsentIteratorLocals.Add(Name); }
	}
	return true;
}

bool UMT2QuestManagerComponent::AdvanceCallbackFrame(FQuestFrame& Frame)
{
	if (Frame.bAwaitingCallback)
	{
		Frame.bAwaitingCallback = false;
		if (!Frame.CallbackReturn.bIsNil) { return false; }
		RestoreIteratorLocals(Frame);
	}
	while (Frame.IteratorIndex < Frame.CallbackKeys.Num())
	{
		const FMT2QuestValue Key = Frame.CallbackKeys[Frame.IteratorIndex++];
		const FMT2QuestValue Value = Frame.IteratorTable.GetTableValue(Key);
		if (!Frame.bSequenceCallback && Value.bIsNil) { continue; }
		if (Frame.IteratorIndex > Frame.MaximumIterations || CallStack.Num() >= 64)
		{
			UE_LOG(LogMT2QuestRuntime, Error, TEXT("Quest table callback exceeded its iteration/nesting limit."));
			Frame.bIteratorFailed = true;
			return false;
		}
		SetScriptVariable(Frame.IteratorIndexVariable, Key);
		if (!Frame.IteratorValueVariable.IsNone()) { SetScriptVariable(Frame.IteratorValueVariable, Value); }
		Frame.CallbackReturn = FMT2QuestValue();
		Frame.bAwaitingCallback = true;
		const TArray<TObjectPtr<UMT2QuestNode>> Body = Frame.Nodes;
		const int32 Depth = CallStack.Num();
		PushFunctionFrame(Body, NAME_None, false);
		if (CallStack.Num() > Depth) { CallStack.Last().bCallbackInvocation = true; }
		return true;
	}
	return false;
}

void UMT2QuestManagerComponent::RestoreIteratorLocals(const FQuestFrame& Frame)
{
	for (const auto& Pair : Frame.SavedIteratorLocals) { ScriptVariables.Add(Pair.Key, Pair.Value); }
	for (FName Name : Frame.AbsentIteratorLocals) { ScriptVariables.Remove(Name); }
}

bool UMT2QuestManagerComponent::PassesTrigger(const FMT2QuestTrigger& Trigger, const FMT2QuestContext& Context)
{
	if (bRunning || HasPendingConversation() || !Context.PlayerState || !Context.PlayerState->HasAuthority()) { return false; }
	const uint64 LockCheckpoint = GetNpcLockCheckpoint();
	TGuardValue<bool> RunGuard(bRunning, true);
	TGuardValue<bool> GateGuard(bEvaluatingTriggerGate, true);
	TGuardValue<bool> FailedGuard(bTriggerGateFailed, false);
	TGuardValue<FMT2QuestContext> ContextGuard(ActiveContext, Context);
	auto SavedVariables = MoveTemp(ScriptVariables);
	auto SavedLoops = MoveTemp(LoopIterations);
	TGuardValue<FText> TitleGuard(PendingTitle, FText::GetEmpty());
	auto SavedLines = MoveTemp(PendingLines);
	bool bPassed = true;
	if (Context.Quest)
	{
		for (const FMT2QuestConstant& Constant : Context.Quest->Constants)
		{
			if (Constant.Name.IsNone()) { continue; }
			bool bOk = true;
			const FMT2QuestValue Value = FMT2QuestExpression::Evaluate(Constant.Expression, Context, bOk);
			if (!bOk)
			{
				UE_LOG(LogMT2QuestRuntime, Error, TEXT("Trigger gate cannot evaluate constant '%s': %s"),
					*Constant.Name.ToString(), *Constant.Expression);
				bPassed = false;
				break;
			}
			ScriptVariables.Add(Constant.Name, Value);
		}
	}
	if (bPassed)
	{
		// Includes disabled recovered/unsupported triggers. Do not execute their helper prelude.
		bPassed = PassesAll(Trigger.Conditions, Context);
	}
	if (bPassed && !Trigger.GatePrelude.IsEmpty())
	{
		PushFrame(Trigger.GatePrelude);
		RunPendingFrames();
		bPassed = !bTriggerGateFailed && CallStack.IsEmpty();
	}
	if (bPassed)
	{
		bPassed = PassesAll(Trigger.GateConditions, Context) && !bTriggerGateFailed;
	}
	// Gates run on the Lua main state, not a resumable conversation. Failed execution cannot leave
	// stack frames, presentation state, iterator locals or newly acquired NPC leases behind.
	for (int32 Index = CallStack.Num() - 1; Index >= 0; --Index) { RestoreIteratorLocals(CallStack[Index]); }
	CallStack.Reset();
	ScriptVariables = MoveTemp(SavedVariables);
	LoopIterations = MoveTemp(SavedLoops);
	PendingLines = MoveTemp(SavedLines);
	if (!bPassed) { ReleaseNpcLocksAfter(LockCheckpoint); }
	return bPassed;
}

bool UMT2QuestManagerComponent::RejectTriggerGateDialog()
{
	if (!bEvaluatingTriggerGate) { return false; }
	bTriggerGateFailed = true;
	UE_LOG(LogMT2QuestRuntime, Error, TEXT("Quest trigger gate attempted dialogue/suspension; gates must execute synchronously."));
	return true;
}

void UMT2QuestManagerComponent::RunPendingFrames()
{
	// Walks the call stack until it empties (block finished) or a node suspends on the player.
	// Re-entrancy guard: nodes push frames rather than calling back into the executor.
	TGuardValue<bool> RunGuard(bRunning, true);

	while (!CallStack.IsEmpty())
	{
		FQuestFrame& Frame = CallStack.Last();
		if (Frame.bCallbackLoop)
		{
			if (AdvanceCallbackFrame(Frame)) { continue; }
			if (Frame.bIteratorFailed)
			{
				if (bEvaluatingTriggerGate) { bTriggerGateFailed = true; }
				for (int32 Unwind = CallStack.Num() - 1; Unwind >= 0; --Unwind) { RestoreIteratorLocals(CallStack[Unwind]); }
				CallStack.Reset();
				break;
			}
			const FName ResultVariable = Frame.ResultVariable;
			const bool bResultQuestScoped = Frame.bResultQuestScoped;
			const FMT2QuestValue Value = Frame.CallbackReturn;
			RestoreIteratorLocals(Frame);
			CallStack.Pop();
			if (!ResultVariable.IsNone())
			{
				if (bResultQuestScoped) { SetQuestScriptVariable(ResultVariable, Value); }
				else { SetScriptVariable(ResultVariable, Value); }
			}
			continue;
		}
		if (Frame.IteratorTable.IsTableRef() && (Frame.IteratorIndex == 0 || !Frame.Nodes.IsValidIndex(Frame.Index)))
		{
			if (AdvanceIpairsFrame(Frame)) { continue; }
			if (Frame.bIteratorFailed)
			{
				if (bEvaluatingTriggerGate) { bTriggerGateFailed = true; }
				for (int32 Unwind = CallStack.Num() - 1; Unwind >= 0; --Unwind)
				{
					RestoreIteratorLocals(CallStack[Unwind]);
				}
				CallStack.Reset();
				break;
			}
			RestoreIteratorLocals(Frame);
			CallStack.Pop();
			continue;
		}
		if (!Frame.Nodes.IsValidIndex(Frame.Index))
		{
			const bool bCompletedFunction = Frame.bFunctionFrame;
			const TArray<FName> ResultVariables = Frame.ResultVariables;
			const TArray<bool> ResultQuestScopes = Frame.ResultQuestScopes;
			const FName ResultVariable = Frame.ResultVariable;
			const bool bResultQuestScoped = Frame.bResultQuestScoped;
			const bool bCallbackInvocation = Frame.bCallbackInvocation;
			if (Frame.LoopNode) { LoopIterations.Remove(Frame.LoopNode); }
			RestoreIteratorLocals(Frame);
			CallStack.Pop();
			if (bCallbackInvocation && !CallStack.IsEmpty() && CallStack.Last().bCallbackLoop)
			{
				CallStack.Last().CallbackReturn = FMT2QuestValue();
			}
			if (bCompletedFunction)
			{
				PublishFunctionResults(ResultVariable, bResultQuestScoped, ResultVariables, ResultQuestScopes, FMT2QuestValue::Results({}));
			}
			continue;
		}
		UMT2QuestNode* Node = Frame.Nodes[Frame.Index++].Get();
		if (!Node || !Node->PassesConditions(ActiveContext))
		{
			continue;
		}

		const EMT2QuestNodeResult Result = Node->Execute(ActiveContext);
		if (Result == EMT2QuestNodeResult::Suspend)
		{
			if (bEvaluatingTriggerGate) { bTriggerGateFailed = true; }
			return; // waiting on a dialog answer; HandleChoiceAnswered resumes us
		}
		if (Result == EMT2QuestNodeResult::Stop)
		{
			if (bEvaluatingTriggerGate) { bTriggerGateFailed = true; }
			for (int32 Unwind = CallStack.Num() - 1; Unwind >= 0; --Unwind)
			{
				RestoreIteratorLocals(CallStack[Unwind]);
			}
			CallStack.Reset();
			break;
		}
	}

	// Block finished: show anything the script said but never selected on (old end-of-block behaviour).
	LoopIterations.Reset();
	if (bEvaluatingTriggerGate) { return; }
	FlushPendingPage();
	ReleaseNpcLocksIfIdle();
}

void UMT2QuestManagerComponent::AppendSay(const FText& Title, const TArray<FText>& Lines)
{
	if (RejectTriggerGateDialog()) { return; }
	if (!Title.IsEmpty())
	{
		PendingTitle = FText::FromString(ExpandTokens(Title, ActiveContext));
	}
	// Lua evaluates say() arguments at the statement, not when the page eventually flushes.
	// Each loop iteration must retain its values after iterator locals unwind.
	for (const FText& Line : Lines)
	{
		const FString Expanded = ExpandTokens(Line, ActiveContext);
		if (Expanded.IsEmpty()) { PendingLines.Add(FText::GetEmpty()); continue; }
		TArray<FString> Parts;
		Expanded.ParseIntoArray(Parts, TEXT("[ENTER]"), false);
		for (const FString& Part : Parts) { PendingLines.Add(FText::FromString(Part.TrimEnd())); }
	}
}

void UMT2QuestManagerComponent::PresentChoices(
	const TArray<FText>& Options, const TArray<FMT2QuestChoiceBranch>& Branches, FName ResultVariable,
	const FString& ResultExpression, bool bExpandLabels)
{
	if (RejectTriggerGateDialog()) { return; }
	const AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner());
	UMT2QuestComponent* Dialog = State ? State->GetQuestComponent() : nullptr;
	if (!Dialog)
	{
		return;
	}
	PendingChoiceBranches = Branches;
	PendingChoiceVariable = ResultVariable;
	PendingChoiceResultExpression = ResultExpression;

	FMT2DialogPayload Payload;
	Payload.Title = PendingTitle.ToString();
	for (const FText& Line : PendingLines)
	{
		Payload.TextLines.Add(Line.ToString());
	}
	for (const FText& Option : Options)
	{
		Payload.Options.Add(bExpandLabels ? ExpandTokens(Option, ActiveContext) : Option.ToString());
	}
	PendingLines.Reset();

	TWeakObjectPtr<UMT2QuestManagerComponent> WeakThis(this);
	Dialog->ShowDialog(Payload, [WeakThis](int32 Choice)
	{
		if (WeakThis.IsValid())
		{
			WeakThis->HandleChoiceAnswered(Choice);
		}
	});
}

void UMT2QuestManagerComponent::PresentWait()
{
	if (RejectTriggerGateDialog()) { return; }
	const AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner());
	UMT2QuestComponent* Dialog = State ? State->GetQuestComponent() : nullptr;
	if (!Dialog)
	{
		return;
	}
	bAwaitingConfirm = true;

	FMT2DialogPayload Payload;
	Payload.Title = PendingTitle.ToString();
	for (const FText& Line : PendingLines)
	{
		Payload.TextLines.Add(Line.ToString());
	}
	PendingLines.Reset(); // the page has been shown; the next say() starts a fresh one

	TWeakObjectPtr<UMT2QuestManagerComponent> WeakThis(this);
	Dialog->ShowDialog(Payload, [WeakThis](int32 Choice)
	{
		if (WeakThis.IsValid())
		{
			WeakThis->HandleChoiceAnswered(Choice);
		}
	});
}

void UMT2QuestManagerComponent::PresentInput(FName ResultVariable, bool bNumericOnly)
{
	if (RejectTriggerGateDialog()) { return; }
	const AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner());
	UMT2QuestComponent* Dialog = State ? State->GetQuestComponent() : nullptr;
	if (!Dialog || ResultVariable.IsNone())
	{
		return;
	}
	PendingInputVariable = ResultVariable;
	bPendingInputNumeric = bNumericOnly;

	FMT2DialogPayload Payload;
	Payload.Title = PendingTitle.ToString();
	for (const FText& Line : PendingLines)
	{
		Payload.TextLines.Add(Line.ToString());
	}
	Payload.bRequestsTextInput = true;
	Payload.bNumericInput = bNumericOnly;
	PendingLines.Reset();

	TWeakObjectPtr<UMT2QuestManagerComponent> WeakThis(this);
	Dialog->ShowInputDialog(Payload, [WeakThis](const FString& Text)
	{
		if (WeakThis.IsValid())
		{
			WeakThis->HandleTextAnswered(Text);
		}
	});
}

void UMT2QuestManagerComponent::HandleTextAnswered(const FString& Text)
{
	if (PendingInputVariable.IsNone())
	{
		return;
	}
	if (bPendingInputNumeric)
	{
		SetScriptVariable(PendingInputVariable, FMT2QuestValue(FCString::Atod(*Text)));
	}
	else
	{
		SetScriptVariable(PendingInputVariable, FMT2QuestValue(Text));
	}
	PendingInputVariable = NAME_None;
	bPendingInputNumeric = false;
	RunPendingFrames();
}

void UMT2QuestManagerComponent::HandleChoiceAnswered(int32 ChoiceIndex)
{
	if (ChoiceIndex <= 0)
	{
		CancelConversation();
		return;
	}
	// A wait() page break: any answer (including a plain OK) just continues the block.
	if (bAwaitingConfirm)
	{
		bAwaitingConfirm = false;
		RunPendingFrames();
		return;
	}

	// Variable mode: publish the chosen index and let the following if-chain dispatch on it.
	if (!PendingChoiceVariable.IsNone())
	{
		FMT2QuestValue Result(static_cast<double>(ChoiceIndex));
		if (!PendingChoiceResultExpression.IsEmpty())
		{
			static const FName SelectionVariable(TEXT("__mt2_selected_index"));
			ScriptVariables.Add(SelectionVariable, Result);
			bool bExpressionOk = true;
			const FMT2QuestValue Evaluated = FMT2QuestExpression::Evaluate(
				PendingChoiceResultExpression, ActiveContext, bExpressionOk);
			ScriptVariables.Remove(SelectionVariable);
			if (bExpressionOk)
			{
				Result = Evaluated;
			}
		}
		SetScriptVariable(PendingChoiceVariable, Result);
		PendingChoiceVariable = NAME_None;
		PendingChoiceResultExpression.Reset();
		PendingChoiceBranches.Reset();
		RunPendingFrames();
		return;
	}

	// ChoiceIndex is 1-based like the old select(); 0 means the player dismissed the window.
	const int32 BranchIndex = ChoiceIndex - 1;
	if (PendingChoiceBranches.IsValidIndex(BranchIndex))
	{
		// A chat menu can mix options from several quests, so adopt the chosen branch's quest before
		// running it - otherwise its flags would be written into the previous quest's scope.
		const FMT2QuestChoiceBranch& Chosen = PendingChoiceBranches[BranchIndex];
		if (Chosen.Quest)
		{
			ActiveContext.Quest = Chosen.Quest;
		}
		// A target that has now been answered stops pointing at its NPC.
		if (!Chosen.ResolvesTargetName.IsNone() && Chosen.Quest)
		{
			ClearTargetMarker(Chosen.ResolvesTargetName, Chosen.Quest->GetQuestId());
		}
		PushFrame(Chosen.Nodes);
	}
	else
	{
		// Dismissed: abandon the rest of the conversation rather than falling through to later nodes.
		for (int32 Unwind = CallStack.Num() - 1; Unwind >= 0; --Unwind)
		{
			RestoreIteratorLocals(CallStack[Unwind]);
		}
		CallStack.Reset();
	}
	PendingChoiceBranches.Reset();
	RunPendingFrames();
}

void UMT2QuestManagerComponent::FlushPendingPage()
{
	if (PendingLines.IsEmpty() && PendingTitle.IsEmpty())
	{
		return;
	}
	const AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner());
	UMT2QuestComponent* Dialog = State ? State->GetQuestComponent() : nullptr;
	if (Dialog)
	{
		FMT2DialogPayload Payload;
		Payload.Title = PendingTitle.ToString();
		for (const FText& Line : PendingLines)
		{
			Payload.TextLines.Add(Line.ToString());
		}
		// No options: the client shows a plain "Close".
		TWeakObjectPtr<UMT2QuestManagerComponent> WeakThis(this);
		Dialog->ShowDialog(Payload, [WeakThis](int32)
		{
			if (WeakThis.IsValid()) { WeakThis->ReleaseNpcLocksIfIdle(); }
		});
	}
	PendingTitle = FText::GetEmpty();
	PendingLines.Reset();
}

void UMT2QuestManagerComponent::CloseDialog()
{
	if (RejectTriggerGateDialog()) { return; }
	ReleaseNpcLocksAfter(0);
	PendingTitle = FText::GetEmpty();
	PendingLines.Reset();
	const AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner());
	if (UMT2QuestComponent* Dialog = State ? State->GetQuestComponent() : nullptr)
	{
		Dialog->CloseDialog();
	}
}

bool UMT2QuestManagerComponent::HasPendingConversation() const
{
	const AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner());
	const UMT2QuestComponent* Dialog = State ? State->GetQuestComponent() : nullptr;
	return !CallStack.IsEmpty() || !PendingChoiceBranches.IsEmpty() || !PendingChoiceVariable.IsNone() ||
		!PendingInputVariable.IsNone() || bAwaitingConfirm || (Dialog && Dialog->HasPendingDialog());
}

bool UMT2QuestManagerComponent::TryLockQuestNpc(const FMT2QuestContext& Context)
{
	check(IsInGameThread());
	AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner());
	if (!IsValid(State) || !State->HasAuthority() || Context.Manager != this || Context.PlayerState != State ||
		!IsValid(Context.Player) || Context.Player != State->GetPawn() || !Context.Player->HasAuthority() ||
		Context.Player->GetPlayerState() != State) { return false; }
	if (Context.TargetActor && !IsValid(Context.TargetActor)) { return false; }
	if (!Context.TargetActor || Cast<AMT2PlayerCharacter>(Context.TargetActor)) { return true; }
	AMT2Mob* Npc = Cast<AMT2Mob>(Context.TargetActor);
	if (!Npc || !Npc->TryAcquireQuestConversation(this, Context.Player)) { return false; }
	Npc->OnDestroyed.AddUniqueDynamic(this, &UMT2QuestManagerComponent::HandleLockedNpcDestroyed);
	if (!LockedQuestNpcs.Contains(Npc)) { LockedQuestNpcs.Add(Npc, ++NpcLockSequence); }
	NpcLockPawn = Context.Player;
	Context.Player->OnDestroyed.AddUniqueDynamic(this, &UMT2QuestManagerComponent::HandleNpcLockPawnDestroyed);
	State->OnPawnSet.AddUniqueDynamic(this, &UMT2QuestManagerComponent::HandleNpcLockPawnChanged);
	return true;
}

bool UMT2QuestManagerComponent::UnlockQuestNpc(const FMT2QuestContext& Context)
{
	check(IsInGameThread());
	const AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner());
	if (!IsValid(State) || !State->HasAuthority() || Context.Manager != this || Context.PlayerState != State) { return false; }
	if (AMT2Mob* Npc = Cast<AMT2Mob>(Context.TargetActor))
	{
		Npc->ReleaseQuestConversation(this);
		Npc->OnDestroyed.RemoveDynamic(this, &UMT2QuestManagerComponent::HandleLockedNpcDestroyed);
		LockedQuestNpcs.Remove(Npc);
	}
	// Also detach lifecycle callbacks when the last explicit unlock emptied the lease set.
	ReleaseNpcLocksAfter(NpcLockSequence);
	return true;
}

bool UMT2QuestManagerComponent::PurgeQuestNpc(const FMT2QuestContext& Context)
{
	check(IsInGameThread());
	AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner());
	if (!IsValid(State) || !State->HasAuthority() || Context.Manager != this || Context.PlayerState != State ||
		!IsValid(Context.Player) || Context.Player != State->GetPawn() || !Context.Player->HasAuthority() ||
		Context.Player->GetPlayerState() != State) { return false; }
	AActor* Target = Context.TargetActor;
	if (!Target) { return true; }
	AMT2Mob* Npc = Cast<AMT2Mob>(Target);
	if (!IsValid(Npc) || !Npc->HasAuthority() || Npc->GetWorld() != Context.Player->GetWorld()) { return false; }

	// Legacy npc.purge clears the current quest NPC before destruction. Otherwise our destruction
	// callback would cancel the initiating script and discard statements following the purge.
	const bool bCurrentTarget = ActiveContext.TargetActor == Npc;
	if (!UnlockQuestNpc(Context)) { return false; }
	if (bCurrentTarget) { ActiveContext.TargetActor = nullptr; }
	if (!Npc->Destroy())
	{
		if (bCurrentTarget) { ActiveContext.TargetActor = Npc; }
		return false;
	}
	return true;
}

void UMT2QuestManagerComponent::ReleaseNpcLocksAfter(uint64 Checkpoint)
{
	check(IsInGameThread());
	for (auto It = LockedQuestNpcs.CreateIterator(); It; ++It)
	{
		if (It.Value() > Checkpoint)
		{
			if (AMT2Mob* Npc = It.Key().Get())
			{
				Npc->ReleaseQuestConversation(this);
				Npc->OnDestroyed.RemoveDynamic(this, &UMT2QuestManagerComponent::HandleLockedNpcDestroyed);
			}
			It.RemoveCurrent();
		}
	}
	if (LockedQuestNpcs.IsEmpty())
	{
		if (AActor* Pawn = NpcLockPawn.Get()) { Pawn->OnDestroyed.RemoveDynamic(this, &UMT2QuestManagerComponent::HandleNpcLockPawnDestroyed); }
		if (AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner()))
		{
			State->OnPawnSet.RemoveDynamic(this, &UMT2QuestManagerComponent::HandleNpcLockPawnChanged);
		}
		NpcLockPawn.Reset();
	}
}

void UMT2QuestManagerComponent::ReleaseNpcLocksIfIdle()
{
	if (!HasPendingConversation()) { ReleaseNpcLocksAfter(0); }
}

void UMT2QuestManagerComponent::CancelConversation()
{
	for (int32 Unwind = CallStack.Num() - 1; Unwind >= 0; --Unwind) { RestoreIteratorLocals(CallStack[Unwind]); }
	CallStack.Reset();
	LoopIterations.Reset();
	PendingChoiceBranches.Reset();
	PendingChoiceVariable = NAME_None;
	PendingChoiceResultExpression.Reset();
	PendingInputVariable = NAME_None;
	bPendingInputNumeric = false;
	bAwaitingConfirm = false;
	CloseDialog();
}

void UMT2QuestManagerComponent::HandleNpcLockPawnDestroyed(AActor* Pawn)
{
	CancelConversation();
}

void UMT2QuestManagerComponent::HandleLockedNpcDestroyed(AActor* Npc)
{
	NotifyQuestNpcRemoved(Cast<AMT2Mob>(Npc));
}

void UMT2QuestManagerComponent::HandleNpcLockPawnChanged(APlayerState* State, APawn* NewPawn, APawn* OldPawn)
{
	if (!LockedQuestNpcs.IsEmpty() && NewPawn != NpcLockPawn.Get()) { CancelConversation(); }
}

void UMT2QuestManagerComponent::NotifyQuestNpcRemoved(AMT2Mob* Npc)
{
	if (ActiveContext.TargetActor == Npc) { CancelConversation(); }
	else { LockedQuestNpcs.Remove(Npc); ReleaseNpcLocksAfter(NpcLockSequence); }
}

void UMT2QuestManagerComponent::SaveTo(const TSharedRef<FJsonObject>& Root) const
{
	TSharedRef<FJsonObject> StatesObject = MakeShared<FJsonObject>();
	for (const TPair<FName, FName>& Pair : QuestStates)
	{
		StatesObject->SetStringField(Pair.Key.ToString(), Pair.Value.ToString());
	}
	Root->SetObjectField(TEXT("quest_states"), StatesObject);

	TSharedRef<FJsonObject> FlagsObject = MakeShared<FJsonObject>();
	for (const TPair<FName, int32>& Pair : QuestFlags)
	{
		FlagsObject->SetNumberField(Pair.Key.ToString(), Pair.Value);
	}
	Root->SetObjectField(TEXT("quest_flags"), FlagsObject);

	// The quest log travels with the character (map markers do not - quests re-issue those on arrival).
	TArray<TSharedPtr<FJsonValue>> JournalValues;
	for (const FMT2QuestJournalEntry& Entry : JournalEntries)
	{
		TSharedRef<FJsonObject> EntryObject = MakeShared<FJsonObject>();
		EntryObject->SetStringField(TEXT("quest"), Entry.QuestId.ToString());
		EntryObject->SetStringField(TEXT("title"), Entry.Title.ToString());
		EntryObject->SetStringField(TEXT("summary"), Entry.Summary.ToString());
		EntryObject->SetNumberField(TEXT("counter"), Entry.Counter);
		JournalValues.Add(MakeShared<FJsonValueObject>(EntryObject));
	}
	Root->SetArrayField(TEXT("quest_journal"), JournalValues);
}

void UMT2QuestManagerComponent::LoadFrom(const TSharedRef<FJsonObject>& Root)
{
	QuestStates.Reset();
	QuestFlags.Reset();

	const TSharedPtr<FJsonObject>* StatesObject = nullptr;
	if (Root->TryGetObjectField(TEXT("quest_states"), StatesObject) && StatesObject && StatesObject->IsValid())
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*StatesObject)->Values)
		{
			if (Pair.Value.IsValid())
			{
				QuestStates.Add(FName(*Pair.Key), FName(*Pair.Value->AsString()));
			}
		}
	}

	const TSharedPtr<FJsonObject>* FlagsObject = nullptr;
	if (Root->TryGetObjectField(TEXT("quest_flags"), FlagsObject) && FlagsObject && FlagsObject->IsValid())
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*FlagsObject)->Values)
		{
			if (Pair.Value.IsValid())
			{
				QuestFlags.Add(FName(*Pair.Key), FMath::RoundToInt(Pair.Value->AsNumber()));
			}
		}
	}

	JournalEntries.Reset();
	TargetMarkers.Reset();
	const TArray<TSharedPtr<FJsonValue>>* JournalValues = nullptr;
	if (Root->TryGetArrayField(TEXT("quest_journal"), JournalValues) && JournalValues)
	{
		for (const TSharedPtr<FJsonValue>& Value : *JournalValues)
		{
			const TSharedPtr<FJsonObject> EntryObject = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!EntryObject.IsValid())
			{
				continue;
			}
			FMT2QuestJournalEntry& Entry = JournalEntries.AddDefaulted_GetRef();
			Entry.QuestId = FName(*EntryObject->GetStringField(TEXT("quest")));
			Entry.Title = FText::FromString(EntryObject->GetStringField(TEXT("title")));
			Entry.Summary = FText::FromString(EntryObject->GetStringField(TEXT("summary")));
			double Counter = -1.0;
			EntryObject->TryGetNumberField(TEXT("counter"), Counter);
			Entry.Counter = FMath::RoundToInt(Counter);
		}
	}
	NotifyJournalChanged();
}
