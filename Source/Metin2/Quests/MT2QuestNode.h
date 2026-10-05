/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Player/MT2PlayerTypes.h"
#include "Quests/MT2QuestTableAsset.h"
#include "Quests/MT2QuestTypes.h"
#include "UObject/Object.h"
#include "MT2QuestNode.generated.h"

class UMT2QuestCondition;

// One step of a quest block. Subclasses are EditInlineNew + DefaultToInstanced so a quest Blueprint
// authors a whole conversation as a list in the details panel ("+ Say", "+ Select", "+ Give Item"),
// and Blueprintable so new node types can be added without C++.
//
// Execution is resumable: a node returns Suspend when it needs the player (a select), and the manager
// resumes the block from the same position when the answer arrives - this is what replaces the old
// Lua coroutine's suspend/resume at select()/input().
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, Blueprintable, CollapseCategories)
class METIN2_API UMT2QuestNode : public UObject
{
	GENERATED_BODY()

public:
	// Native execution. Context is rebuilt per run; never cache it.
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context);

	// Override in a node Blueprint for custom behaviour (runs when the native class doesn't handle it).
	UFUNCTION(BlueprintNativeEvent, Category = "Quest|Node")
	void Run(const FMT2QuestContext& Context);
	virtual void Run_Implementation(const FMT2QuestContext& Context) {}

	// Optional gate: when set, the node is skipped unless every condition passes.
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Node")
	TArray<TObjectPtr<UMT2QuestCondition>> Conditions;

	// True when Conditions are all satisfied (or there are none).
	bool PassesConditions(const FMT2QuestContext& Context) const;
};

// say_title() + say(): appends to the page being built. The page is flushed to the client by the next
// Select node, or automatically when the block ends - matching the old scripts, where say() accumulates
// text and select()/end-of-block presents it.
UCLASS(DisplayName = "Say")
class METIN2_API UMT2QuestNode_Say : public UMT2QuestNode
{
	GENERATED_BODY()

public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;

	// say_title(); leave empty to keep the current title (usually the NPC name).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Say")
	FText Title;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Say", meta = (MultiLine = "true"))
	TArray<FText> Lines;
};

// One select() choice: a label and the nodes that run when it is picked.
USTRUCT(BlueprintType)
struct METIN2_API FMT2QuestChoice
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Choice")
	FText Label;

	// Only offered when these pass (old scripts hide options behind level/item checks).
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Choice")
	TArray<TObjectPtr<UMT2QuestCondition>> Conditions;

	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Choice")
	TArray<TObjectPtr<UMT2QuestNode>> Nodes;
};

// select(): flushes the accumulated page with its options and suspends until the player answers, then
// runs the chosen branch.
UCLASS(DisplayName = "Select")
class METIN2_API UMT2QuestNode_Select : public UMT2QuestNode
{
	GENERATED_BODY()

public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Select")
	TArray<FMT2QuestChoice> Choices;

	bool BuildTableOptions(const FMT2QuestContext& Context, TArray<FText>& OutOptions) const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Select")
	FString TableExpression;

	// Optional: stores the chosen 1-based index into this script variable, mirroring the old
	// `local s = select(...)`. The converted `if s == 2 then ...` chain then reads it as an expression.
	// When set, the per-choice Nodes are ignored - the dispatch lives in the following if-chain instead.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Select")
	FName ResultVariable;

	// Optional expression applied to the selected 1-based index. The importer uses this for Lua such
	// as `choice = select(...) + 6`, preserving the value consumed by the following branch.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Select")
	FString ResultExpression;
};

// Branch on conditions.
UCLASS(DisplayName = "If")
class METIN2_API UMT2QuestNode_If : public UMT2QuestNode
{
	GENERATED_BODY()

public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;

	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "If")
	TArray<TObjectPtr<UMT2QuestCondition>> Test;

	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "If")
	TArray<TObjectPtr<UMT2QuestNode>> Then;

	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "If")
	TArray<TObjectPtr<UMT2QuestNode>> Else;
};

// Runtime loop used by converted while/repeat/numeric-for blocks. The importer lowers numeric for
// into SetVariable + While + increment, keeping one execution model for every loop form.
UCLASS(DisplayName = "While")
class METIN2_API UMT2QuestNode_While : public UMT2QuestNode
{
	GENERATED_BODY()

public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loop")
	FString ConditionExpression;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loop")
	bool bExecuteBodyFirst = false;

	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Loop")
	TArray<TObjectPtr<UMT2QuestNode>> Body;

	// Protects imported content from hanging the server when a malformed loop never terminates.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loop", meta = (ClampMin = "1"))
	int32 MaximumIterations = 10000;
};

UCLASS(DisplayName = "NPC Conversation Lock")
class METIN2_API UMT2QuestNode_NpcLock : public UMT2QuestNode
{
	GENERATED_BODY()
public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC") bool bUnlock = false;
};

UCLASS(DisplayName = "Purge NPC")
class METIN2_API UMT2QuestNode_PurgeNpc : public UMT2QuestNode
{
	GENERATED_BODY()
public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;
};

UCLASS(DisplayName = "Break Loop")
class METIN2_API UMT2QuestNode_Break : public UMT2QuestNode
{
	GENERATED_BODY()
public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;
};

// Read-only Lua sequence iteration. The manager retains the iterator frame across dialog suspends.
UCLASS(DisplayName = "For Each (ipairs)")
class METIN2_API UMT2QuestNode_ForEach : public UMT2QuestNode
{
	GENERATED_BODY()

public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loop") FString TableExpression;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loop") FName IndexVariable;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loop") FName ValueVariable;
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Loop")
	TArray<TObjectPtr<UMT2QuestNode>> Body;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Loop", meta = (ClampMin = "1"))
	int32 MaximumIterations = 10000;
};

UCLASS(DisplayName = "Table Callback")
class METIN2_API UMT2QuestNode_TableCallback : public UMT2QuestNode
{
	GENERATED_BODY()
public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Callback") FString TableExpression;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Callback") FName KeyVariable;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Callback") FName ValueVariable;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Callback") bool bSequence = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Callback") FName ResultVariable;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Callback") bool bResultQuestScoped = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Callback") TArray<FName> LocalVariables;
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Callback") TArray<TObjectPtr<UMT2QuestNode>> Body;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Callback", meta = (ClampMin = "1")) int32 MaximumIterations = 10000;
};

// Assigns a script variable. Lua `local` values live for one trigger; bare assignments use the
// per-player quest scope shared by that quest's triggers.
UCLASS(DisplayName = "Set Variable")
class METIN2_API UMT2QuestNode_SetVariable : public UMT2QuestNode
{
	GENERATED_BODY()

public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variable")
	FName VariableName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variable")
	FString Expression;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variable")
	bool bQuestScoped = false;

	// Literal Lua tables belong to the node that declares them. Keeping them inline avoids fragile
	// generated-name lookups through the shared quest-library asset.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Variable")
	bool bHasInlineTable = false;

	UPROPERTY()
	FMT2QuestTable InlineTable;
};

UCLASS(DisplayName = "Assign Values")
class METIN2_API UMT2QuestNode_AssignValues : public UMT2QuestNode
{
	GENERATED_BODY()
public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variable") TArray<FName> VariableNames;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variable") TArray<FString> Expressions;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variable") TArray<bool> QuestScopedTargets;
};

UCLASS(DisplayName = "Mutate Table")
class METIN2_API UMT2QuestNode_MutateTable : public UMT2QuestNode
{
	GENERATED_BODY()
public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Table") FString TableExpression;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Table") FString KeyExpression;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Table") FString ValueExpression;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Table") bool bInsert = false;
};

// Calls a function declared inside the imported quest. Parameters are evaluated in the caller, then
// its body runs in a marked execution frame so `return` resumes the caller instead of ending the quest.
UCLASS(DisplayName = "Call Quest Function")
class METIN2_API UMT2QuestNode_CallFunction : public UMT2QuestNode
{
	GENERATED_BODY()

public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Function")
	FName FunctionName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Function")
	TArray<FName> ParameterNames;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Function")
	TArray<FString> ArgumentExpressions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Function")
	FName ResultVariable;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Function")
	bool bResultQuestScoped = false;

	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Function")
	TArray<TObjectPtr<UMT2QuestNode>> Body;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Function") TArray<FName> ResultVariables;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Function") TArray<bool> ResultQuestScopes;
};

// pc.give_item2 / pc.remove_item. The *Expression fields override the literal when the script passed
// something computed instead of a constant; leaving them empty uses the literal.
UCLASS(DisplayName = "Give Item")
class METIN2_API UMT2QuestNode_GiveItem : public UMT2QuestNode
{
	GENERATED_BODY()

public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	int32 ItemVnum = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item", meta = (ClampMin = "1"))
	int32 Count = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Expressions")
	FString ItemVnumExpression;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Expressions")
	FString CountExpression;

	// Optional race-specific replacement for ItemVnum. Imported from old quest helper tables such as
	// give_basic_weapon.basic_item(pc.job, 1); an absent race falls back to ItemVnum/its expression.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	TMap<EMT2CharacterRace, int32> ItemVnumByRace;
};

UCLASS(DisplayName = "Take Item")
class METIN2_API UMT2QuestNode_TakeItem : public UMT2QuestNode
{
	GENERATED_BODY()

public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	int32 ItemVnum = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item", meta = (ClampMin = "1"))
	int32 Count = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Expressions")
	FString ItemVnumExpression;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Expressions")
	FString CountExpression;
};

// pc.give_exp2 / pc.give_exp_perc / pc.change_money (negative Gold takes gold).
UCLASS(DisplayName = "Give Reward")
class METIN2_API UMT2QuestNode_GiveReward : public UMT2QuestNode
{
	GENERATED_BODY()

public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward")
	int64 Experience = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward")
	int64 Gold = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Expressions")
	FString ExperienceExpression;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Expressions")
	FString GoldExpression;

	// pc.give_exp_perc(name, level, percent): a share of what one level costs at ExperienceLevel,
	// rather than a flat amount. Zero percent means this node grants no percentage-based experience.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward")
	float ExperiencePercent = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward")
	int32 ExperienceLevel = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Expressions")
	FString ExperiencePercentExpression;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reward|Expressions")
	FString ExperienceLevelExpression;
};

// pc.change_sp(delta): changes current mana, refusing a cost the player cannot pay like the old API.
UCLASS(DisplayName = "Change Mana")
class METIN2_API UMT2QuestNode_ChangeMana : public UMT2QuestNode
{
	GENERATED_BODY()

public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mana")
	int32 Delta = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mana|Expressions")
	FString DeltaExpression;
};

// pc.set_skill_level(vnum, level). horse.set_level(level) imports through this same node using the
// old server's SKILL_HORSE (130), because CHARACTER::SetHorseLevel mirrors the level into that skill.
UCLASS(DisplayName = "Set Skill Level")
class METIN2_API UMT2QuestNode_SetSkillLevel : public UMT2QuestNode
{
	GENERATED_BODY()

public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skill")
	int32 SkillVnum = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skill")
	int32 Level = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skill|Expressions")
	FString SkillVnumExpression;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skill|Expressions")
	FString LevelExpression;
};

// input()/input_number(): asks the owning client for text and resumes with the submitted value.
UCLASS(DisplayName = "Input")
class METIN2_API UMT2QuestNode_Input : public UMT2QuestNode
{
	GENERATED_BODY()

public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	FName ResultVariable;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	bool bNumericOnly = false;
};

// Mutating skill APIs that do not naturally map to SetSkillLevel. These remain one node family so
// imported quests expose the operation clearly without duplicating execution and validation code.
UENUM(BlueprintType)
enum class EMT2QuestSkillOperation : uint8
{
	SetGroup,
	ClearAll,
	ClearOne
};

UCLASS(DisplayName = "Modify Player Skills")
class METIN2_API UMT2QuestNode_ModifySkills : public UMT2QuestNode
{
	GENERATED_BODY()

public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skills")
	EMT2QuestSkillOperation Operation = EMT2QuestSkillOperation::SetGroup;

	// Skill group for SetGroup, or skill vnum for ClearOne.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skills")
	int32 Value = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skills|Expressions")
	FString ValueExpression;
};

// horse.summon(): registers the player's grade-appropriate called horse. The current mount system
// keeps this as server-owned state; Ctrl+H or a horse book performs the actual mount transition.
UCLASS(DisplayName = "Call Horse")
class METIN2_API UMT2QuestNode_CallHorse : public UMT2QuestNode
{
	GENERATED_BODY()

public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Horse")
	int32 HorseSkillVnum = 130;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Horse")
	int32 BasicHorseBookVnum = 50051;
};

UENUM(BlueprintType)
enum class EMT2QuestHorseOperation : uint8
{
	Ride,
	Unride,
	Unsummon,
	Advance
};

UCLASS(DisplayName = "Modify Horse")
class METIN2_API UMT2QuestNode_ModifyHorse : public UMT2QuestNode
{
	GENERATED_BODY()

public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Horse")
	EMT2QuestHorseOperation Operation = EMT2QuestHorseOperation::Ride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Horse")
	int32 HorseSkillVnum = 130;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Horse", meta = (ClampMin = "1"))
	int32 MaximumHorseLevel = 30;
};

// pc.setqf / pc.set_quest_flag.
UCLASS(DisplayName = "Set Flag")
class METIN2_API UMT2QuestNode_SetFlag : public UMT2QuestNode
{
	GENERATED_BODY()

public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flag")
	FName FlagName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flag")
	int32 Value = 1;

	// Adds to the current value instead of replacing it (old pc.setqf(f+1) idiom).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flag")
	bool bAdd = false;

	// party.setqf writes each locally online member's quest flag, or the caller's when solo.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flag")
	bool bPartyScoped = false;

	// Evaluated once for party.setqf; preserves quoted and computed Lua flag names.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flag|Expressions")
	FString PartyFlagNameExpression;

	// Overrides Value when set, for the common pc.setqf("f", pc.getqf("f") + 1) form.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flag|Expressions")
	FString ValueExpression;
};

// set_state(): moves this quest to another state for this player.
UCLASS(DisplayName = "Set Quest State")
class METIN2_API UMT2QuestNode_SetState : public UMT2QuestNode
{
	GENERATED_BODY()

public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State")
	FName StateName = TEXT("start");
};

// chat/notice text (d.notice, say_reward style feedback) straight into the system chat.
UCLASS(DisplayName = "Notice")
class METIN2_API UMT2QuestNode_Notice : public UMT2QuestNode
{
	GENERATED_BODY()

public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Notice")
	FText Message;
};

// Quest-journal operations (the old q.start / q.set_title / q.done / send_letter / makequestbutton
// family). They maintain the player's quest-log entry for the running quest.
UENUM(BlueprintType)
enum class EMT2QuestJournalOp : uint8
{
	Start,        // q.start / send_letter: create or refresh the journal entry
	SetTitle,     // q.set_title
	SetSummary,   // q.set_clock_name / counter name / descriptive line
	SetCounter,   // q.set_counter / set_counter_value: the "x / y" progress figure
	Done,         // q.done: mark complete
	Clear,        // clear_letter: remove the entry
	Restart       // restart_quest: reset to start without marking the quest complete
};

// One journal operation on the running quest's log entry.
UCLASS(DisplayName = "Quest Journal")
class METIN2_API UMT2QuestNode_Journal : public UMT2QuestNode
{
	GENERATED_BODY()

public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Journal")
	EMT2QuestJournalOp Operation = EMT2QuestJournalOp::Start;

	// Title/summary text for SetTitle and SetSummary.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Journal")
	FText Text;

	// Counter value for SetCounter (or its expression, when the script computed it).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Journal")
	int32 CounterValue = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Journal|Expressions")
	FString CounterExpression;
};

// Quest target markers (the old target.pos / target.vid / target.delete): the map waypoints that point
// a player at where a quest wants them to go.
UENUM(BlueprintType)
enum class EMT2QuestTargetOp : uint8
{
	SetPosition,  // target.pos(name, x, y)
	SetNpc,       // target.vid(name, vid) - point at an NPC/mob
	Clear,        // target.delete(name)
	SetActor      // target.vid with a real runtime entity ID; appended for serialized enum compatibility.
};

UCLASS(DisplayName = "Quest Target")
class METIN2_API UMT2QuestNode_Target : public UMT2QuestNode
{
	GENERATED_BODY()

public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target")
	EMT2QuestTargetOp Operation = EMT2QuestTargetOp::SetPosition;

	// Marker id, so a later target.delete can remove exactly this one.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target")
	FName TargetName;

	// Old local map coordinates for SetPosition; converted to world XY at runtime.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target")
	FVector2D Position = FVector2D::ZeroVector;

	// NPC/mob vnum for SetNpc.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target")
	int32 Vnum = 0;

	// target.vid(name, v) passes a variable rather than a literal vnum, so the identifier is usually
	// computed (typically from find_npc_by_vnum).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target|Expressions")
	FString VnumExpression;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target|Expressions")
	FString PositionXExpression;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target|Expressions")
	FString PositionYExpression;
};

// timer(name, seconds) / server_timer(name, seconds, arg): schedules a named timer that fires a
// `when <name>.timer` / `.server_timer` trigger when it elapses. The optional argument is readable in
// the handler through get_server_timer_arg().
UCLASS(DisplayName = "Start Timer")
class METIN2_API UMT2QuestNode_StartTimer : public UMT2QuestNode
{
	GENERATED_BODY()

public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timer")
	FName TimerName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timer", meta = (Units = "s"))
	float Seconds = 60.0f;

	// Fires a ServerTimer event instead of a Timer event (the old server_timer family).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timer")
	bool bServerTimer = false;

	// Repeats until cleared (old loop_timer).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timer")
	bool bLooping = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timer|Expressions")
	FString SecondsExpression;

	// Value handed to the handler via get_server_timer_arg().
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timer|Expressions")
	FString ArgumentExpression;
};

// clear_server_timer(name) / cleartimer(name): cancels a scheduled timer.
UCLASS(DisplayName = "Clear Timer")
class METIN2_API UMT2QuestNode_ClearTimer : public UMT2QuestNode
{
	GENERATED_BODY()

public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;

	// Empty clears every timer this quest started.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timer")
	FName TimerName;
};

// item.set_socket(index, value): writes the scripted numeric value of a socket on the item that raised
// the current item event (quest items use sockets as scratch storage).
UCLASS(DisplayName = "Set Item Socket")
class METIN2_API UMT2QuestNode_SetItemSocket : public UMT2QuestNode
{
	GENERATED_BODY()

public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Socket", meta = (ClampMin = "0"))
	int32 SocketIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Socket")
	int32 Value = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Socket|Expressions")
	FString ValueExpression;
};

// mob.spawn(vnum, x, y) / d.spawn_mob(...): spawns a mob on the current map. Coordinates are the old
// local metre coordinates; leaving them at zero spawns next to the player (what most scripts intend).
UCLASS(DisplayName = "Spawn Mob")
class METIN2_API UMT2QuestNode_SpawnMob : public UMT2QuestNode
{
	GENERATED_BODY()

public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	int32 MobVnum = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn", meta = (ClampMin = "1"))
	int32 Count = 1;

	// Zero means "at the player".
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	FVector2D Position = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn|Expressions")
	FString MobVnumExpression;
};

// game.drop_item(vnum, count): drops an item on the ground at the player.
UCLASS(DisplayName = "Drop Item")
class METIN2_API UMT2QuestNode_DropItem : public UMT2QuestNode
{
	GENERATED_BODY()

public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	int32 ItemVnum = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item", meta = (ClampMin = "1"))
	int32 Count = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Expressions")
	FString ItemVnumExpression;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Expressions")
	FString CountExpression;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	bool bOwnedByPlayer = false;
};

// item.remove(): consumes the item that raised the current item event (the old scripts' way of using
// up a quest item once its script has run).
UCLASS(DisplayName = "Remove Used Item")
class METIN2_API UMT2QuestNode_RemoveUsedItem : public UMT2QuestNode
{
	GENERATED_BODY()

public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;
};

// Opens the shop of the NPC that raised the current interaction. Exists so "Open Shop" can be offered
// as one option in the NPC's menu alongside its quests, instead of the shop pre-empting them.
UCLASS(DisplayName = "Open NPC Shop")
class METIN2_API UMT2QuestNode_OpenShop : public UMT2QuestNode
{
	GENERATED_BODY()

public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;

	// Lua calls continue; existing interaction-menu nodes close the dialogue and stop.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop")
	bool bContinueQuest = false;
};

// pc.warp resolves old global coordinates to a destination map; pc.warp_local stays on this map.
UCLASS(DisplayName = "Warp Player")
class METIN2_API UMT2QuestNode_Warp : public UMT2QuestNode
{
	GENERATED_BODY()

public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp")
	FVector2D Position = FVector2D::ZeroVector;

	// Scripts often compute the destination (`pc.warp(CordX, CordY)`), so each axis may be an expression.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp|Expressions")
	FString PositionXExpression;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp|Expressions")
	FString PositionYExpression;

	// Compatibility for old authored nodes. Newly imported pc.warp_local uses the fields below.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp")
	bool bCoordinatesAreLocalMetres = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp")
	bool bUsesLegacyMapLocalCoordinates = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp|Expressions")
	FString MapIndexExpression;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp")
	bool bToEmpireVillage = false;

	bool ResolveMapDestination(const FMT2QuestContext& Context, const UMT2QuestTableAsset& Tables,
		const FMT2QuestMapDefinition*& OutMap, FVector2D& OutPosition, FString& OutError) const;
};

// wait(): shows the page built so far and pauses until the player confirms, then carries on. This is
// the old scripts' page break - the "next" click between two halves of a conversation.
UCLASS(DisplayName = "Wait For Confirm")
class METIN2_API UMT2QuestNode_Wait : public UMT2QuestNode
{
	GENERATED_BODY()

public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;
};

// Closes the dialog window and ends the block (the old scripts' explicit close).
UCLASS(DisplayName = "Close Dialog")
class METIN2_API UMT2QuestNode_Close : public UMT2QuestNode
{
	GENERATED_BODY()

public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;
};

UENUM(BlueprintType)
enum class EMT2QuestAffectOperation : uint8
{
	AddQuest,
	AddCollect,
	AddHair,
	RemoveAllCollect,
	RemoveHair
};

// Old affect.add/add_collect/add_collect_point/removal operations. ApplyType stores the modern
// EApplyTypes ordinal, including point-based collect rewards normalized by the importer.
UCLASS(DisplayName = "Modify Status Effect")
class METIN2_API UMT2QuestNode_ModifyAffect : public UMT2QuestNode
{
	GENERATED_BODY()

public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affect")
	EMT2QuestAffectOperation Operation = EMT2QuestAffectOperation::AddQuest;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affect")
	int32 ApplyType = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affect")
	int32 Value = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affect")
	int32 DurationSeconds = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affect|Expressions")
	FString ApplyTypeExpression;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affect|Expressions")
	FString ValueExpression;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Affect|Expressions")
	FString DurationExpression;
};

// Returns from an imported quest-local function. At top level it stops the current trigger without
// changing quest state, matching Lua's early-return behavior.
UCLASS(DisplayName = "Return")
class METIN2_API UMT2QuestNode_Return : public UMT2QuestNode
{
	GENERATED_BODY()

public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Return")
	FString ValueExpression;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Return") TArray<FString> ValueExpressions;
};

// item.set_value(index, apply, value): replaces one mutable item attribute.
UCLASS(DisplayName = "Set Item Attribute")
class METIN2_API UMT2QuestNode_SetItemAttribute : public UMT2QuestNode
{
	GENERATED_BODY()

public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attribute") FString IndexExpression;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attribute") FString ApplyTypeExpression;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attribute") FString ValueExpression;
};

UCLASS(DisplayName = "Copy And Replace Item")
class METIN2_API UMT2QuestNode_CopyItem : public UMT2QuestNode
{
	GENERATED_BODY()

public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item") FString ResultVnumExpression;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item") FString MaterialTableExpression;
};

UCLASS(DisplayName = "TODO (Unconverted Lua)")
class METIN2_API UMT2QuestNode_Unconverted : public UMT2QuestNode
{
	GENERATED_BODY()

public:
	virtual EMT2QuestNodeResult Execute(const FMT2QuestContext& Context) override;

	// Source script and line the snippet came from.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unconverted")
	FString SourceLocation;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unconverted", meta = (MultiLine = "true"))
	FString SourceLua;

	// Unsupported inventory transactions must not fall through to consuming their materials.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Unconverted")
	bool bStopExecution = false;
};
