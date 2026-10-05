/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Quests/MT2QuestTableAsset.h"
#include "Quests/MT2QuestTypes.h"

struct FMT2QuestRuntimeTable;

// Script values and shared table identity, independent of generated Blueprint defaults.
struct METIN2_API FMT2QuestValue
{
	double Number = 0.0;
	FString Text;
	bool bIsText = false;
	bool bIsNil = true;
	bool bIsBoolean = false;
	TSharedPtr<FMT2QuestRuntimeTable> RuntimeTable;
	// Transient call-result metadata. Scalar storage/operators discard it; lists expand only their tail.
	TSharedPtr<const TArray<FMT2QuestValue>> CallResults;
	FMT2QuestValue Scalar() const { FMT2QuestValue Result = *this; Result.CallResults.Reset(); return Result; }
	static FMT2QuestValue Results(TArray<FMT2QuestValue> Values);

	// A reference into a converted Lua table (`special.levelup_quest[lev][1]`). Null unless this value
	// *is* a table node; the tables themselves are shared read-only data.
	const FMT2QuestTable* Table = nullptr;
	int32 TableNodeIndex = INDEX_NONE;
	// Iterator snapshots can outlive their frame when a nested value is assigned to another local.
	TSharedPtr<const FMT2QuestTable> OwnedTable;

	FMT2QuestValue() = default;
	explicit FMT2QuestValue(double InNumber) : Number(InNumber), bIsNil(false) {}
	explicit FMT2QuestValue(const FString& InText) : Text(InText), bIsText(true), bIsNil(false) {}
	FMT2QuestValue(const FMT2QuestTable* InTable, int32 InNodeIndex)
		: bIsNil(false), Table(InTable), TableNodeIndex(InNodeIndex) {}

	bool IsTableRef() const { return RuntimeTable.IsValid() || (Table != nullptr && TableNodeIndex != INDEX_NONE); }
	static FMT2QuestValue NewTable();
	static FMT2QuestValue Boolean(bool Value)
	{
		FMT2QuestValue Result(Value ? 1.0 : 0.0);
		Result.bIsBoolean = true;
		return Result;
	}
	FMT2QuestValue ToRuntimeTable() const;
	FMT2QuestValue GetTableValue(const FMT2QuestValue& Key) const;
	bool SetTableValue(const FMT2QuestValue& Key, const FMT2QuestValue& Value, bool bCommit = true) const;
	int32 GetTableLength() const;
	TArray<FMT2QuestValue> GetTableKeys() const;

	// Lua's only false values are nil and the boolean false; zero and empty strings are true.
	bool AsBool() const { return !bIsNil && (!bIsBoolean || Number != 0.0); }
	int32 AsInt() const { return bIsText ? FCString::Atoi(*Text) : FMath::RoundToInt(Number); }
};

struct FMT2QuestRuntimeTable
{
	TMap<double, FMT2QuestValue> Numbers;
	TMap<FString, FMT2QuestValue> Names;
	TMap<bool, FMT2QuestValue> Booleans;
};

// Evaluates the expression subset the old .quest scripts use: arithmetic, comparisons, and/or/not,
// and the quest API calls (pc.get_level(), pc.count_item(v), pc.getqf(f), game.get_event_flag(f), ...).
// This is what lets `if`, `local x = ...` and non-literal arguments convert into data instead of
// becoming TODOs - the expression text rides on the node and is evaluated at runtime.
//
// It deliberately covers expressions only; statements and control flow remain node structure.
class METIN2_API FMT2QuestExpression
{
public:
	// Evaluates Expression. bOutOk is false when something in it is unsupported, in which case the
	// caller should treat the result as unusable rather than as a value.
	static FMT2QuestValue Evaluate(
		const FString& Expression, const FMT2QuestContext& Context, bool& bOutOk);
	static TArray<FMT2QuestValue> EvaluateList(
		const TArray<FString>& Expressions, const FMT2QuestContext& Context, bool& bOutOk);

	static bool EvaluateBool(const FString& Expression, const FMT2QuestContext& Context);

	// Import-time validation: true when every identifier and call in Expression is understood, so the
	// importer can convert it instead of emitting a TODO. OutUnsupported names what is missing.
	static bool IsSupported(const FString& Expression, FString& OutUnsupported);

	// Names the evaluator can call. Shared with IsSupported so validation can never drift from runtime.
	static bool IsSupportedFunction(const FString& FunctionName);

	// True when TableName is one of the converted Lua tables, so the importer can accept expressions
	// that index it.
	static bool IsKnownTable(const FString& TableName);
};
