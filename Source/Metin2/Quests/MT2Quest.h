/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Quests/MT2QuestTypes.h"
#include "UObject/Object.h"
#include "MT2Quest.generated.h"

class UMT2QuestNode;
class UMT2QuestCondition;

// One trigger inside a quest state: which event runs this block, and what it filters on.
// Mirrors the old `when <vnum>.<event> begin ... end`.
USTRUCT(BlueprintType)
struct METIN2_API FMT2QuestTrigger
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger")
	EMT2QuestEvent Event = EMT2QuestEvent::Click;

	// NPC vnum for Click/Chat, mob vnum for Kill, item vnum for ItemUse. 0 = match any.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger")
	int32 Vnum = 0;

	// Name-matched events (Timer/ServerTimer, TargetClick, Arrive and TargetDie) identify themselves by name
	// rather than a vnum: `when devilcatacomb_45m_left_timer.server_timer`. Empty = match any.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger")
	FName TriggerName;

	// Chat-menu label for Chat triggers (the old `when <vnum>.chat."Some option"`).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger")
	FText ChatOption;

	// Extra gate on top of the event itself; the trigger is skipped when these fail.
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Trigger")
	TArray<TObjectPtr<UMT2QuestCondition>> Conditions;

	// Synchronous helper calls needed to compute a legacy `with` clause, before this trigger claims
	// an event or appears in a chat menu. Locals are isolated from the eventual body execution.
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Trigger")
	TArray<TObjectPtr<UMT2QuestNode>> GatePrelude;

	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Trigger")
	TArray<TObjectPtr<UMT2QuestCondition>> GateConditions;

	// The block to run - authored inline in the details panel.
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Trigger")
	TArray<TObjectPtr<UMT2QuestNode>> Nodes;
};

// A constant declared at quest scope in the old script (`GUARD = 20345`), outside any `when` block.
// Seeded into the script variables before a block runs, so expressions can reference it by name.
USTRUCT(BlueprintType)
struct METIN2_API FMT2QuestConstant
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constant")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constant")
	FString Expression;
};

// A quest state (old `state start begin ... end`). Only the player's current state's triggers listen.
USTRUCT(BlueprintType)
struct METIN2_API FMT2QuestState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State")
	FName StateName = TEXT("start");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State")
	TArray<FMT2QuestTrigger> Triggers;
};

// A single quest. Every quest is a Blueprint asset under /Game/Quests: the class defaults hold the
// declarative states/triggers/nodes (which covers the great majority of the old scripts), and the
// Blueprint's own graph can override the hooks below for logic that isn't expressible as data - the
// same escape hatch the complex Lua scripts need.
//
// Quests are stateless shared definitions; all per-player progress lives in UMT2QuestManagerComponent.
UCLASS(Blueprintable, BlueprintType, Abstract)
class METIN2_API UMT2Quest : public UObject
{
	GENERATED_BODY()

public:
	// Identifier used for flag scoping and save data. Defaults to the asset name when left empty.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Quest")
	FName QuestName;

	// Shown by quest UI; not used for matching.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Quest")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Quest")
	TArray<FMT2QuestState> States;

	// Quest-scope constants (NPC vnums, item ids, thresholds) available to every trigger's expressions.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Quest")
	TArray<FMT2QuestConstant> Constants;

	// Resolved quest id (QuestName, or the asset name when unset).
	UFUNCTION(BlueprintPure, Category = "Quest")
	FName GetQuestId() const;

	const FMT2QuestState* FindState(FName StateName) const;

	// Blueprint hook: return true to swallow the event and skip the declarative triggers entirely.
	// This is where a converted complex script puts logic the data model can't express.
	UFUNCTION(BlueprintImplementableEvent, Category = "Quest")
	bool OnQuestEvent(const FMT2QuestContext& Context);
};
