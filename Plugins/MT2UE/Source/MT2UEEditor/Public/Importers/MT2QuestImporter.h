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
// Supported dialogue, control flow, functions, expressions and gameplay APIs become declarative
// quest nodes. Unsupported statements retain their original Lua in TODO nodes and the report.
//
// Dialog text is resolved through the locale's translate.lua, so generated quests carry real strings
// instead of gameforge.* keys.
class FMT2QuestImporter
{
public:
	// QuestSourceRoot: the locale quest folder (contains *.quest and, one level up, translate.lua).
	// DestinationRoot: content root for the generated assets, normally "/Game" (-> /Game/Quests).
	// bSavePackages=false is for isolated commandlet audits: assets change in memory but are not saved.
	static bool Import(
		const FString& QuestSourceRoot, const FString& DestinationRoot, FMT2QuestImportResult& OutResult,
		const FString& ClientSourceRoot = FString(), bool bSavePackages = true);
};
