/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Items/MT2ItemTypes.h"
#include "MT2QuestTypes.generated.h"

class UMT2QuestNode;
class UMT2QuestManagerComponent;
class AMT2PlayerCharacter;
class AMT2PlayerState;
class AActor;

// One quest dialog page sent server -> client: the accumulated say()/say_title() text plus the
// select() options (the old QuestScript packet). An empty Options array means a plain "Close".
USTRUCT(BlueprintType)
struct METIN2_API FMT2DialogPayload
{
	GENERATED_BODY()

	// say_title() line (usually the NPC or quest name), shown highlighted.
	UPROPERTY(BlueprintReadOnly, Category = "Dialog")
	FString Title;

	// say() lines, in order.
	UPROPERTY(BlueprintReadOnly, Category = "Dialog")
	TArray<FString> TextLines;

	// select() choices; answering sends the 1-based choice index back, like the old scripts expect.
	UPROPERTY(BlueprintReadOnly, Category = "Dialog")
	TArray<FString> Options;

	UPROPERTY(BlueprintReadOnly, Category = "Dialog")
	bool bRequestsTextInput = false;

	UPROPERTY(BlueprintReadOnly, Category = "Dialog")
	bool bNumericInput = false;
};

// Old quest.h event types. A quest state's triggers subscribe to one of these; the manager fans the
// game's events out to every quest that listens (the old CQuestManager dispatch).
UENUM(BlueprintType)
enum class EMT2QuestEvent : uint8
{
	Click,      // NPC clicked (filter = NPC vnum)
	Chat,       // NPC chat menu entry chosen (filter = NPC vnum, ChatOption = the menu label)
	Kill,       // mob killed (filter = mob vnum, 0 = any)
	LevelUp,
	Login,
	Logout,
	Button,     // quest window button
	Info,
	ItemUse,    // item used (filter = item vnum)
	Timer,
	Letter,     // quest (re)evaluated - the old "letter" refresh that draws the quest entry
	Enter,      // entered the map/area
	Leave,
	Target,     // quest target reached
	Arrive,
	Unmount,    // dismounted a horse
	PartyKill,  // a party member landed the kill
	ServerTimer, // scheduled server-side timer fired
	ItemTake,   // item offered to an NPC (filter = NPC vnum, not item vnum)
	TargetClick, // named target clicked; appended to preserve serialized event values
	TargetDie    // tracked entity disappeared
};

// Result of running one quest node, driving the resumable executor.
UENUM(BlueprintType)
enum class EMT2QuestNodeResult : uint8
{
	Continue,   // move to the next node
	Suspend,    // waiting on the player (a dialog select/input); resumed by the manager
	Stop        // end this quest block entirely
};

// Everything a node needs while running. Rebuilt per dispatch; never stored across a suspend (the
// manager re-resolves it on resume, so a logged-out/moved player can't leave a stale pointer behind).
USTRUCT(BlueprintType)
struct METIN2_API FMT2QuestContext
{
	GENERATED_BODY()

	// Per-player quest manager: quest flags, states, dialog. Always valid while a node runs.
	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	TObjectPtr<UMT2QuestManagerComponent> Manager = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	TObjectPtr<AMT2PlayerState> PlayerState = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	TObjectPtr<AMT2PlayerCharacter> Player = nullptr;

	// The NPC that was clicked / mob that was killed / item used, when the event carries one.
	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	TObjectPtr<AActor> TargetActor = nullptr;

	// Event that started this run, plus the vnum it carried (mob killed, item used, NPC clicked).
	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	EMT2QuestEvent Event = EMT2QuestEvent::Click;

	// Captured by entry-event runs, including suspended dialogs; not a global warp disable.
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Quest")
	bool bPreservePIESpawn = false;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	int32 EventVnum = 0;

	// Inventory slot of the item that raised an ItemUse event, so item.* reads (sockets, count) resolve
	// against the actual item the script is acting on. INDEX_NONE when the event carries no item.
	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	int32 EventItemSlot = -1;

	// Server snapshot at the start of an NPC item-offer conversation. Revalidated after suspension.
	UPROPERTY()
	FMT2ItemSlot EventItemSnapshot;
	UPROPERTY()
	bool bHasEventItemSnapshot = false;

	// The quest whose block is running (a /Game/Quests Blueprint CDO).
	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	TObjectPtr<const class UMT2Quest> Quest = nullptr;
};
