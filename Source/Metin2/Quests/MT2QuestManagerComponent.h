/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Quests/MT2QuestExpression.h"
#include "Quests/MT2QuestTypes.h"
#include "MT2QuestManagerComponent.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMT2QuestRuntime, Log, All);

class UMT2Quest;
class UMT2QuestNode;
struct FMT2QuestTrigger;
class AMT2Mob;
class APlayerState;
class APawn;
class FJsonObject;

// The node list behind one pending select() choice (a UPROPERTY-safe wrapper so the branches stay
// GC-referenced while the dialog is open).
USTRUCT()
struct FMT2QuestChoiceBranch
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<TObjectPtr<UMT2QuestNode>> Nodes;

	// The quest this branch belongs to. An NPC chat menu gathers options from several quests at once,
	// and each branch has to run with its own quest as context or its flags resolve to the wrong scope.
	UPROPERTY()
	TObjectPtr<const UMT2Quest> Quest = nullptr;

	// Set when this option is a quest target being answered. Choosing it retires that marker, so the
	// overhead arrow and flashing map dot stop even if the script never calls target.delete itself.
	UPROPERTY()
	FName ResolvesTargetName;
};

// One quest's entry in the player's quest log (the old q.* journal API).
USTRUCT(BlueprintType)
struct METIN2_API FMT2QuestJournalEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Journal")
	FName QuestId;

	UPROPERTY(BlueprintReadOnly, Category = "Journal")
	FText Title;

	UPROPERTY(BlueprintReadOnly, Category = "Journal")
	FText Summary;

	// "x" of the old counter display; -1 when the quest shows no counter.
	UPROPERTY(BlueprintReadOnly, Category = "Journal")
	int32 Counter = -1;
};

// A map waypoint a quest asked for (old target.pos / target.vid).
USTRUCT(BlueprintType)
struct METIN2_API FMT2QuestTargetMarker
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Target")
	FName TargetName;

	UPROPERTY(BlueprintReadOnly, Category = "Target")
	FName QuestId;

	// World XY on the current map.
	UPROPERTY(BlueprintReadOnly, Category = "Target")
	FVector2D WorldPosition = FVector2D::ZeroVector;

	// Non-zero when the marker tracks an NPC/mob vnum instead of a fixed point.
	UPROPERTY(BlueprintReadOnly, Category = "Target")
	int32 Vnum = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Target")
	bool bTracksActor = false;
	UPROPERTY(BlueprintReadOnly, Category = "Target")
	TObjectPtr<AActor> TargetActor = nullptr;
	bool MatchesActor(const AActor* Actor) const;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMT2QuestJournalChangedSignature);

// Per-player quest brain, living on the PlayerState next to the dialog component. It collects every
// quest Blueprint under /Game/Quests (via the quest registry), fans game events out to the quests that
// listen for them, tracks this player's per-quest state and flags, and runs quest blocks with a
// resumable executor so a select() can suspend mid-script and continue when the answer arrives.
//
// Dialog transport (the client RPCs) stays in UMT2QuestComponent; this component decides *what* to say.
UCLASS(BlueprintType, ClassGroup = "MT2", meta = (BlueprintSpawnableComponent))
class METIN2_API UMT2QuestManagerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMT2QuestManagerComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// True when an active quest is pointing the player at this NPC/mob vnum. Drives the in-world quest
	// arrow and the flashing map marker; readable on the owning client because the markers replicate.
	UFUNCTION(BlueprintPure, Category = "Quest|Target")
	bool IsQuestTargetVnum(int32 Vnum) const;
	bool IsQuestTargetActor(const AActor* Actor) const;

	// Server: routes one game event to every quest whose current state listens for it. Vnum filters the
	// trigger (NPC clicked, mob killed, item used); TargetActor is the NPC/mob involved, when any.
	// Returns true when at least one quest handled it (so the NPC click can skip its default dialog).
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Quest")
	bool DispatchEvent(EMT2QuestEvent Event, int32 Vnum = 0, AActor* TargetActor = nullptr,
		int32 ItemSlot = -1);

	// Talking to an NPC: gathers every quest chat option available for this NPC right now and presents
	// them as a menu (the old game's chat list), or runs a click block directly when the quest uses one.
	// Returns false when no quest wants this NPC, so the caller can fall back to a plain greeting.
	bool DispatchNpcInteraction(int32 NpcVnum, AActor* Npc, bool bNpcHasShop = false);

	// Name-matched dispatch for events that identify themselves by name rather than a vnum
	// (Timer/ServerTimer, named target arrivals).
	bool DispatchNamedEvent(EMT2QuestEvent Event, FName EventName, AActor* TargetActor = nullptr);

	// Convenience for gameplay code that has an actor rather than the component: routes the event to
	// that actor's quest manager when it is a player. Safe to call with anything (including null).
	static bool DispatchEventForActor(
		AActor* PlayerActor, EMT2QuestEvent Event, int32 Vnum = 0, AActor* TargetActor = nullptr);

	// Runs every quest's `letter` block, which is how quests (re)declare their journal entry. Called on
	// world entry and whenever a quest's state changes.
	void RefreshQuestJournal();

	// Owning client -> server: the player clicked this quest in their log. Runs that quest's `button`
	// block (falling back to `info`), which is what opens the quest's description dialog in the old game.
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Quest")
	void ServerOpenQuestDialog(FName QuestId);

	// ---- Quest timers (old timer / server_timer / clear_server_timer) ----
	// Schedules a named timer owned by OwningQuest. Argument is readable in the handler through
	// get_server_timer_arg(). Re-using a name replaces the pending timer, as the old scripts expect.
	void StartQuestTimer(FName TimerName, float Seconds, bool bServerTimer, bool bLooping,
		const FMT2QuestValue& Argument, const UMT2Quest* OwningQuest);
	// An empty name clears every timer the quest owns.
	void ClearQuestTimer(FName TimerName, const UMT2Quest* OwningQuest);

	// Argument of the timer currently being handled (old get_server_timer_arg()).
	FMT2QuestValue GetActiveTimerArgument() const { return ActiveTimerArgument; }

	// ---- Quest flags (old pc.getqf / pc.setqf - by far the most used quest primitive) ----
	// Unqualified names are scoped to OwningQuest; names containing '.' are used verbatim, so one quest
	// can read another's flag.
	UFUNCTION(BlueprintPure, Category = "Quest|Flags")
	int32 GetQuestFlag(FName FlagName, const UMT2Quest* OwningQuest = nullptr) const;
	bool HasQuestFlag(FName FlagName, const UMT2Quest* OwningQuest = nullptr) const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Quest|Flags")
	void SetQuestFlag(FName FlagName, int32 Value, const UMT2Quest* OwningQuest = nullptr);

	// ---- Per-quest state (old set_state) ----
	UFUNCTION(BlueprintPure, Category = "Quest|State")
	FName GetQuestState(const UMT2Quest* Quest) const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Quest|State")
	void SetQuestState(const UMT2Quest* Quest, FName StateName);

	// ---- Dialog building, called by the Say/Select/Close nodes ----
	// say_title()/say(): accumulate into the page being built.
	void AppendSay(const FText& Title, const TArray<FText>& Lines);
	// select(): flush the accumulated page with these options and wait for the answer. Branches are the
	// node lists to run per option, in the same order.
	void PresentChoices(const TArray<FText>& Options, const TArray<FMT2QuestChoiceBranch>& Branches,
		FName ResultVariable = NAME_None, const FString& ResultExpression = FString(), bool bExpandLabels = true);

	// wait(): flush the page and pause until the player confirms, then continue the same block.
	void PresentWait();
	void PresentInput(FName ResultVariable, bool bNumericOnly);
	// Flush a pending page with a plain "Close" (end of a block that said something but never selected).
	void FlushPendingPage();
	void CloseDialog();
	bool TryLockQuestNpc(const FMT2QuestContext& Context);
	bool UnlockQuestNpc(const FMT2QuestContext& Context);
	bool PurgeQuestNpc(const FMT2QuestContext& Context);
	uint64 GetNpcLockCheckpoint() const { return NpcLockSequence; }
	void ReleaseNpcLocksAfter(uint64 Checkpoint);
	void NotifyQuestNpcRemoved(AMT2Mob* Npc);
	void CancelConversation();

	// Pushes a nested node list (If branches, Select branches) to run before the rest of the block.
	void PushFrame(const TArray<TObjectPtr<UMT2QuestNode>>& Nodes);
	void PushFunctionFrame(const TArray<TObjectPtr<UMT2QuestNode>>& Nodes, FName ResultVariable,
		bool bResultQuestScoped);
	bool BeginScriptFunction(const TArray<TObjectPtr<UMT2QuestNode>>& Nodes, FName ResultVariable,
		bool bResultQuestScoped, const TArray<FName>& Parameters, const TArray<FMT2QuestValue>& Arguments,
		const TArray<FName>& ResultVariables = {}, const TArray<bool>& ResultQuestScopes = {});
	bool ReturnFromFunction(const FMT2QuestValue& Value);
	bool ConsumeLoopIteration(const UMT2QuestNode* LoopNode, int32 MaximumIterations);
	bool IsExecutingLoop(const UMT2QuestNode* LoopNode) const;
	void PushLoopFrame(const TArray<TObjectPtr<UMT2QuestNode>>& Nodes, UMT2QuestNode* LoopNode);
	bool BreakLoop();
	bool PushIpairsFrame(const TArray<TObjectPtr<UMT2QuestNode>>& Nodes, const FMT2QuestValue& Table,
		FName IndexVariable, FName ValueVariable, int32 MaximumIterations);
	bool PushTableCallbackFrame(const TArray<TObjectPtr<UMT2QuestNode>>& Nodes, const FMT2QuestValue& Table,
		FName KeyVariable, FName ValueVariable, bool bSequence, const TArray<FName>& LocalVariables,
		FName ResultVariable, bool bResultQuestScoped, int32 MaximumIterations);

	// Script locals (the old scripts' `local x = ...`), scoped to one running quest block and cleared
	// when the next one starts. Read back by expressions.
	FMT2QuestValue GetScriptVariable(FName VariableName) const;
	void SetScriptVariable(FName VariableName, const FMT2QuestValue& Value);
	void SetQuestScriptVariable(FName VariableName, const FMT2QuestValue& Value);

	// ---- Quest log (old q.* / send_letter family) ----
	UFUNCTION(BlueprintPure, Category = "Quest|Journal")
	const TArray<FMT2QuestJournalEntry>& GetJournalEntries() const { return JournalEntries; }

	// Resolves the {name} / {=expr} tokens the importer writes into quest text. Dialog text is expanded
	// as it is sent; journal text has to be expanded here, when it is written, because the entry is
	// stored and replicated as plain text.
	FString ExpandQuestText(const FText& Text, const FMT2QuestContext& Context) const;

	// Creates the entry if missing and returns it, so a journal node can edit in place.
	FMT2QuestJournalEntry& FindOrAddJournalEntry(FName QuestId);
	void RemoveJournalEntry(FName QuestId);
	void NotifyJournalChanged();

	// ---- Quest target markers (old target.* family) ----
	UFUNCTION(BlueprintPure, Category = "Quest|Target")
	const TArray<FMT2QuestTargetMarker>& GetTargetMarkers() const { return TargetMarkers; }

	void SetTargetMarker(const FMT2QuestTargetMarker& Marker);
	void ClearTargetMarker(FName TargetName, FName QuestId);

	UPROPERTY(BlueprintAssignable, Category = "Quest|Journal")
	FMT2QuestJournalChangedSignature OnJournalChanged;

	// Persistence: quest states + flags travel with the character.
	void SaveTo(const TSharedRef<FJsonObject>& Root) const;
	void LoadFrom(const TSharedRef<FJsonObject>& Root);

private:
	friend class FMT2QuestIpairsTest;
	friend class FMT2QuestTablesTest;
	friend class FMT2QuestCallbacksTest;
	friend class FMT2QuestNpcLocksTest;
	friend class FMT2QuestNpcPurgeTest;
	friend class FMT2QuestEntitiesTest;
	friend class FMT2QuestFunctionsTest;
	friend class FMT2QuestLoweringTest;
	friend class FMT2QuestCompactReturnTest;
	friend class FMT2QuestStringImportTest;
	friend class FMT2QuestResultListImportTest;
	friend class FMT2QuestTargetDispatchTest;
	friend class FMT2QuestTriggerGateTest;
	TMap<TWeakObjectPtr<AMT2Mob>, uint64> LockedQuestNpcs;
	TWeakObjectPtr<AActor> NpcLockPawn;
	uint64 NpcLockSequence = 0;
	bool HasPendingConversation() const;
	void ReleaseNpcLocksIfIdle();
	UFUNCTION() void HandleNpcLockPawnDestroyed(AActor* Pawn);
	UFUNCTION() void HandleLockedNpcDestroyed(AActor* Npc);
	UFUNCTION() void HandleNpcLockPawnChanged(APlayerState* State, APawn* NewPawn, APawn* OldPawn);
	// Shared dispatch: EventName is set for name-matched events (timers), Vnum for the rest.
	bool DispatchEventInternal(
		EMT2QuestEvent Event, int32 Vnum, FName EventName, AActor* TargetActor, int32 ItemSlot = -1,
		FName QuestId = NAME_None);

	// Runs one specific quest's trigger for an event, rather than broadcasting to every quest.
	bool DispatchEventToQuest(const UMT2Quest* Quest, EMT2QuestEvent Event);

	// Runs a quest block to completion or to its first suspend.
	void RunPendingFrames();
	void PublishFunctionResults(FName ResultVariable, bool bQuestScoped, const TArray<FName>& ResultVariables,
		const TArray<bool>& ResultQuestScopes, const FMT2QuestValue& Value);
	bool PassesTrigger(const FMT2QuestTrigger& Trigger, const FMT2QuestContext& Context);
	bool RejectTriggerGateDialog();
	// Resolves a flag name to its stored key ("quest.flag").
	FName ResolveFlagKey(FName FlagName, const UMT2Quest* OwningQuest) const;
	void HandleChoiceAnswered(int32 ChoiceIndex);
	void HandleTextAnswered(const FString& Text);

	// One level of the executor's call stack: a node list and how far through it we are.
	struct FQuestFrame
	{
		TArray<TObjectPtr<UMT2QuestNode>> Nodes;
		int32 Index = 0;
		bool bFunctionFrame = false;
		TArray<FName> ResultVariables;
		TArray<bool> ResultQuestScopes;
		const UMT2QuestNode* LoopNode = nullptr;
		FName ResultVariable;
		bool bResultQuestScoped = false;
		FMT2QuestValue IteratorTable;
		int32 IteratorIndex = 0;
		int32 MaximumIterations = 0;
		bool bIteratorFailed = false;
		FName IteratorIndexVariable;
		FName IteratorValueVariable;
		TMap<FName, FMT2QuestValue> SavedIteratorLocals;
		TSet<FName> AbsentIteratorLocals;
		bool bCallbackLoop = false;
		bool bCallbackInvocation = false;
		bool bAwaitingCallback = false;
		bool bSequenceCallback = false;
		TArray<FMT2QuestValue> CallbackKeys;
		FMT2QuestValue CallbackReturn;
	};
	bool AdvanceIpairsFrame(FQuestFrame& Frame);
	bool AdvanceCallbackFrame(FQuestFrame& Frame);
	void RestoreIteratorLocals(const FQuestFrame& Frame);

	// Active block's call stack; non-empty means a quest is mid-run (possibly suspended on a dialog).
	TArray<FQuestFrame> CallStack;

	// Context of the running block, kept across a suspend so the resumed branch sees the same NPC.
	UPROPERTY(Transient)
	FMT2QuestContext ActiveContext;

	// Page being accumulated by Say nodes until a Select or the end of the block flushes it.
	FText PendingTitle;
	TArray<FText> PendingLines;

	// Choice branches of the Select currently awaiting an answer, in the order shown to the player.
	UPROPERTY(Transient)
	TArray<FMT2QuestChoiceBranch> PendingChoiceBranches;

	// Per-quest current state (quest name -> state name); absent means "start".
	UPROPERTY()
	TMap<FName, FName> QuestStates;

	// Flag store, keyed "quest.flag".
	UPROPERTY()
	TMap<FName, int32> QuestFlags;

	// Locals of the block currently running; not persisted (they die with the conversation).
	TMap<FName, FMT2QuestValue> ScriptVariables;
	// Bare Lua assignments are quest globals. Keep them per player and per quest, avoiding the old
	// server's unsafe cross-player Lua environment while preserving values between that quest's events.
	TMap<FName, FMT2QuestValue> QuestScriptVariables;
	TMap<TWeakObjectPtr<const UMT2QuestNode>, int32> LoopIterations;

	// Quest log and map markers. The journal persists with the character; markers are rebuilt by the
	// quests themselves (they are map-local), so they are not saved.
	// Replicated to the owning client so the quest window, the in-world arrow and the map markers can
	// read them directly instead of asking the server every frame.
	UPROPERTY(ReplicatedUsing = OnRep_QuestPresentation)
	TArray<FMT2QuestJournalEntry> JournalEntries;

	UPROPERTY(ReplicatedUsing = OnRep_QuestPresentation)
	TArray<FMT2QuestTargetMarker> TargetMarkers;

	UFUNCTION()
	void OnRep_QuestPresentation();

	// Guards against a quest block re-entering the dispatcher while it is already running.
	bool bRunning = false;
	bool bEvaluatingTriggerGate = false;
	bool bTriggerGateFailed = false;
	// Several quests can change state during one event. Their letter refresh is coalesced into one
	// next-tick dispatch so the journal is rebuilt once instead of once per changed quest.
	bool bJournalRefreshQueued = false;
	// A state transition fires that quest's Enter block on the next tick. Keeping quest ids rather
	// than callbacks prevents duplicate Enter execution when several nodes change the same state.
	TSet<FName> PendingStateEntryQuestIds;
	TSet<FName> PendingPIESpawnPreservingQuestIds;

	// True while suspended on a wait() page break: any answer resumes the block instead of being
	// treated as a dismissal.
	bool bAwaitingConfirm = false;

	// Script variable that receives the chosen index of the select currently awaiting an answer.
	FName PendingChoiceVariable;
	FString PendingChoiceResultExpression;
	FName PendingInputVariable;
	bool bPendingInputNumeric = false;

	// One scheduled quest timer.
	struct FQuestTimer
	{
		FName TimerName;
		FName QuestId;
		bool bServerTimer = false;
		bool bLooping = false;
		float Seconds = 0.0f;
		FMT2QuestValue Argument;
		FTimerHandle Handle;
	};
	TArray<FQuestTimer> QuestTimers;

	// Argument of the timer being handled right now, for get_server_timer_arg().
	FMT2QuestValue ActiveTimerArgument;

	void HandleQuestTimerFired(FName TimerName, FName QuestId);

	// Waits for the pawn to exist, then fires the world-entry events exactly once. Driven from
	// BeginPlay rather than the admission flow so PIE and a live map server behave identically.
	void TryFireEntryEvents();

	// Fires the Target/Arrive events when the player reaches a quest marker. Old scripts advance on
	// arrival (`when teacher1.target.arrive`), not on talking, so without this those quests never move.
	void CheckTargetArrivals();

	// Retries arrivals while markers remain, like the old target event timer.
	FTimerHandle TargetArrivalTimer;

	bool bEntryEventsFired = false;
	bool bPreservePIESpawnDuringDispatch = false;
	FTimerHandle EntryEventTimer;
};
