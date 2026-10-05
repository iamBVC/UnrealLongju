/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"

struct FMT2QuestImportResult
{
	int32 ScriptsParsed = 0;
	int32 QuestsCreated = 0;
	int32 QuestsRefreshed = 0;
	int32 TriggersConverted = 0;
	int32 NodesCreated = 0;
	// Statements the translator could not express as nodes; each becomes a stub + a report line.
	int32 StatementsUnconverted = 0;
	int32 LocaleStringsLoaded = 0;

	TArray<FString> Warnings;
	TArray<FString> Errors;
	// Per-script "what didn't convert" detail, written next to the generated assets.
	TArray<FString> ConversionReport;

	FString BuildSummary() const;
};

// Converts the old server's Lua .quest scripts into quest Blueprints under /Game/Quests
// (see Docs/OldGameResearch/QuestSystem.md).
//
// The mechanical subset - say/select/if, give/take item, exp/gold, quest flags, set_state - becomes
// declarative UMT2QuestNode data on the generated Blueprint's class defaults. Anything the translator
// cannot express (loops, local functions, math.random, string.format, table work) is NOT silently
// dropped: the original Lua is preserved in the trigger's stub node and listed in the conversion
// report, so the remaining work is explicit and finished by hand in the Blueprint graph.
//
// Dialog text is resolved through the locale's translate.lua, so generated quests carry real strings
// instead of gameforge.* keys.
class FMT2QuestImporter
{
public:
	// QuestSourceRoot: the locale quest folder (contains *.quest and, one level up, translate.lua).
	// DestinationRoot: content root for the generated assets, normally "/Game" (-> /Game/Quests).
	static bool Import(
		const FString& QuestSourceRoot, const FString& DestinationRoot, FMT2QuestImportResult& OutResult,
		const FString& ClientSourceRoot = FString());
};
