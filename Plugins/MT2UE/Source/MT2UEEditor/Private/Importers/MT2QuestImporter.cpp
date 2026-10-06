/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Importers/MT2QuestImporter.h"
#include "Config/MT2PathSettings.h"
#include "MT2QuestExpressionSyntax.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "FileHelpers.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "HAL/FileManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Quests/MT2Quest.h"
#include "Quests/MT2QuestCondition.h"
#include "Quests/MT2QuestExpression.h"
#include "Quests/MT2QuestNode.h"
#include "Quests/MT2QuestTableAsset.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Characters/MT2PlayerCharacter.h"
#include "Components/MT2StatusEffectComponent.h"
#include "Components/MT2StatusEffectDefinition.h"
#include "Mobs/MT2Mob.h"
#include "Npcs/MT2Npc.h"
#include "Npcs/MT2NpcShopComponent.h"
#include "Player/MT2PlayerState.h"
#include "Party/MT2Party.h"
#include "Skills/MT2SkillComponent.h"
#include "Skills/MT2SkillDefinition.h"
#include "Skills/MT2SkillSet.h"
#include "Config/MT2GameplaySettings.h"
#include "Dom/JsonObject.h"
#include "Quests/MT2QuestCondition.h"
#include "Quests/MT2QuestComponent.h"
#include "Quests/MT2QuestManagerComponent.h"
#include "Quests/MT2QuestRegistrySubsystem.h"
#include "Core/MT2VnumRegistrySubsystem.h"
#include "Core/MT2VnumRegistry.h"
#include "Items/MT2InventoryComponent.h"
#include "Items/MT2ItemTemplate.h"
#include "UObject/StrongObjectPtr.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogMT2QuestImport, Log, All);

FString FMT2QuestImportResult::BuildSummary() const
{
	return FString::Printf(
		TEXT("Quest import: %d script(s) parsed, %d quest(s) created, %d refreshed, %d trigger(s), ")
		TEXT("%d node(s), %d unconverted statement(s), %d locale string(s)."),
		ScriptsParsed, QuestsCreated, QuestsRefreshed, TriggersConverted, NodesCreated,
		StatementsUnconverted, LocaleStringsLoaded);
}

namespace
{
	// ---------------------------------------------------------------------------------------------
	// Lua-ish scanning helpers
	// ---------------------------------------------------------------------------------------------

	// Strips a trailing -- comment, ignoring one inside a quoted string.
	FString StripComment(const FString& Line)
	{
		bool bInString = false;
		TCHAR StringQuote = TEXT('"');
		for (int32 Index = 0; Index < Line.Len() - 1; ++Index)
		{
			const TCHAR Char = Line[Index];
			if (bInString)
			{
				if (Char == TEXT('\\')) { ++Index; continue; }
				if (Char == StringQuote) { bInString = false; }
				continue;
			}
			if (Char == TEXT('"') || Char == TEXT('\''))
			{
				bInString = true;
				StringQuote = Char;
				continue;
			}
			if (Char == TEXT('-') && Line[Index + 1] == TEXT('-'))
			{
				return Line.Left(Index);
			}
		}
		return Line;
	}

	FString NormalizeStatement(const FString& Line)
	{
		FString Result = StripComment(Line).TrimStartAndEnd();
		while (Result.RemoveFromEnd(TEXT(";")))
		{
			Result.TrimEndInline();
		}
		return Result;
	}

	void LoadQuestNpcAliases(const FString& Filename, TMap<FString, int32>& OutAliases)
	{
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *Filename))
		{
			return;
		}

		for (const FString& RawLine : Lines)
		{
			const FString Line = StripComment(RawLine).TrimStartAndEnd();
			int32 Separator = INDEX_NONE;
			for (int32 Index = 0; Index < Line.Len(); ++Index)
			{
				if (FChar::IsWhitespace(Line[Index]))
				{
					Separator = Index;
					break;
				}
			}
			if (Separator == INDEX_NONE)
			{
				continue;
			}

			const FString VnumText = Line.Left(Separator).TrimStartAndEnd();
			const FString Alias = Line.Mid(Separator + 1).TrimStartAndEnd();
			if (!VnumText.IsNumeric() || Alias.IsEmpty())
			{
				continue;
			}
			OutAliases.Add(Alias, FCString::Atoi(*VnumText));
		}
	}

	// Whole-word keyword scan of a comment/string-free line.
	void CollectKeywords(const FString& Line, TArray<FString>& OutWords)
	{
		FString Current;
		bool bInString = false;
		TCHAR StringQuote = TEXT('"');
		for (int32 Index = 0; Index < Line.Len(); ++Index)
		{
			const TCHAR Char = Line[Index];
			if (bInString)
			{
				if (Char == TEXT('\\')) { ++Index; continue; }
				if (Char == StringQuote) { bInString = false; }
				continue;
			}
			if (Char == TEXT('"') || Char == TEXT('\''))
			{
				bInString = true;
				StringQuote = Char;
				if (!Current.IsEmpty()) { OutWords.Add(Current); Current.Reset(); }
				continue;
			}
			if (FChar::IsAlnum(Char) || Char == TEXT('_'))
			{
				Current.AppendChar(Char);
			}
			else if (!Current.IsEmpty())
			{
				OutWords.Add(Current);
				Current.Reset();
			}
		}
		if (!Current.IsEmpty())
		{
			OutWords.Add(Current);
		}
	}

	// Net block depth change for one line. 'elseif' deliberately does not open a block (it continues
	// the enclosing if), which is why keywords are matched as whole words.
	int32 BlockDelta(const FString& Line)
	{
		TArray<FString> Words;
		CollectKeywords(StripComment(Line), Words);
		int32 Delta = 0;
		for (const FString& Word : Words)
		{
			if (Word == TEXT("if") || Word == TEXT("for") || Word == TEXT("while") ||
				Word == TEXT("function") || Word == TEXT("begin") || Word == TEXT("repeat"))
			{
				++Delta;
			}
			else if (Word == TEXT("end") || Word == TEXT("until"))
			{
				--Delta;
			}
		}
		return Delta;
	}

	// Net open parentheses on a line, ignoring anything inside string literals. Positive means the
	// statement continues on the next line.
	int32 CountUnclosedParens(const FString& Line)
	{
		int32 Balance = 0;
		bool bInString = false;
		TCHAR StringQuote = TEXT('"');
		for (int32 Index = 0; Index < Line.Len(); ++Index)
		{
			const TCHAR Char = Line[Index];
			if (bInString)
			{
				if (Char == TEXT('\\')) { ++Index; continue; }
				if (Char == StringQuote) { bInString = false; }
				continue;
			}
			if (Char == TEXT('"') || Char == TEXT('\''))
			{
				bInString = true;
				StringQuote = Char;
				continue;
			}
			if (Char == TEXT('(')) { ++Balance; }
			else if (Char == TEXT(')')) { --Balance; }
		}
		return Balance;
	}

	bool LineHasWord(const FString& Line, const TCHAR* Word)
	{
		TArray<FString> Words;
		CollectKeywords(StripComment(Line), Words);
		return Words.Contains(Word);
	}

	// True when the `if` starting at HeaderLine has its `then` on that line or on a following one - a
	// condition may be broken after a trailing `and`/`or`. Scanning stops at the first line that opens
	// or closes a block, so a malformed header cannot swallow the rest of the script.
	bool IfHeaderHasThen(const TArray<FString>& Lines, int32 HeaderLine, int32 EndLine)
	{
		for (int32 Index = HeaderLine; Index < EndLine; ++Index)
		{
			if (LineHasWord(Lines[Index], TEXT("then")))
			{
				return true;
			}
			if (Index > HeaderLine && BlockDelta(Lines[Index]) != 0)
			{
				return false;
			}
		}
		return false;
	}

	// Index of the 'end' closing the block opened on StartLine. Also reports the 'else' belonging to
	// that block, when there is one. Returns INDEX_NONE if unbalanced.
	int32 FindMatchingEnd(const TArray<FString>& Lines, int32 StartLine, int32& OutElseLine)
	{
		OutElseLine = INDEX_NONE;
		int32 Depth = BlockDelta(Lines[StartLine]);
		if (Depth <= 0)
		{
			return INDEX_NONE;
		}
		for (int32 Index = StartLine + 1; Index < Lines.Num(); ++Index)
		{
			// An 'else' sitting at the block's own level splits then/else.
			if (Depth == 1 && OutElseLine == INDEX_NONE &&
				LineHasWord(Lines[Index], TEXT("else")) && !LineHasWord(Lines[Index], TEXT("elseif")))
			{
				OutElseLine = Index;
			}
			Depth += BlockDelta(Lines[Index]);
			if (Depth <= 0)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	// Splits "a, b, c" on top-level commas (parens/strings aware).
	TArray<FString> SplitArguments(const FString& Arguments)
	{
		TArray<FString> Result;
		int32 Depth = 0;
		bool bInString = false;
		TCHAR StringQuote = TEXT('"');
		FString Current;
		for (int32 Index = 0; Index < Arguments.Len(); ++Index)
		{
			const TCHAR Char = Arguments[Index];
			if (bInString)
			{
				Current.AppendChar(Char);
				if (Char == TEXT('\\') && Index + 1 < Arguments.Len())
				{
					Current.AppendChar(Arguments[++Index]);
					continue;
				}
				if (Char == StringQuote) { bInString = false; }
				continue;
			}
			if (Char == TEXT('"') || Char == TEXT('\''))
			{
				bInString = true;
				StringQuote = Char;
				Current.AppendChar(Char);
				continue;
			}
			if (Char == TEXT('(') || Char == TEXT('{') || Char == TEXT('[')) { ++Depth; }
			else if (Char == TEXT(')') || Char == TEXT('}') || Char == TEXT(']')) { --Depth; }
			if (Char == TEXT(',') && Depth == 0)
			{
				Result.Add(Current.TrimStartAndEnd());
				Current.Reset();
				continue;
			}
			Current.AppendChar(Char);
		}
		if (!Current.TrimStartAndEnd().IsEmpty())
		{
			Result.Add(Current.TrimStartAndEnd());
		}
		return Result;
	}

	// Extracts the argument text of the first Call( ... ) on the line, honouring nesting.
	// The scripts freely write "set_state (x)" with a space before the paren, so whitespace between the
	// name and '(' is skipped; a partial identifier match ("say" inside "say_title"/"essay") is rejected.
	bool ExtractCallArguments(const FString& Line, const FString& Call, FString& OutArguments)
	{
		const FString Clean = StripComment(Line);
		int32 SearchStart = 0;
		while (SearchStart < Clean.Len())
		{
			const int32 CallIndex =
				Clean.Find(Call, ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchStart);
			if (CallIndex == INDEX_NONE)
			{
				return false;
			}
			SearchStart = CallIndex + Call.Len();

			// Left boundary: must not continue an identifier or be a member of another table.
			if (CallIndex > 0)
			{
				const TCHAR Previous = Clean[CallIndex - 1];
				if (FChar::IsAlnum(Previous) || Previous == TEXT('_') || Previous == TEXT('.'))
				{
					continue;
				}
			}

			// Right boundary: the next non-space character must open the argument list.
			int32 Cursor = SearchStart;
			while (Cursor < Clean.Len() && FChar::IsWhitespace(Clean[Cursor]))
			{
				++Cursor;
			}
			if (Cursor >= Clean.Len() || Clean[Cursor] != TEXT('('))
			{
				continue;
			}

			int32 Depth = 0;
			int32 Start = INDEX_NONE;
			for (; Cursor < Clean.Len(); ++Cursor)
			{
				if (Clean[Cursor] == TEXT('('))
				{
					if (Depth == 0) { Start = Cursor + 1; }
					++Depth;
				}
				else if (Clean[Cursor] == TEXT(')'))
				{
					if (--Depth == 0)
					{
						OutArguments = Clean.Mid(Start, Cursor - Start);
						return true;
					}
				}
			}
			return false;
		}
		return false;
	}

	// ---------------------------------------------------------------------------------------------
	// Locale
	// ---------------------------------------------------------------------------------------------

	// Parses translate.lua ("key = \"value\"") into a flat lookup so generated quests carry real text.
	void LoadLocaleTable(const FString& TranslatePath, TMap<FString, FString>& OutTable)
	{
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *TranslatePath))
		{
			return;
		}
		for (const FString& Raw : Lines)
		{
			FString Line = Raw.TrimStartAndEnd();
			int32 Equals = INDEX_NONE;
			if (Line.IsEmpty() || Line.StartsWith(TEXT("--")) || !Line.FindChar(TEXT('='), Equals))
			{
				continue;
			}
			const FString Key = Line.Left(Equals).TrimStartAndEnd();
			FString Value = Line.Mid(Equals + 1).TrimStartAndEnd();
			if (!Value.StartsWith(TEXT("\"")))
			{
				continue; // "key = {}" table declarations carry no text
			}
			Value = Value.Mid(1);
			const int32 LastQuote = Value.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			if (LastQuote != INDEX_NONE)
			{
				Value = Value.Left(LastQuote);
			}
			OutTable.Add(Key, Value);
		}
	}

	// Splits on top-level ".." so a concatenated message can be resolved piece by piece.
	TArray<FString> SplitConcatenation(const FString& Expression)
	{
		TArray<FString> Parts;
		int32 Depth = 0;
		bool bInString = false;
		TCHAR StringQuote = TEXT('"');
		FString Current;
		for (int32 Index = 0; Index < Expression.Len(); ++Index)
		{
			const TCHAR Char = Expression[Index];
			if (bInString)
			{
				Current.AppendChar(Char);
				if (Char == TEXT('\\') && Index + 1 < Expression.Len())
				{
					Current.AppendChar(Expression[++Index]);
					continue;
				}
				if (Char == StringQuote) { bInString = false; }
				continue;
			}
			if (Char == TEXT('"') || Char == TEXT('\''))
			{
				bInString = true;
				StringQuote = Char;
				Current.AppendChar(Char);
				continue;
			}
			if (Char == TEXT('(') || Char == TEXT('[') || Char == TEXT('{')) { ++Depth; }
			else if (Char == TEXT(')') || Char == TEXT(']') || Char == TEXT('}')) { --Depth; }
			// ".." at top level joins two pieces; "..." inside a number is not a concern here.
			if (Depth == 0 && Char == TEXT('.') && Index + 1 < Expression.Len() &&
				Expression[Index + 1] == TEXT('.'))
			{
				Parts.Add(Current.TrimStartAndEnd());
				Current.Reset();
				++Index;
				continue;
			}
			Current.AppendChar(Char);
		}
		Parts.Add(Current.TrimStartAndEnd());
		Parts.RemoveAll([](const FString& Entry) { return Entry.IsEmpty(); });
		return Parts;
	}

	// Preserve complete display expressions; the runtime owns Lua concatenation and formatting.
	FString ResolveStaticTableConstants(const FString& Source, const TMap<FString, FString>& Locale);

	FString ResolveText(const FString& Argument, const TMap<FString, FString>& Locale, bool& bOutResolved)
	{
		bOutResolved = true;
		FString Value = Argument.TrimStartAndEnd();

		// Concatenation is not printf substitution, even when a locale string contains "%s".
		if (const TArray<FString> ConcatParts = SplitConcatenation(Value); ConcatParts.Num() > 1)
		{
			return FString::Printf(TEXT("{=%s}"), *ResolveStaticTableConstants(Value, Locale));
		}

		FString FormatArguments;
		if (ExtractCallArguments(Value, TEXT("string.format"), FormatArguments))
		{
			// Keep conversion types, flags and byte precision intact for the runtime formatter.
			return FString::Printf(TEXT("{=%s}"), *ResolveStaticTableConstants(Value, Locale));
		}

		FString Text;
		bool bHaveText = false;
		if (Value.StartsWith(TEXT("\"")) && Value.EndsWith(TEXT("\"")) && Value.Len() >= 2)
		{
			Text = Value.Mid(1, Value.Len() - 2);
			bHaveText = true;
		}
		else if (const FString* Found = Locale.Find(Value))
		{
			Text = *Found;
			bHaveText = true;
		}
		if (!bHaveText)
		{
			bOutResolved = false;
			return Value;
		}

		return Text;
	}

	// Player-visible text. Anything that cannot be resolved to a string becomes a runtime expression
	// token rather than being shown as-is: a dialog must never display raw Lua, so an unsupported
	// expression renders as nothing instead of "mob_name(special.levelup_quest[lev][1])..".
	FString ResolveDisplayText(const FString& Argument, const TMap<FString, FString>& Locale)
	{
		bool bResolved = false;
		const FString Text = ResolveText(Argument, Locale, bResolved);
		if (bResolved)
		{
			return Text;
		}
		return FString::Printf(TEXT("{=%s}"), *Argument.TrimStartAndEnd());
	}

	// locale.lua is an alias/config layer over translate.lua. It contains both strings and numeric
	// constants, plus simple concatenations of already-defined values. Resolve those declarations in
	// dependency order so quest locals can consume them exactly like ordinary translated keys.
	void LoadLocaleAliases(const FString& FilePath, TMap<FString, FString>& InOutLocale)
	{
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *FilePath))
		{
			return;
		}
		TArray<TPair<FString, FString>> Pending;
		for (const FString& Raw : Lines)
		{
			const FString Line = NormalizeStatement(Raw);
			int32 EqualsIndex = INDEX_NONE;
			if (!Line.FindChar(TEXT('='), EqualsIndex) || EqualsIndex <= 0)
			{
				continue;
			}
			const FString Key = Line.Left(EqualsIndex).TrimStartAndEnd();
			const FString Value = Line.Mid(EqualsIndex + 1).TrimStartAndEnd();
			if (Key.Contains(TEXT(".")) && !Value.IsEmpty() && Value != TEXT("{}"))
			{
				Pending.Emplace(Key, Value);
			}
		}

		for (int32 Pass = 0; Pass < 32 && !Pending.IsEmpty(); ++Pass)
		{
			bool bProgress = false;
			for (int32 Index = Pending.Num() - 1; Index >= 0; --Index)
			{
				FString Resolved;
				bool bResolved = true;
				for (const FString& Part : SplitConcatenation(Pending[Index].Value))
				{
					const FString Piece = Part.TrimStartAndEnd();
					if (Piece.Len() >= 2 &&
						((Piece.StartsWith(TEXT("\"")) && Piece.EndsWith(TEXT("\""))) ||
						 (Piece.StartsWith(TEXT("'")) && Piece.EndsWith(TEXT("'")))))
					{
						Resolved += Piece.Mid(1, Piece.Len() - 2);
					}
					else if (Piece.IsNumeric())
					{
						Resolved += Piece;
					}
					else if (const FString* Existing = InOutLocale.Find(Piece))
					{
						Resolved += *Existing;
					}
					else
					{
						bResolved = false;
						break;
					}
				}
				if (bResolved)
				{
					InOutLocale.Add(Pending[Index].Key, MoveTemp(Resolved));
					Pending.RemoveAtSwap(Index);
					bProgress = true;
				}
			}
			if (!bProgress) { break; }
		}
	}

	// Old scripts embed [ENTER] as the line break inside one say().
	TArray<FText> SplitDialogLines(const FString& Text)
	{
		// A locale's [ENTER] may occur inside a quoted runtime expression. Split its result instead.
		if (Text.Contains(TEXT("{="))) { return {FText::FromString(Text)}; }
		TArray<FString> Parts;
		Text.ParseIntoArray(Parts, TEXT("[ENTER]"), false);
		TArray<FText> Lines;
		for (const FString& Part : Parts)
		{
			Lines.Add(FText::FromString(Part.TrimEnd()));
		}
		return Lines;
	}

	bool ParseInt(const FString& Argument, int32& OutValue)
	{
		const FString Trimmed = Argument.TrimStartAndEnd();
		if (Trimmed.IsNumeric())
		{
			OutValue = FCString::Atoi(*Trimmed);
			return true;
		}
		return false;
	}

	// ---------------------------------------------------------------------------------------------
	// Lua constant tables (questlib.lua / locale.lua "special.*" definitions)
	// ---------------------------------------------------------------------------------------------

	// Parses one Lua value starting at Cursor, appending nodes to Out and returning the node index.
	// Handles nested { ... } constructors, numbers and quoted strings; anything else becomes an empty
	// leaf so a single unrecognised entry cannot derail the whole table.
	int32 ParseLuaValue(const FString& Source, int32& Cursor, TArray<FMT2QuestTableNode>& Out)
	{
		auto SkipTrivia = [&Source, &Cursor]()
		{
			while (Cursor < Source.Len())
			{
				if (FChar::IsWhitespace(Source[Cursor])) { ++Cursor; continue; }
				// Line comment
				if (Source[Cursor] == TEXT('-') && Cursor + 1 < Source.Len() && Source[Cursor + 1] == TEXT('-'))
				{
					while (Cursor < Source.Len() && Source[Cursor] != TEXT('\n')) { ++Cursor; }
					continue;
				}
				break;
			}
		};

		SkipTrivia();
		const int32 NodeIndex = Out.AddDefaulted();
		if (Cursor >= Source.Len())
		{
			return NodeIndex;
		}

		// Nested table
		if (Source[Cursor] == TEXT('{'))
		{
			++Cursor;
			Out[NodeIndex].bIsTable = true;
			while (Cursor < Source.Len())
			{
				SkipTrivia();
				if (Cursor < Source.Len() && Source[Cursor] == TEXT('}'))
				{
					++Cursor;
					break;
				}
				FString NamedKey;
				int32 ExplicitLuaIndex = INDEX_NONE;
				if (Source[Cursor] == TEXT('['))
				{
					const int32 KeyStart = ++Cursor;
					while (Cursor < Source.Len() && Source[Cursor] != TEXT(']')) { ++Cursor; }
					FString Key = Source.Mid(KeyStart, Cursor - KeyStart).TrimStartAndEnd();
					if (Cursor < Source.Len()) { ++Cursor; }
					SkipTrivia();
					if (Cursor < Source.Len() && Source[Cursor] == TEXT('=')) { ++Cursor; }
					if (Key.IsNumeric())
					{
						ExplicitLuaIndex = FCString::Atoi(*Key);
					}
					else
					{
						Key.RemoveFromStart(TEXT("\""));
						Key.RemoveFromEnd(TEXT("\""));
						Key.RemoveFromStart(TEXT("'"));
						Key.RemoveFromEnd(TEXT("'"));
						NamedKey = Key;
					}
				}
				else if (FChar::IsAlpha(Source[Cursor]) || Source[Cursor] == TEXT('_'))
				{
					const int32 SavedCursor = Cursor;
					while (Cursor < Source.Len() &&
						(FChar::IsAlnum(Source[Cursor]) || Source[Cursor] == TEXT('_'))) { ++Cursor; }
					const FString Candidate = Source.Mid(SavedCursor, Cursor - SavedCursor);
					SkipTrivia();
					if (Cursor < Source.Len() && Source[Cursor] == TEXT('='))
					{
						NamedKey = Candidate;
						++Cursor;
					}
					else
					{
						Cursor = SavedCursor;
					}
				}
				const int32 ChildIndex = ParseLuaValue(Source, Cursor, Out);
				if (!NamedKey.IsEmpty())
				{
					Out[NodeIndex].NamedChildren.Add(FName(*NamedKey), ChildIndex);
				}
				else if (ExplicitLuaIndex > 0)
				{
					const int32 PreviousCount = Out[NodeIndex].Children.Num();
					Out[NodeIndex].Children.SetNum(FMath::Max(PreviousCount, ExplicitLuaIndex));
					for (int32 EmptyIndex = PreviousCount;
						EmptyIndex < Out[NodeIndex].Children.Num(); ++EmptyIndex)
					{
						Out[NodeIndex].Children[EmptyIndex] = INDEX_NONE;
					}
					Out[NodeIndex].Children[ExplicitLuaIndex - 1] = ChildIndex;
				}
				else
				{
					Out[NodeIndex].Children.Add(ChildIndex);
				}
				SkipTrivia();
				if (Cursor < Source.Len() && Source[Cursor] == TEXT(',')) { ++Cursor; }
			}
			return NodeIndex;
		}

		// Quoted string
		if (Source[Cursor] == TEXT('"') || Source[Cursor] == TEXT('\''))
		{
			const TCHAR Quote = Source[Cursor++];
			FString Text;
			while (Cursor < Source.Len() && Source[Cursor] != Quote)
			{
				if (Source[Cursor] == TEXT('\\') && Cursor + 1 < Source.Len()) { ++Cursor; }
				Text.AppendChar(Source[Cursor++]);
			}
			if (Cursor < Source.Len()) { ++Cursor; }
			Out[NodeIndex].Text = Text;
			return NodeIndex;
		}

		// Number or bare token
		const int32 Start = Cursor;
		while (Cursor < Source.Len() && Source[Cursor] != TEXT(',') &&
			Source[Cursor] != TEXT('}') && !FChar::IsWhitespace(Source[Cursor]))
		{
			++Cursor;
		}
		const FString Token = Source.Mid(Start, Cursor - Start).TrimStartAndEnd();
		if (Token == TEXT("true") || Token == TEXT("false"))
		{
			Out[NodeIndex].bIsBoolean = true;
			Out[NodeIndex].Number = Token == TEXT("true") ? 1.0 : 0.0;
		}
		else if (Token == TEXT("nil")) { Out[NodeIndex].bIsNil = true; }
		else if (Token.IsNumeric())
		{
			Out[NodeIndex].bIsNumber = true;
			Out[NodeIndex].Number = FCString::Atod(*Token);
		}
		else
		{
			Out[NodeIndex].Text = Token;
		}
		return NodeIndex;
	}

	// Scans a Lua file for "<name> = {" definitions and converts each into a table.
	void ParseLuaTables(const FString& FilePath, TArray<FMT2QuestTable>& OutTables)
	{
		FString Source;
		if (!FFileHelper::LoadFileToString(Source, *FilePath))
		{
			return;
		}
		int32 Cursor = 0;
		while (Cursor < Source.Len())
		{
			const int32 Assign = Source.Find(TEXT("="), ESearchCase::CaseSensitive, ESearchDir::FromStart, Cursor);
			if (Assign == INDEX_NONE)
			{
				break;
			}
			// The name is the identifier immediately before '='.
			int32 NameEnd = Assign - 1;
			while (NameEnd >= 0 && FChar::IsWhitespace(Source[NameEnd])) { --NameEnd; }
			int32 NameStart = NameEnd;
			while (NameStart >= 0 &&
				(FChar::IsAlnum(Source[NameStart]) || Source[NameStart] == TEXT('_') ||
				 Source[NameStart] == TEXT('.')))
			{
				--NameStart;
			}
			const FString Name = Source.Mid(NameStart + 1, NameEnd - NameStart).TrimStartAndEnd();

			// Only "<name> = {" starts a table definition.
			int32 ValueCursor = Assign + 1;
			while (ValueCursor < Source.Len() && FChar::IsWhitespace(Source[ValueCursor])) { ++ValueCursor; }
			// Dotted names are the `special.*` / `locale.*` families; the bare ones are the ALL_CAPS
			// lists at the bottom of questlib.lua (WARRIOR1_NPC_LIST and friends), which the skill
			// teacher checks index. Any other bare assignment is a local and is skipped.
			const bool bIsConstantName = !Name.Contains(TEXT(".")) && Name == Name.ToUpper();
			if (ValueCursor >= Source.Len() || Source[ValueCursor] != TEXT('{') || Name.IsEmpty() ||
				!(Name.Contains(TEXT(".")) || bIsConstantName))
			{
				Cursor = Assign + 1;
				continue;
			}

			FMT2QuestTable Table;
			Table.Name = FName(*Name);
			ParseLuaValue(Source, ValueCursor, Table.Nodes);
			Cursor = ValueCursor;
			// An empty "{}" declaration carries no data; a later assignment usually fills it in.
			if (Table.Nodes.Num() > 1)
			{
				OutTables.RemoveAll([&Table](const FMT2QuestTable& Existing)
				{
					return Existing.Name == Table.Name;
				});
				OutTables.Add(MoveTemp(Table));
			}
		}
	}

	// ---------------------------------------------------------------------------------------------
	// Parsed script model
	// ---------------------------------------------------------------------------------------------

	struct FParsedTrigger
	{
		FString Spec;            // raw text between "when" and "begin"
		int32 BodyStart = 0;     // first line inside the block
		int32 BodyEnd = 0;       // line of the closing "end" (exclusive)
		bool bMultilineHeader = false;
	};

	struct FParsedState
	{
		FString Name;
		TArray<FParsedTrigger> Triggers;
	};

	// A `function name(a, b) ... end` block defined inside the quest. The old scripts use these as
	// plain subroutines - there are no closures and no return values other than constant tables - so a
	// call site can be translated by inlining the body.
	struct FParsedFunction
	{
		FString Name;
		TArray<FString> Parameters;
		int32 BodyStart = INDEX_NONE;
		int32 BodyEnd = INDEX_NONE;
	};

	struct FParsedQuest
	{
		FString Name;
		TArray<FParsedState> States;
		TArray<FParsedFunction> Functions;
		// Quest-scope assignments (GUARD = 20345) that live outside any when-block.
		TArray<TPair<FString, FString>> Constants;
		// Bare Lua assignments inside triggers are quest globals; `local` assignments are not.
		TSet<FString> GlobalVariables;
	};

	void CollectQuestGlobalAssignments(
		const TArray<FString>& Lines, int32 StartLine, int32 EndLine, TSet<FString>& OutGlobals)
	{
		TSet<FString> Locals;
		for (int32 LineIndex = StartLine; LineIndex < EndLine && Lines.IsValidIndex(LineIndex); ++LineIndex)
		{
			FString Statement = NormalizeStatement(Lines[LineIndex]);
			const bool bLocal = Statement.StartsWith(TEXT("local "));
			if (bLocal) { Statement.RemoveFromStart(TEXT("local ")); }
			int32 EqualsIndex = INDEX_NONE;
			if (!Statement.FindChar(TEXT('='), EqualsIndex) || EqualsIndex <= 0 ||
				Statement[EqualsIndex - 1] == TEXT('=') || Statement[EqualsIndex - 1] == TEXT('>') ||
				Statement[EqualsIndex - 1] == TEXT('<') || Statement[EqualsIndex - 1] == TEXT('~') ||
				Statement[EqualsIndex - 1] == TEXT('!') ||
				(EqualsIndex + 1 < Statement.Len() && Statement[EqualsIndex + 1] == TEXT('=')))
			{
				continue;
			}
			const FString Name = Statement.Left(EqualsIndex).TrimStartAndEnd();
			if (Name.IsEmpty() || Name.Contains(TEXT(".")) || Name.Contains(TEXT("[")) ||
				Name.Contains(TEXT(" ")) || Name.Contains(TEXT(",")))
			{
				continue;
			}
			if (bLocal) { Locals.Add(Name); }
			else if (!Locals.Contains(Name)) { OutGlobals.Add(Name); }
		}
	}

	// questlib.lua is executed once before every quest in the old server. Import its column-zero
	// scalar declarations as shared constants; indented assignments belong to function bodies and
	// are deliberately ignored. Tables are handled separately by ParseLuaTables.
	void ParseLuaLibraryScalars(const FString& Filename, TMap<FString, FString>& OutConstants)
	{
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *Filename))
		{
			return;
		}
		for (const FString& Raw : Lines)
		{
			if (Raw.IsEmpty() || FChar::IsWhitespace(Raw[0]))
			{
				continue;
			}
			const FString Line = NormalizeStatement(Raw);
			int32 EqualsIndex = INDEX_NONE;
			if (!Line.FindChar(TEXT('='), EqualsIndex) || EqualsIndex <= 0 ||
				(EqualsIndex + 1 < Line.Len() && Line[EqualsIndex + 1] == TEXT('=')))
			{
				continue;
			}
			const FString Name = Line.Left(EqualsIndex).TrimStartAndEnd();
			const FString Value = Line.Mid(EqualsIndex + 1).TrimStartAndEnd();
			if (Name.IsEmpty() || Value.IsEmpty() || Name.Contains(TEXT(" ")) ||
				Name.Contains(TEXT(".")) || Name.Contains(TEXT("[")) || Name.Contains(TEXT("(")))
			{
				continue;
			}
			const bool bLiteral = Value.IsNumeric() ||
				((Value.StartsWith(TEXT("\"")) && Value.EndsWith(TEXT("\""))) ||
				 (Value.StartsWith(TEXT("'")) && Value.EndsWith(TEXT("'"))));
			if (bLiteral)
			{
				OutConstants.FindOrAdd(Name) = Value;
			}
		}
	}

	// Pulls quest/state/when structure out of a script. Bodies stay as raw lines for the translator.
	bool ParseQuestScript(const TArray<FString>& Lines, FParsedQuest& OutQuest)
	{
		bool bFoundQuest = false;
		for (int32 Index = 0; Index < Lines.Num(); ++Index)
		{
			const FString Line = NormalizeStatement(Lines[Index]);
			TArray<FString> Words;
			CollectKeywords(Line, Words);

			// "define NAME VALUE" is the old quest preprocessor's constant form (it sits above the
			// quest block, so it is collected before anything else).
			if (Words.Num() >= 2 && Words[0] == TEXT("define"))
			{
				FString Rest = Line;
				Rest.RemoveFromStart(TEXT("define"));
				Rest.TrimStartAndEndInline();
				int32 SpaceIndex = INDEX_NONE;
				if (Rest.FindChar(TEXT(' '), SpaceIndex) || Rest.FindChar(TEXT('	'), SpaceIndex))
				{
					const FString Name = Rest.Left(SpaceIndex).TrimStartAndEnd();
					const FString Value = Rest.Mid(SpaceIndex + 1).TrimStartAndEnd();
					if (!Name.IsEmpty() && !Value.IsEmpty())
					{
						OutQuest.Constants.Emplace(Name, Value);
					}
				}
				continue;
			}

			if (!bFoundQuest && Words.Num() >= 3 && Words[0] == TEXT("quest") && Words.Last() == TEXT("begin"))
			{
				OutQuest.Name = Words[1];
				bFoundQuest = true;
				continue;
			}
			// A function definition: record it and skip its body, so its statements are not mistaken for
			// the enclosing state's own.
			if (Words.Num() >= 2 && Words[0] == TEXT("function"))
			{
				int32 UnusedElse = INDEX_NONE;
				const int32 EndLine = FindMatchingEnd(Lines, Index, UnusedElse);
				if (EndLine == INDEX_NONE)
				{
					continue;
				}
				FParsedFunction Function;
				Function.Name = Words[1];
				Function.BodyStart = Index + 1;
				Function.BodyEnd = EndLine;
				FString Arguments;
				if (ExtractCallArguments(Line, *Function.Name, Arguments))
				{
					for (const FString& Parameter : SplitArguments(Arguments))
					{
						const FString Trimmed = Parameter.TrimStartAndEnd();
						if (!Trimmed.IsEmpty())
						{
							Function.Parameters.Add(Trimmed);
						}
					}
				}
				OutQuest.Functions.Add(MoveTemp(Function));
				Index = EndLine;
				continue;
			}
			if (Words.Num() >= 3 && Words[0] == TEXT("state") && Words.Last() == TEXT("begin"))
			{
				FParsedState& State = OutQuest.States.AddDefaulted_GetRef();
				State.Name = Words[1];
				continue;
			}
			if (Words.Num() >= 1 && Words[0] == TEXT("when"))
			{
				FString Header = Line;
				int32 HeaderEnd = Index;
				while (!LineHasWord(Header, TEXT("begin")) && HeaderEnd + 1 < Lines.Num())
				{
					const FString Continuation = NormalizeStatement(Lines[HeaderEnd + 1]);
					// Do not absorb a later trigger/state when a malformed header has no begin.
					if (LineHasWord(Continuation, TEXT("when")) || LineHasWord(Continuation, TEXT("state")) ||
						Continuation == TEXT("end")) { break; }
					Header += TEXT(" ") + Continuation;
					++HeaderEnd;
				}
				if (!LineHasWord(Header, TEXT("begin"))) { continue; }
				if (OutQuest.States.IsEmpty())
				{
					OutQuest.States.AddDefaulted_GetRef().Name = TEXT("start");
				}
				int32 ElseLine = INDEX_NONE;
				const int32 EndLine = FindMatchingEnd(Lines, HeaderEnd, ElseLine);
				if (EndLine == INDEX_NONE)
				{
					continue;
				}
				FParsedTrigger& Trigger = OutQuest.States.Last().Triggers.AddDefaulted_GetRef();
				// Spec is everything between "when" and the trailing "begin".
				FString Spec = Header;
				Spec.RemoveFromStart(TEXT("when"));
				const int32 BeginIndex = Spec.Find(TEXT("begin"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
				if (BeginIndex != INDEX_NONE)
				{
					Spec = Spec.Left(BeginIndex);
				}
				Trigger.Spec = Spec.TrimStartAndEnd();
				Trigger.bMultilineHeader = HeaderEnd != Index;
				Trigger.BodyStart = HeaderEnd + 1;
				Trigger.BodyEnd = EndLine;
				CollectQuestGlobalAssignments(
					Lines, Trigger.BodyStart, Trigger.BodyEnd, OutQuest.GlobalVariables);
				Index = EndLine; // skip the block we just captured
				continue;
			}

			// Anything else at quest/state level that assigns a plain name is a constant the triggers
			// may reference (NPC vnums, item ids, thresholds).
			{
				int32 EqualsIndex = INDEX_NONE;
				if (Line.FindChar(TEXT('='), EqualsIndex) && EqualsIndex > 0 &&
					Line[EqualsIndex - 1] != TEXT('=') && Line[EqualsIndex - 1] != TEXT('>') &&
					Line[EqualsIndex - 1] != TEXT('<') && Line[EqualsIndex - 1] != TEXT('~') &&
					Line[EqualsIndex - 1] != TEXT('!') &&
					(EqualsIndex + 1 >= Line.Len() || Line[EqualsIndex + 1] != TEXT('=')))
				{
					FString Name = Line.Left(EqualsIndex).TrimStartAndEnd();
					Name.RemoveFromStart(TEXT("local "));
					Name.TrimStartAndEndInline();
					const FString Value = Line.Mid(EqualsIndex + 1).TrimStartAndEnd();
					if (!Name.IsEmpty() && !Value.IsEmpty() &&
						!Name.Contains(TEXT(".")) && !Name.Contains(TEXT("[")) &&
						!Name.Contains(TEXT(" ")) && !Name.Contains(TEXT(",")))
					{
						OutQuest.Constants.Emplace(Name, Value);
					}
				}
			}
		}
		return bFoundQuest;
	}

	// Maps an old trigger spec ("20017.chat.gameforge.x._10_npcChat", "login", "1001.kill") to an event.
	// Splits a raw when-spec into its event specs and its optional "with <condition>" clause.
	// `when login or levelup or enter with pc.level >= 90` -> ["login","levelup","enter"] + the condition.
	void SplitTriggerSpec(const FString& Spec, TArray<FString>& OutSpecs, FString& OutCondition)
	{
		FString Working = Spec.TrimStartAndEnd();
		Working.ReplaceInline(TEXT("\t"), TEXT(" "));
		while (Working.Contains(TEXT("  "))) { Working.ReplaceInline(TEXT("  "), TEXT(" ")); }
		OutCondition.Reset();

		// " with " separates the gate; match it as a whole word.
		const int32 WithIndex = Working.Find(TEXT(" with "), ESearchCase::CaseSensitive);
		if (WithIndex != INDEX_NONE)
		{
			OutCondition = Working.Mid(WithIndex + 6).TrimStartAndEnd();
			Working = Working.Left(WithIndex).TrimStartAndEnd();
		}

		// The remainder may list several events joined by " or ".
		int32 SearchStart = 0;
		while (true)
		{
			const int32 OrIndex = Working.Find(TEXT(" or "), ESearchCase::CaseSensitive,
				ESearchDir::FromStart, SearchStart);
			if (OrIndex == INDEX_NONE)
			{
				OutSpecs.Add(Working.Mid(SearchStart).TrimStartAndEnd());
				break;
			}
			OutSpecs.Add(Working.Mid(SearchStart, OrIndex - SearchStart).TrimStartAndEnd());
			SearchStart = OrIndex + 4;
		}
		OutSpecs.RemoveAll([](const FString& Entry) { return Entry.IsEmpty(); });
	}

	bool ParseTriggerSpec(
		const FString& Spec, const TMap<FString, FString>& Locale,
		EMT2QuestEvent& OutEvent, int32& OutVnum, FText& OutChatOption,
		const TMap<FString, int32>* NamedVnums = nullptr, FName* OutTriggerName = nullptr)
	{
		if (OutTriggerName) { *OutTriggerName = NAME_None; }
		OutVnum = 0;
		OutChatOption = FText::GetEmpty();

		// Drop any "with <condition>" suffix; those extra gates are reported, not converted.
		FString Working = Spec;
		int32 WithIndex = INDEX_NONE;
		{
			TArray<FString> Words;
			CollectKeywords(Working, Words);
			if (Words.Contains(TEXT("with")))
			{
				WithIndex = Working.Find(TEXT(" with "), ESearchCase::CaseSensitive);
				if (WithIndex != INDEX_NONE)
				{
					Working = Working.Left(WithIndex);
				}
			}
		}
		Working = Working.TrimStartAndEnd();

		TArray<FString> Parts;
		Working.ParseIntoArray(Parts, TEXT("."), true);
		if (Parts.IsEmpty())
		{
			return false;
		}

		int32 PartIndex = 0;
		if (Parts[0].IsNumeric())
		{
			OutVnum = FCString::Atoi(*Parts[0]);
			PartIndex = 1;
		}
		// `when MOB1_1.kill` names a `define`d constant instead of the raw vnum; resolve it.
		else if (NamedVnums && NamedVnums->Contains(Parts[0]))
		{
			OutVnum = (*NamedVnums)[Parts[0]];
			PartIndex = 1;
		}
		else if (Parts.Num() > 1 && (Parts[1] == TEXT("target") || Parts[1] == TEXT("chat") ||
			Parts[1] == TEXT("timer") || Parts[1] == TEXT("server_timer")))
		{
			PartIndex = 1; // named target/chat/timer form
		}
		if (!Parts.IsValidIndex(PartIndex))
		{
			return false;
		}

		const FString EventName = Parts[PartIndex].TrimStartAndEnd();
		static const TMap<FString, EMT2QuestEvent> EventMap = {
			{TEXT("click"),   EMT2QuestEvent::Click},
			{TEXT("chat"),    EMT2QuestEvent::Chat},
			{TEXT("kill"),    EMT2QuestEvent::Kill},
			{TEXT("login"),   EMT2QuestEvent::Login},
			{TEXT("logout"),  EMT2QuestEvent::Logout},
			{TEXT("levelup"), EMT2QuestEvent::LevelUp},
			{TEXT("enter"),   EMT2QuestEvent::Enter},
			{TEXT("leave"),   EMT2QuestEvent::Leave},
			{TEXT("button"),  EMT2QuestEvent::Button},
			{TEXT("info"),    EMT2QuestEvent::Info},
			{TEXT("use"),     EMT2QuestEvent::ItemUse},
			{TEXT("take"),    EMT2QuestEvent::ItemTake},
			{TEXT("timer"),   EMT2QuestEvent::Timer},
			{TEXT("letter"),  EMT2QuestEvent::Letter},
			{TEXT("target"),  EMT2QuestEvent::Target},
			{TEXT("arrive"),  EMT2QuestEvent::Arrive},
			{TEXT("unmount"), EMT2QuestEvent::Unmount},
			{TEXT("party_kill"), EMT2QuestEvent::PartyKill},
			{TEXT("server_timer"), EMT2QuestEvent::ServerTimer},
		};
		const EMT2QuestEvent* Found = EventMap.Find(EventName);
		if (!Found)
		{
			return false;
		}
		OutEvent = *Found;
		if (OutEvent == EMT2QuestEvent::Target)
		{
			// The legacy target event carries a marker name AND a verb.
			if (PartIndex != 1 || Parts.Num() != 3) { return false; }
			if (Parts[2] == TEXT("click")) { OutEvent = EMT2QuestEvent::TargetClick; }
			else if (Parts[2] == TEXT("arrive")) { OutEvent = EMT2QuestEvent::Arrive; }
			else if (Parts[2] == TEXT("die")) { OutEvent = EMT2QuestEvent::TargetDie; }
			else { return false; }
		}

		// Timers and quest targets identify themselves by the name in front of the event
		// ("mytimer.server_timer", "teacher1.target.arrive") rather than by a vnum, so carry that name
		// onto the trigger for name matching.
		if ((OutEvent == EMT2QuestEvent::Timer || OutEvent == EMT2QuestEvent::ServerTimer ||
			OutEvent == EMT2QuestEvent::TargetClick || OutEvent == EMT2QuestEvent::Arrive ||
			OutEvent == EMT2QuestEvent::TargetDie) &&
			OutTriggerName && PartIndex > 0 && Parts.IsValidIndex(PartIndex - 1))
		{
			*OutTriggerName = FName(*Parts[PartIndex - 1]);
			OutVnum = 0;
		}

		// A chat trigger's remaining parts are the localized menu label.
		if (OutEvent == EMT2QuestEvent::Chat && Parts.Num() > PartIndex + 1)
		{
			TArray<FString> KeyParts;
			for (int32 Index = PartIndex + 1; Index < Parts.Num(); ++Index)
			{
				KeyParts.Add(Parts[Index]);
			}
			const FString Key = FString::Join(KeyParts, TEXT("."));
			OutChatOption = FText::FromString(ResolveDisplayText(Key, Locale));
		}
		return true;
	}

	// ---------------------------------------------------------------------------------------------
	// Statement -> node translation
	// ---------------------------------------------------------------------------------------------

	struct FTranslateState
	{
		const TMap<FString, FString>* Locale = nullptr;
		UObject* Outer = nullptr;
		FString ScriptName;
		FString QuestName;
		FMT2QuestImportResult* Result = nullptr;
		// Locals the block has assigned so far. An expression may only reference variables we have
		// actually captured, otherwise it would silently read 0 at runtime.
		TSet<FString> KnownVariables;
		// Script functions this quest defines, and the ones currently being inlined - a script that
		// calls itself (directly or in a cycle) must not expand forever.
		const TArray<FParsedFunction>* Functions = nullptr;
		const TSet<FString>* GlobalVariables = nullptr;
		TSet<FString> InliningFunctions;
		int32 GeneratedTemporaryIndex = 0;
		int32 LoopDepth = 0;
	};

	bool FindFunctionCallSpan(
		const FString& Source, const FString& FunctionName, int32& OutStart, int32& OutEnd,
		FString& OutArguments)
	{
		int32 SearchStart = 0;
		while (SearchStart < Source.Len())
		{
			const int32 NameStart = Source.Find(
				FunctionName, ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchStart);
			if (NameStart == INDEX_NONE) { return false; }
			SearchStart = NameStart + FunctionName.Len();
			bool bQuotedName = false;
			TCHAR NameQuote = 0;
			for (int32 Prefix = 0; Prefix < NameStart; ++Prefix)
			{
				const TCHAR Ch = Source[Prefix];
				if (bQuotedName && Ch == TEXT('\\')) { ++Prefix; continue; }
				if (bQuotedName && Ch == NameQuote) { bQuotedName = false; }
				else if (!bQuotedName && (Ch == TEXT('\'') || Ch == TEXT('"'))) { bQuotedName = true; NameQuote = Ch; }
			}
			if (bQuotedName) { continue; }
			if (NameStart > 0)
			{
				const TCHAR Previous = Source[NameStart - 1];
				if (FChar::IsAlnum(Previous) || Previous == TEXT('_') || Previous == TEXT('.')) { continue; }
			}
			int32 OpenParen = SearchStart;
			while (OpenParen < Source.Len() && FChar::IsWhitespace(Source[OpenParen])) { ++OpenParen; }
			if (OpenParen >= Source.Len() || Source[OpenParen] != TEXT('(')) { continue; }

			int32 Depth = 0;
			bool bInString = false;
			TCHAR Quote = TEXT('\0');
			for (int32 Cursor = OpenParen; Cursor < Source.Len(); ++Cursor)
			{
				const TCHAR Char = Source[Cursor];
				if (bInString)
				{
					if (Char == TEXT('\\')) { ++Cursor; continue; }
					if (Char == Quote) { bInString = false; }
					continue;
				}
				if (Char == TEXT('"') || Char == TEXT('\'')) { bInString = true; Quote = Char; }
				else if (Char == TEXT('(')) { ++Depth; }
				else if (Char == TEXT(')') && --Depth == 0)
				{
					OutStart = NameStart;
					OutEnd = Cursor + 1;
					OutArguments = Source.Mid(OpenParen + 1, Cursor - OpenParen - 1);
					return true;
				}
			}
			return false;
		}
		return false;
	}

	bool ParseInt64(const FString& Argument, int64& OutValue)
	{
		const FString Trimmed = Argument.TrimStartAndEnd();
		return Trimmed.IsNumeric() && LexTryParseString(OutValue, *Trimmed);
	}

	FString SubstituteFunctionParameter(
		const FString& Source, const FString& Parameter, const FString& Argument)
	{
		FString Result;
		bool bInString = false;
		TCHAR Quote = TEXT('\0');
		for (int32 Index = 0; Index < Source.Len();)
		{
			const TCHAR Char = Source[Index];
			if (bInString)
			{
				Result.AppendChar(Char);
				if (Char == TEXT('\\') && Index + 1 < Source.Len()) { Result.AppendChar(Source[++Index]); }
				else if (Char == Quote) { bInString = false; }
				++Index;
				continue;
			}
			if (Char == TEXT('"') || Char == TEXT('\''))
			{
				bInString = true;
				Quote = Char;
				Result.AppendChar(Char);
				++Index;
				continue;
			}
			if (FChar::IsAlpha(Char) || Char == TEXT('_'))
			{
				int32 TokenEnd = Index + 1;
				while (TokenEnd < Source.Len() &&
					(FChar::IsAlnum(Source[TokenEnd]) || Source[TokenEnd] == TEXT('_'))) { ++TokenEnd; }
				const FString Token = Source.Mid(Index, TokenEnd - Index);
				const bool bMemberName = Index > 0 && Source[Index - 1] == TEXT('.');
				Result += Token == Parameter && !bMemberName
					? FString::Printf(TEXT("(%s)"), *Argument)
					: Token;
				Index = TokenEnd;
				continue;
			}
			Result.AppendChar(Char);
			++Index;
		}
		return Result;
	}

	// Inline only referentially transparent helpers of the form `return <expression>`. This preserves
	// Lua helper reuse without introducing a runtime interpreter or duplicating stateful side effects.
	FString ExpandPureFunctionCalls(
		const TArray<FString>& Lines, const FString& Source, const FTranslateState& State)
	{
		if (!State.Functions) { return Source; }
		FString Expanded = Source;
		for (int32 Pass = 0; Pass < 16; ++Pass)
		{
			bool bChanged = false;
			for (const FParsedFunction& Function : *State.Functions)
			{
				FString ReturnExpression;
				int32 StatementCount = 0;
				for (int32 LineIndex = Function.BodyStart;
					LineIndex < Function.BodyEnd && Lines.IsValidIndex(LineIndex); ++LineIndex)
				{
					FString Statement = NormalizeStatement(Lines[LineIndex]);
					if (Statement.IsEmpty()) { continue; }
					++StatementCount;
					if (StatementCount == 1 && Statement.RemoveFromStart(TEXT("return")))
					{
						ReturnExpression = Statement.TrimStartAndEnd();
					}
				}
				if (StatementCount != 1 || ReturnExpression.IsEmpty()) { continue; }

				const FString Names[] = {
					State.QuestName + TEXT(".") + Function.Name,
					State.ScriptName + TEXT(".") + Function.Name,
					Function.Name};
				for (const FString& Name : Names)
				{
					int32 CallStart = INDEX_NONE;
					int32 CallEnd = INDEX_NONE;
					FString Arguments;
					if (!FindFunctionCallSpan(Expanded, Name, CallStart, CallEnd, Arguments)) { continue; }
					const TArray<FString> ArgumentList = SplitArguments(Arguments);
					if (ArgumentList.Num() != Function.Parameters.Num()) { continue; }
					// Textual substitution can duplicate/discard an argument. Only immutable scalar
					// literals are safe; reads and calls must use frame-based argument capture.
					bool bLiteralArguments = true;
					for (const FString& Argument : ArgumentList)
					{
						const FString Value = Argument.TrimStartAndEnd();
						MT2QuestExpressionSyntax::FParser Literal(Value);
						const auto LiteralTree = Literal.Expression();
						bLiteralArguments &= Literal.bOk && Literal.Cursor == Literal.Tokens.Num() && LiteralTree &&
							LiteralTree->Kind == MT2QuestExpressionSyntax::EKind::Leaf && Literal.Tokens.Num() == 1 &&
							(Value.IsNumeric() || Value == TEXT("true") || Value == TEXT("false") ||
							Value == TEXT("nil") || (Value.Len() >= 2 &&
								((Value[0] == TEXT('"') && Value[Value.Len() - 1] == TEXT('"')) ||
								 (Value[0] == TEXT('\'') && Value[Value.Len() - 1] == TEXT('\'')))));
					}
					if (!bLiteralArguments) { continue; }
					FString Replacement = ReturnExpression;
					for (int32 ParameterIndex = 0; ParameterIndex < Function.Parameters.Num(); ++ParameterIndex)
					{
						Replacement = SubstituteFunctionParameter(
							Replacement, Function.Parameters[ParameterIndex], ArgumentList[ParameterIndex]);
					}
					Expanded = Expanded.Left(CallStart) + TEXT("(") + Replacement + TEXT(")") +
						Expanded.Mid(CallEnd);
					bChanged = true;
					break;
				}
				if (bChanged) { break; }
			}
			if (!bChanged) { break; }
		}
		return Expanded;
	}

	bool ResolveLegacyAffectType(const FString& Source, bool bPointType, int32& OutApplyType)
	{
		FString Name = Source.TrimStartAndEnd();
		if (ParseInt(Name, OutApplyType)) { return OutApplyType > 0; }
		if (Name.StartsWith(TEXT("apply."))) { Name.RightChopInline(6); }

		static const TMap<FString, int32> ApplyTypes = {
			{TEXT("MAX_HP"), 1}, {TEXT("CON"), 3}, {TEXT("INT"), 4}, {TEXT("STR"), 5},
			{TEXT("DEX"), 6}, {TEXT("ATT_SPEED"), 7}, {TEXT("MOV_SPEED"), 8},
			{TEXT("HP_REGEN"), 10}, {TEXT("CRITICAL_PCT"), 15},
			{TEXT("ATTBONUS_ANIMAL"), 18}, {TEXT("ATT_GRADE_BONUS"), 53},
			{TEXT("DEF_GRADE_BONUS"), 54}, {TEXT("MELEE_MAGIC_ATTBONUS_PER"), 86}};
		if (!bPointType)
		{
			if (const int32* Found = ApplyTypes.Find(Name)) { OutApplyType = *Found; return true; }
			return false;
		}

		// add_collect_point receives EPointTypes rather than EApplyTypes. Normalize the point onto the
		// equivalent apply channel used by the new game's unified equipment/status-effect calculator.
		static const TMap<FString, int32> PointTypes = {
			{TEXT("POINT_MAX_HP_PCT"), 69}, {TEXT("POINT_MAX_SP_PCT"), 70},
			{TEXT("POINT_MOV_SPEED"), 8}, {TEXT("POINT_ATT_SPEED"), 7},
			{TEXT("POINT_CASTING_SPEED"), 9}, {TEXT("POINT_MAGIC_DEF_GRADE"), 56},
			{TEXT("POINT_ATT_BONUS"), 64}, {TEXT("POINT_DEF_BONUS"), 65},
			{TEXT("POINT_ATTBONUS_WARRIOR"), 59}, {TEXT("POINT_ATTBONUS_ASSASSIN"), 60},
			{TEXT("POINT_ATTBONUS_SURA"), 61}, {TEXT("POINT_ATTBONUS_SHAMAN"), 62},
			{TEXT("POINT_ATTBONUS_MONSTER"), 63}, {TEXT("POINT_RESIST_WARRIOR"), 78},
			{TEXT("POINT_RESIST_ASSASSIN"), 79}, {TEXT("POINT_RESIST_SURA"), 80},
			{TEXT("POINT_RESIST_SHAMAN"), 81}};
		if (const int32* Found = PointTypes.Find(Name)) { OutApplyType = *Found; return true; }
		return false;
	}

	bool IsLiteralLuaTableConstructor(const FString& Source)
	{
		bool bInString = false;
		TCHAR Quote = TEXT('\0');
		for (int32 Index = 0; Index < Source.Len(); ++Index)
		{
			const TCHAR Char = Source[Index];
			if (bInString)
			{
				if (Char == TEXT('\\')) { ++Index; continue; }
				if (Char == Quote) { bInString = false; }
				continue;
			}
			if (Char == TEXT('"') || Char == TEXT('\''))
			{
				bInString = true;
				Quote = Char;
				continue;
			}
			if (FChar::IsAlpha(Char) || Char == TEXT('_'))
			{
				int32 TokenEnd = Index + 1;
				while (TokenEnd < Source.Len() &&
					(FChar::IsAlnum(Source[TokenEnd]) || Source[TokenEnd] == TEXT('_'))) { ++TokenEnd; }
				int32 Lookahead = TokenEnd;
				while (Lookahead < Source.Len() && FChar::IsWhitespace(Source[Lookahead])) { ++Lookahead; }
				if (Lookahead >= Source.Len() || Source[Lookahead] != TEXT('='))
				{
					return false;
				}
				Index = TokenEnd - 1;
			}
		}
		return !bInString;
	}

	// Locale constants are immutable strings too, but Lua writes them as bare identifiers inside table
	// constructors. Resolve those identifiers before parsing so tables such as map_warp's destination
	// menu remain static imported data instead of TODO nodes.
	FString ResolveStaticTableConstants(
		const FString& Source, const TMap<FString, FString>& Locale)
	{
		FString Resolved;
		bool bInString = false;
		TCHAR Quote = TEXT('\0');
		for (int32 Index = 0; Index < Source.Len();)
		{
			const TCHAR Char = Source[Index];
			if (bInString)
			{
				Resolved.AppendChar(Char);
				if (Char == TEXT('\\') && Index + 1 < Source.Len())
				{
					Resolved.AppendChar(Source[++Index]);
				}
				else if (Char == Quote)
				{
					bInString = false;
				}
				++Index;
				continue;
			}
			if (Char == TEXT('"') || Char == TEXT('\''))
			{
				bInString = true;
				Quote = Char;
				Resolved.AppendChar(Char);
				++Index;
				continue;
			}
			if (FChar::IsAlpha(Char) || Char == TEXT('_'))
			{
				const int32 TokenStart = Index++;
				while (Index < Source.Len() &&
					(FChar::IsAlnum(Source[Index]) || Source[Index] == TEXT('_') ||
					 Source[Index] == TEXT('.')))
				{
					++Index;
				}
				const FString Token = Source.Mid(TokenStart, Index - TokenStart);
				if (const FString* LocaleText = Locale.Find(Token))
				{
					FString Escaped = *LocaleText;
					Escaped.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
					Escaped.ReplaceInline(TEXT("\""), TEXT("\\\""));
					Escaped.ReplaceInline(TEXT("\r"), TEXT("\\r"));
					Escaped.ReplaceInline(TEXT("\n"), TEXT("\\n"));
					Resolved += TEXT("\"") + Escaped + TEXT("\"");
				}
				else
				{
					Resolved += Token;
				}
				continue;
			}
			Resolved.AppendChar(Char);
			++Index;
		}
		return Resolved;
	}

	// An expression converts when every call in it is bound in the evaluator. Bare identifiers remain
	// valid because Lua globals may be initialized by another branch, trigger, or questlib helper; the
	// runtime variable store provides Lua's nil value until such an assignment occurs.
	bool CanConvertExpression(const FString& Expression, const FTranslateState& State, FString& OutReason)
	{
		const FString Trimmed = Expression.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			OutReason = TEXT("empty");
			return false;
		}
		if (!FMT2QuestExpression::IsSupported(Trimmed, OutReason))
		{
			return false;
		}
		FString Current;
		auto CheckIdentifier = [&State](const FString& Name) -> bool
		{
			static const TSet<FString> Keywords = {
				TEXT("and"), TEXT("or"), TEXT("not"), TEXT("true"), TEXT("false"), TEXT("nil")};
			if (Name.IsEmpty() || Keywords.Contains(Name) || Name.IsNumeric() ||
				State.KnownVariables.Contains(Name) ||
				FMT2QuestExpression::IsKnownTable(Name) ||
				FMT2QuestExpression::IsSupportedFunction(Name))
			{
				return true;
			}
			// Remaining names are legal Lua globals or fields on a table-valued global.
			return true;
		};

		bool bInString = false;
		TCHAR StringQuote = TEXT('"');
		for (int32 Index = 0; Index <= Trimmed.Len(); ++Index)
		{
			const TCHAR Char = Index < Trimmed.Len() ? Trimmed[Index] : TEXT(' ');
			if (bInString)
			{
				if (Char == StringQuote) { bInString = false; }
				continue;
			}
			if (Char == TEXT('"') || Char == TEXT('\''))
			{
				bInString = true;
				StringQuote = Char;
				continue;
			}
			if (FChar::IsAlnum(Char) || Char == TEXT('_') || Char == TEXT('.'))
			{
				Current.AppendChar(Char);
				continue;
			}
			if (!Current.IsEmpty())
			{
				// A name followed by '(' is a call, already validated by IsSupported. The scripts often
				// put a space before the paren ("number (1,100)"), so look past whitespace.
				int32 Lookahead = Index;
				while (Lookahead < Trimmed.Len() && FChar::IsWhitespace(Trimmed[Lookahead])) { ++Lookahead; }
				const bool bIsCall = Lookahead < Trimmed.Len() && Trimmed[Lookahead] == TEXT('(');
				if (!bIsCall && !CheckIdentifier(Current))
				{
					return false;
				}
				Current.Reset();
			}
		}
		return true;
	}

	TArray<TObjectPtr<UMT2QuestNode>> TranslateBlock(
		const TArray<FString>& Lines, int32 Start, int32 End, FTranslateState& State);

	bool TryConvertStaticTableFunction(
		const TArray<FString>& Lines, const FString& CallExpression, const FString& VariableName,
		int32 SourceLine, FTranslateState& State, FMT2QuestTable& OutTable)
	{
		if (!State.Functions)
		{
			return false;
		}
		for (const FParsedFunction& Function : *State.Functions)
		{
			FString Arguments;
			const FString QuestQualified = State.QuestName + TEXT(".") + Function.Name;
			const FString ScriptQualified = State.ScriptName + TEXT(".") + Function.Name;
			if ((!ExtractCallArguments(CallExpression, *QuestQualified, Arguments) &&
				 !ExtractCallArguments(CallExpression, *ScriptQualified, Arguments) &&
				 !ExtractCallArguments(CallExpression, *Function.Name, Arguments)) ||
				!Arguments.TrimStartAndEnd().IsEmpty())
			{
				continue;
			}

			int32 ReturnLine = Function.BodyStart;
			while (ReturnLine < Function.BodyEnd &&
				NormalizeStatement(Lines[ReturnLine]).IsEmpty()) { ++ReturnLine; }
			if (ReturnLine >= Function.BodyEnd)
			{
				return false;
			}
			FString Constructor = NormalizeStatement(Lines[ReturnLine]);
			if (!Constructor.RemoveFromStart(TEXT("return")))
			{
				return false;
			}
			Constructor.TrimStartAndEndInline();
			if (Constructor.IsEmpty() && ++ReturnLine < Function.BodyEnd)
			{
				Constructor = StripComment(Lines[ReturnLine]).TrimStartAndEnd();
			}
			if (!Constructor.StartsWith(TEXT("{")))
			{
				return false;
			}

			int32 BraceDepth = 0;
			bool bInString = false;
			TCHAR Quote = TEXT('\0');
			auto CountBraces = [&BraceDepth, &bInString, &Quote](const FString& Text)
			{
				for (int32 Index = 0; Index < Text.Len(); ++Index)
				{
					const TCHAR Char = Text[Index];
					if (bInString)
					{
						if (Char == TEXT('\\')) { ++Index; continue; }
						if (Char == Quote) { bInString = false; }
						continue;
					}
					if (Char == TEXT('"') || Char == TEXT('\'')) { bInString = true; Quote = Char; }
					else if (Char == TEXT('{')) { ++BraceDepth; }
					else if (Char == TEXT('}')) { --BraceDepth; }
				}
			};
			CountBraces(Constructor);
			while (BraceDepth > 0 && ++ReturnLine < Function.BodyEnd)
			{
				const FString Continuation = StripComment(Lines[ReturnLine]).TrimStartAndEnd();
				Constructor += TEXT("\n") + Continuation;
				CountBraces(Continuation);
			}
			Constructor = ResolveStaticTableConstants(Constructor, *State.Locale);
			if (BraceDepth != 0 || !IsLiteralLuaTableConstructor(Constructor))
			{
				return false;
			}

			OutTable = FMT2QuestTable();
			const FString TableName = FString::Printf(TEXT("__%s_%s_L%d"),
				*State.ScriptName, *VariableName, SourceLine + 1);
			OutTable.Name = FName(*TableName);
			int32 Cursor = 0;
			ParseLuaValue(Constructor, Cursor, OutTable.Nodes);
			if (OutTable.Nodes.Num() <= 1)
			{
				return false;
			}
			return true;
		}
		return false;
	}
	TArray<TObjectPtr<UMT2QuestNode>> TranslateIfChain(
		const TArray<FString>& Lines, int32 HeaderLine, int32 EndLine, FTranslateState& State);
	bool TranslateSelect(
		const TArray<FString>& Lines, int32& Index, int32 End, const FString& JoinedLine,
		FTranslateState& State, TArray<TObjectPtr<UMT2QuestNode>>& OutNodes);
	TArray<TObjectPtr<UMT2QuestNode>> TranslateBlock(
		const TArray<FString>& Lines, int32 Start, int32 End, FTranslateState& State);

	// A call to a function declared in this quest, e.g. `event_mystery_box.drop_box()`. It becomes a
	// runtime function frame so return values and early returns keep their Lua behavior.
	bool TranslateScriptFunctionCall(
		const TArray<FString>& Lines, const FString& Line, FTranslateState& State,
		TArray<TObjectPtr<UMT2QuestNode>>& OutNodes)
	{
		if (!State.Functions)
		{
			return false;
		}
		for (const FParsedFunction& Function : *State.Functions)
		{
			// Written either bare or qualified with the quest name; both name the same function.
			FString Arguments;
			const FString QuestQualified = State.QuestName + TEXT(".") + Function.Name;
			const FString ScriptQualified = State.ScriptName + TEXT(".") + Function.Name;
			int32 CallStart = INDEX_NONE, CallEnd = INDEX_NONE;
			if (!FindFunctionCallSpan(Line, QuestQualified, CallStart, CallEnd, Arguments) &&
				!FindFunctionCallSpan(Line, ScriptQualified, CallStart, CallEnd, Arguments) &&
				!FindFunctionCallSpan(Line, Function.Name, CallStart, CallEnd, Arguments))
			{
				continue;
			}
			if (Function.BodyStart == INDEX_NONE || State.InliningFunctions.Contains(Function.Name))
			{
				// Recursive translation would not terminate. Runtime nesting is separately bounded.
				return false;
			}

			const TArray<FString> ArgumentList = SplitArguments(Arguments);
			TArray<FString> ConvertedArguments;
			for (int32 ParameterIndex = 0; ParameterIndex < ArgumentList.Num(); ++ParameterIndex)
			{
				const FString Argument = ArgumentList[ParameterIndex].TrimStartAndEnd();
				FString Reason;
				if (Argument.IsEmpty() || !CanConvertExpression(Argument, State, Reason))
				{
					// Missing parameters become nil at runtime; every supplied argument must evaluate.
					return false;
				}
				ConvertedArguments.Add(Argument);
			}

			TArray<FName> ResultVariables;
			FString CallStatement = Line.TrimStartAndEnd();
			const bool bLocalResult = CallStatement.StartsWith(TEXT("local "));
			if (bLocalResult)
			{
				CallStatement.RemoveFromStart(TEXT("local "));
			}
			int32 EqualsIndex = INDEX_NONE;
			MT2QuestExpressionSyntax::FParser BareParser(CallStatement);
			const auto BareTree = BareParser.Expression();
			const bool bBareCall = BareParser.bOk && BareParser.Cursor == BareParser.Tokens.Num() && BareTree &&
				BareTree->Kind == MT2QuestExpressionSyntax::EKind::Call;
			if (!bBareCall && CallStatement.FindChar(TEXT('='), EqualsIndex))
			{
				for (const FString& Candidate : SplitArguments(CallStatement.Left(EqualsIndex)))
				{
					if (Candidate.IsEmpty() || !(FChar::IsAlpha(Candidate[0]) || Candidate[0] == TEXT('_'))) { return false; }
					for (TCHAR Character : Candidate) { if (!FChar::IsAlnum(Character) && Character != TEXT('_')) { return false; } }
					ResultVariables.Add(FName(*Candidate));
				}
				if (ResultVariables.IsEmpty()) { return false; }
				CallStatement = CallStatement.Mid(EqualsIndex + 1).TrimStartAndEnd();
			}
			// Only a complete, unparenthesized call can expand into these destinations.
			if ((!FindFunctionCallSpan(CallStatement, QuestQualified, CallStart, CallEnd, Arguments) &&
				!FindFunctionCallSpan(CallStatement, ScriptQualified, CallStart, CallEnd, Arguments) &&
				!FindFunctionCallSpan(CallStatement, Function.Name, CallStart, CallEnd, Arguments)) ||
				CallStart != 0 || CallEnd != CallStatement.Len()) { return false; }

			const TSet<FString> CallerVariables = State.KnownVariables;
			for (const FString& Parameter : Function.Parameters)
			{
				State.KnownVariables.Add(Parameter);
			}
			const int32 UnconvertedBefore = State.Result->StatementsUnconverted;
			const int32 ReportLengthBefore = State.Result->ConversionReport.Num();
			const int32 NodesBefore = State.Result->NodesCreated;

			State.InliningFunctions.Add(Function.Name);
			const int32 CallerLoopDepth = State.LoopDepth;
			State.LoopDepth = 0;
			TArray<TObjectPtr<UMT2QuestNode>> Body =
				TranslateBlock(Lines, Function.BodyStart, Function.BodyEnd, State);
			State.LoopDepth = CallerLoopDepth;
			State.InliningFunctions.Remove(Function.Name);
			State.KnownVariables = CallerVariables;

			if (State.Result->StatementsUnconverted != UnconvertedBefore)
			{
				State.Result->StatementsUnconverted = UnconvertedBefore;
				State.Result->ConversionReport.SetNum(ReportLengthBefore);
				State.Result->NodesCreated = NodesBefore;
				return false;
			}

			UMT2QuestNode_CallFunction* Node = NewObject<UMT2QuestNode_CallFunction>(State.Outer);
			Node->FunctionName = FName(*Function.Name);
			for (const FString& Parameter : Function.Parameters)
			{
				Node->ParameterNames.Add(FName(*Parameter));
			}
			Node->ArgumentExpressions = MoveTemp(ConvertedArguments);
			Node->ResultVariables = ResultVariables;
			for (FName Name : ResultVariables)
			{
				Node->ResultQuestScopes.Add(!bLocalResult && State.GlobalVariables && State.GlobalVariables->Contains(Name.ToString()));
			}
			Node->Body = MoveTemp(Body);
			OutNodes.Add(Node);
			++State.Result->NodesCreated;
			for (FName Name : ResultVariables)
			{
				State.KnownVariables.Add(Name.ToString());
			}
			return true;
		}
		return false;
	}

	bool ExtractRuntimeFunctionCalls(
		const TArray<FString>& Lines, FString& InOutExpression, FTranslateState& State,
		TArray<TObjectPtr<UMT2QuestNode>>& OutPrelude, bool bForceNativeCall = false);

	// Converts a single call after its arguments have already been captured by expression lowering.
	bool ExtractSimpleFunctionCall(
		const TArray<FString>& Lines, FString& InOutExpression, FTranslateState& State,
		TArray<TObjectPtr<UMT2QuestNode>>& OutPrelude)
	{
		if (!State.Functions) { return true; }
		for (int32 Pass = 0; Pass < 32; ++Pass)
		{
			bool bFound = false;
			for (const FParsedFunction& Function : *State.Functions)
			{
				if (Function.BodyStart == INDEX_NONE || State.InliningFunctions.Contains(Function.Name))
				{
					continue;
				}
				const FString Names[] = {
					State.QuestName + TEXT(".") + Function.Name,
					State.ScriptName + TEXT(".") + Function.Name,
					Function.Name};
				int32 CallStart = INDEX_NONE;
				int32 CallEnd = INDEX_NONE;
				FString Arguments;
				for (const FString& Name : Names)
				{
					if (FindFunctionCallSpan(InOutExpression, Name, CallStart, CallEnd, Arguments))
					{
						break;
					}
				}
				if (CallStart == INDEX_NONE) { continue; }

				const TArray<FString> ArgumentList = SplitArguments(Arguments);
				for (const FString& Argument : ArgumentList)
				{
					FString Reason;
					if (!CanConvertExpression(Argument, State, Reason)) { return false; }
				}

				const TSet<FString> CallerVariables = State.KnownVariables;
				for (const FString& Parameter : Function.Parameters) { State.KnownVariables.Add(Parameter); }
				const int32 UnconvertedBefore = State.Result->StatementsUnconverted;
				const int32 ReportLengthBefore = State.Result->ConversionReport.Num();
				const int32 NodesBefore = State.Result->NodesCreated;
				State.InliningFunctions.Add(Function.Name);
				const int32 CallerLoopDepth = State.LoopDepth;
				State.LoopDepth = 0;
				TArray<TObjectPtr<UMT2QuestNode>> Body =
					TranslateBlock(Lines, Function.BodyStart, Function.BodyEnd, State);
				State.LoopDepth = CallerLoopDepth;
				State.InliningFunctions.Remove(Function.Name);
				State.KnownVariables = CallerVariables;
				if (State.Result->StatementsUnconverted != UnconvertedBefore)
				{
					State.Result->StatementsUnconverted = UnconvertedBefore;
					State.Result->ConversionReport.SetNum(ReportLengthBefore);
					State.Result->NodesCreated = NodesBefore;
					return false;
				}

				const FName ResultName(*FString::Printf(
					TEXT("__quest_call_%d"), State.GeneratedTemporaryIndex++));
				UMT2QuestNode_CallFunction* Node = NewObject<UMT2QuestNode_CallFunction>(State.Outer);
				Node->FunctionName = FName(*Function.Name);
				for (const FString& Parameter : Function.Parameters) Node->ParameterNames.Add(FName(*Parameter));
				Node->ArgumentExpressions = ArgumentList;
				Node->ResultVariable = ResultName;
				Node->Body = MoveTemp(Body);
				OutPrelude.Add(Node);
				++State.Result->NodesCreated;
				State.KnownVariables.Add(ResultName.ToString());
				InOutExpression = InOutExpression.Left(CallStart) + ResultName.ToString() +
					InOutExpression.Mid(CallEnd);
				bFound = true;
				break;
			}
			if (!bFound) { return true; }
		}
		return false;
	}

	bool LowerExpressionTree(const TSharedPtr<MT2QuestExpressionSyntax::FNode>& Tree,
		const TArray<FString>& Lines, FTranslateState& State,
		TArray<TObjectPtr<UMT2QuestNode>>& Nodes, FString& OutValue)
	{
		using namespace MT2QuestExpressionSyntax;
		if (!Tree) { return false; }
		auto Capture = [&](const FString& Expression)
		{
			FString Reason;
			if (!CanConvertExpression(Expression, State, Reason)) { return false; }
			OutValue = FString::Printf(TEXT("__quest_expr_%d"), State.GeneratedTemporaryIndex++);
			UMT2QuestNode_SetVariable* Node = NewObject<UMT2QuestNode_SetVariable>(State.Outer);
			Node->VariableName = FName(*OutValue);
			Node->Expression = Expression;
			Nodes.Add(Node);
			State.KnownVariables.Add(OutValue);
			++State.Result->NodesCreated;
			return true;
		};
		if (Tree->Kind == EKind::Leaf) { return Capture(Tree->Text); }
		FString Left;
		if (Tree->Kind == EKind::Call)
		{
			TArray<FString> Arguments;
			for (const auto& Child : Tree->Children)
			{
				FString Value;
				if (!LowerExpressionTree(Child, Lines, State, Nodes, Value)) { return false; }
				Arguments.Add(Value);
			}
			FString Call = Tree->Text + TEXT("(") + FString::Join(Arguments, TEXT(",")) + TEXT(")");
			if (!ExtractSimpleFunctionCall(Lines, Call, State, Nodes)) { return false; }
			return Capture(Call);
		}
		if (!LowerExpressionTree(Tree->Children[0], Lines, State, Nodes, Left)) { return false; }
		if (Tree->Kind == EKind::Unary) { return Capture(Tree->Text + TEXT(" (") + Left + TEXT(")")); }
		if (Tree->Kind == EKind::Binary && (Tree->Text == TEXT("and") || Tree->Text == TEXT("or")))
		{
			// Preserve the actual operand value, not a boolean coercion. Only the branch decision
			// tests truthiness; all RHS evaluation (including native reads) lives inside that branch.
			TArray<TObjectPtr<UMT2QuestNode>> RightNodes;
			FString Right;
			if (!LowerExpressionTree(Tree->Children[1], Lines, State, RightNodes, Right)) { return false; }
			UMT2QuestNode_SetVariable* Assign = NewObject<UMT2QuestNode_SetVariable>(State.Outer);
			Assign->VariableName = FName(*Left);
			Assign->Expression = Right;
			RightNodes.Add(Assign);
			UMT2QuestCondition_Expression* Test = NewObject<UMT2QuestCondition_Expression>(State.Outer);
			Test->Expression = Left;
			UMT2QuestNode_If* Branch = NewObject<UMT2QuestNode_If>(State.Outer);
			Branch->Test.Add(Test);
			if (Tree->Text == TEXT("and")) { Branch->Then = MoveTemp(RightNodes); }
			else { Branch->Else = MoveTemp(RightNodes); }
			Nodes.Add(Branch);
			State.Result->NodesCreated += 2;
			OutValue = Left;
			return true;
		}
		FString Right;
		if (!LowerExpressionTree(Tree->Children[1], Lines, State, Nodes, Right)) { return false; }
		return Capture(Tree->Kind == EKind::Index ? Left + TEXT("[") + Right + TEXT("]")
			: Left + TEXT(" ") + Tree->Text + TEXT(" ") + Right);
	}

	bool ExtractRuntimeFunctionCalls(
		const TArray<FString>& Lines, FString& InOutExpression, FTranslateState& State,
		TArray<TObjectPtr<UMT2QuestNode>>& OutPrelude, bool bForceNativeCall)
	{
		if (!State.Functions && !bForceNativeCall) { return true; }
		bool bHasQuestCall = false;
		if (State.Functions)
		{
			for (const FParsedFunction& Function : *State.Functions)
			{
				for (const FString& Name : {State.QuestName + TEXT(".") + Function.Name,
					State.ScriptName + TEXT(".") + Function.Name, Function.Name})
				{
					int32 Start = 0, End = 0;
					FString Arguments;
					bHasQuestCall |= FindFunctionCallSpan(InOutExpression, Name, Start, End, Arguments);
				}
			}
		}
		if (!bHasQuestCall && !bForceNativeCall) { return true; }
		MT2QuestExpressionSyntax::FParser Parser(InOutExpression);
		const auto Tree = Parser.Expression();
		if (!Parser.bOk || Parser.Cursor != Parser.Tokens.Num()) { return false; }
		const FTranslateState Before = State;
		const int32 NodeCount = State.Result->NodesCreated;
		const int32 StatementCount = State.Result->StatementsUnconverted;
		const int32 ReportCount = State.Result->ConversionReport.Num();
		TArray<TObjectPtr<UMT2QuestNode>> Lowered;
		FString Value;
		if (!LowerExpressionTree(Tree, Lines, State, Lowered, Value))
		{
			State = Before;
			State.Result->NodesCreated = NodeCount;
			State.Result->StatementsUnconverted = StatementCount;
			State.Result->ConversionReport.SetNum(ReportCount);
			return false;
		}
		OutPrelude.Append(Lowered);
		InOutExpression = Value;
		return true;
	}

	// Resolves the common old-quest helper shape:
	//   item_list[0] = {50187}; ...; return item_list[job][index]
	// when called with pc.job. Keeping the result on the Give Item node preserves the authored race
	// selection while leaving it visible and editable in the imported quest Blueprint.
	bool TryParseRaceItemFunction(
		const TArray<FString>& Lines, const FString& Expression, const FTranslateState& State,
		TMap<EMT2CharacterRace, int32>& OutVnums)
	{
		if (!State.Functions)
		{
			return false;
		}
		for (const FParsedFunction& Function : *State.Functions)
		{
			FString Arguments;
			const FString QuestQualified = State.QuestName + TEXT(".") + Function.Name;
			const FString ScriptQualified = State.ScriptName + TEXT(".") + Function.Name;
			if (!ExtractCallArguments(Expression, *QuestQualified, Arguments) &&
				!ExtractCallArguments(Expression, *ScriptQualified, Arguments) &&
				!ExtractCallArguments(Expression, *Function.Name, Arguments))
			{
				continue;
			}
			const TArray<FString> CallArguments = SplitArguments(Arguments);
			if (CallArguments.IsEmpty() ||
				(!CallArguments[0].Contains(TEXT("pc.job")) &&
				 !CallArguments[0].Contains(TEXT("pc.get_job"))))
			{
				return false;
			}

			for (int32 LineIndex = Function.BodyStart;
				LineIndex < Function.BodyEnd && Lines.IsValidIndex(LineIndex); ++LineIndex)
			{
				const FString Line = NormalizeStatement(Lines[LineIndex]);
				const int32 ItemList = Line.Find(TEXT("item_list["), ESearchCase::IgnoreCase);
				if (ItemList == INDEX_NONE)
				{
					continue;
				}
				const int32 RaceStart = ItemList + 10;
				const int32 RaceEnd = Line.Find(TEXT("]"), ESearchCase::CaseSensitive,
					ESearchDir::FromStart, RaceStart);
				const int32 ValueStart = Line.Find(TEXT("{"), ESearchCase::CaseSensitive,
					ESearchDir::FromStart, RaceEnd);
				const int32 ValueEnd = Line.Find(TEXT("}"), ESearchCase::CaseSensitive,
					ESearchDir::FromStart, ValueStart);
				if (RaceEnd == INDEX_NONE || ValueStart == INDEX_NONE || ValueEnd == INDEX_NONE)
				{
					continue;
				}
				const int32 Race = FCString::Atoi(*Line.Mid(RaceStart, RaceEnd - RaceStart));
				const int32 Vnum = FCString::Atoi(*Line.Mid(ValueStart + 1, ValueEnd - ValueStart - 1));
				if (Race >= 0 && Race <= 3 && Vnum > 0)
				{
					OutVnums.Add(static_cast<EMT2CharacterRace>(Race), Vnum);
				}
			}
			return OutVnums.Num() == 4;
		}
		return false;
	}

	// First 'elseif'/'else' belonging to the if-block whose body starts after HeaderLine. Depth starts
	// at 1 because we are already inside the block ('elseif'/'else' are continuations, not new blocks).
	int32 FindNextBranchLine(
		const TArray<FString>& Lines, int32 HeaderLine, int32 EndLine, bool& bOutIsElseIf)
	{
		bOutIsElseIf = false;
		int32 Depth = 1;
		for (int32 Index = HeaderLine + 1; Index < EndLine; ++Index)
		{
			if (Depth == 1)
			{
				const bool bElseIf = LineHasWord(Lines[Index], TEXT("elseif"));
				if (bElseIf || LineHasWord(Lines[Index], TEXT("else")))
				{
					bOutIsElseIf = bElseIf;
					return Index;
				}
			}
			Depth += BlockDelta(Lines[Index]);
			if (Depth <= 0)
			{
				break;
			}
		}
		return INDEX_NONE;
	}

	void RecordUnconverted(
		FTranslateState& State, TArray<TObjectPtr<UMT2QuestNode>>& Nodes,
		const FString& Lua, int32 LineNumber)
	{
		++State.Result->StatementsUnconverted;
		State.Result->ConversionReport.Add(FString::Printf(
			TEXT("%s(%d): %s"), *State.ScriptName, LineNumber + 1, *Lua.TrimStartAndEnd()));

		// Fold consecutive unconverted lines into one TODO node so the details panel stays readable.
		if (!Nodes.IsEmpty())
		{
			if (UMT2QuestNode_Unconverted* Previous = Cast<UMT2QuestNode_Unconverted>(Nodes.Last().Get()))
			{
				Previous->SourceLua += TEXT("\n") + Lua.TrimStartAndEnd();
				Previous->bStopExecution |= Lua.Contains(TEXT("item.copy_and_give_before_remove"));
				return;
			}
		}
		UMT2QuestNode_Unconverted* Node = NewObject<UMT2QuestNode_Unconverted>(State.Outer);
		Node->bStopExecution = Lua.Contains(TEXT("item.copy_and_give_before_remove"));
		Node->SourceLocation = FString::Printf(TEXT("%s:%d"), *State.ScriptName, LineNumber + 1);
		Node->SourceLua = Lua.TrimStartAndEnd();
		Nodes.Add(Node);
		++State.Result->NodesCreated;
	}

	// Converts a Lua boolean expression into conditions. Only the regular comparisons convert.
	bool TranslateCondition(
		const FString& Expression, FTranslateState& State,
		TArray<TObjectPtr<UMT2QuestCondition>>& OutConditions)
	{
		FString Text = Expression.TrimStartAndEnd();
		if (Text.IsEmpty())
		{
			return false;
		}
		// Prefer the fully parsed expression over substring-based specializations. In particular,
		// count_item(...) == 0 is not HasItem(count=1), and get_level()+N is not a raw level test.
		FString ExpressionReason;
		if (CanConvertExpression(Text, State, ExpressionReason))
		{
			UMT2QuestCondition_Expression* Condition = NewObject<UMT2QuestCondition_Expression>(State.Outer);
			Condition->Expression = Text;
			OutConditions.Add(Condition);
			return true;
		}
		// Parentheses around the whole predicate do not change its truth value, but previously hid the
		// call from the predicate recognizer (`if (is_test_server()) then`).
		auto HasOneOuterParenthesisPair = [](const FString& Candidate)
		{
			if (!Candidate.StartsWith(TEXT("(")) || !Candidate.EndsWith(TEXT(")"))) { return false; }
			int32 Depth = 0;
			for (int32 Index = 0; Index < Candidate.Len(); ++Index)
			{
				if (Candidate[Index] == TEXT('(')) { ++Depth; }
				else if (Candidate[Index] == TEXT(')') && --Depth == 0)
				{
					return Index == Candidate.Len() - 1;
				}
			}
			return false;
		};
		while (HasOneOuterParenthesisPair(Text))
		{
			const FString Inner = Text.Mid(1, Text.Len() - 2).TrimStartAndEnd();
			if (Inner.IsEmpty() || CountUnclosedParens(Inner) != 0) { break; }
			Text = Inner;
		}

		// A bare predicate call (`if is_test_server() then`, `if party.is_party() then`) has no
		// comparison, but these functions return 0/1 so truthiness is unambiguous - unlike a bare numeric
		// getter, which is why only predicate-shaped names qualify.
		{
			const FString Bare = Text.TrimStartAndEnd();
			const int32 ParenIndex = Bare.Find(TEXT("("));
			if (ParenIndex > 0 && Bare.EndsWith(TEXT(")")))
			{
				const FString FunctionName = Bare.Left(ParenIndex).TrimStartAndEnd();
				const bool bPredicate = FunctionName.StartsWith(TEXT("is_")) ||
					FunctionName.StartsWith(TEXT("has_")) || FunctionName.Contains(TEXT(".is_")) ||
					FunctionName.Contains(TEXT(".has_")) || FunctionName == TEXT("pc.hasguild") ||
					FunctionName == TEXT("pc.has_guild");
				FString Reason;
				if (bPredicate && FMT2QuestExpression::IsSupportedFunction(FunctionName) &&
					CanConvertExpression(Bare, State, Reason))
				{
					UMT2QuestCondition_Expression* Condition =
						NewObject<UMT2QuestCondition_Expression>(State.Outer);
					Condition->Expression = Bare;
					OutConditions.Add(Condition);
					return true;
				}
			}
		}

		// Compound conditions don't reduce to one simple test, but the evaluator handles them, so they
		// convert as an Expression condition instead of becoming a TODO.
		if (LineHasWord(Text, TEXT("and")) || LineHasWord(Text, TEXT("or")) || LineHasWord(Text, TEXT("not")))
		{
			FString Reason;
			if (CanConvertExpression(Text, State, Reason))
			{
				UMT2QuestCondition_Expression* Condition =
					NewObject<UMT2QuestCondition_Expression>(State.Outer);
				Condition->Expression = Text;
				OutConditions.Add(Condition);
				return true;
			}
			return false;
		}

		// Ordered longest-first and scanned in this exact order: a TMap's arbitrary iteration order
		// would let ">" match before ">=" and silently invert the comparison. The scripts use both
		// Lua's "~=" and C-style "!=".
		static const TArray<TPair<FString, EMT2QuestCompare>> Operators = {
			{TEXT(">="), EMT2QuestCompare::GreaterOrEqual},
			{TEXT("<="), EMT2QuestCompare::LessOrEqual},
			{TEXT("=="), EMT2QuestCompare::Equal},
			{TEXT("~="), EMT2QuestCompare::NotEqual},
			{TEXT("!="), EMT2QuestCompare::NotEqual},
			{TEXT(">"),  EMT2QuestCompare::Greater},
			{TEXT("<"),  EMT2QuestCompare::Less},
		};

		FString Left, Right;
		EMT2QuestCompare Comparison = EMT2QuestCompare::Equal;
		bool bFoundOperator = false;
		for (const TPair<FString, EMT2QuestCompare>& Operator : Operators)
		{
			const int32 OperatorIndex = Text.Find(Operator.Key, ESearchCase::CaseSensitive);
			if (OperatorIndex != INDEX_NONE)
			{
				Left = Text.Left(OperatorIndex).TrimStartAndEnd();
				Right = Text.Mid(OperatorIndex + Operator.Key.Len()).TrimStartAndEnd();
				Comparison = Operator.Value;
				bFoundOperator = true;
				break;
			}
		}

		// Lua truthiness is represented explicitly by the evaluator, including bare locals and
		// function-return temporaries. A comparison is not required for a valid condition.
		auto TryExpressionFallback = [&]() -> bool
		{
			FString Reason;
			if (!CanConvertExpression(Text, State, Reason))
			{
				return false;
			}
			UMT2QuestCondition_Expression* Condition = NewObject<UMT2QuestCondition_Expression>(State.Outer);
			Condition->Expression = Text;
			OutConditions.Add(Condition);
			return true;
		};

		if (!bFoundOperator)
		{
			return TryExpressionFallback();
		}

		int32 RightValue = 0;
		if (!ParseInt(Right, RightValue))
		{
			return TryExpressionFallback();
		}

		FString CallArguments;
		// pc.get_level() / pc.level
		if (Left.Contains(TEXT("pc.get_level")) || Left == TEXT("pc.level"))
		{
			UMT2QuestCondition_Level* Condition = NewObject<UMT2QuestCondition_Level>(State.Outer);
			Condition->Comparison = Comparison;
			Condition->Level = RightValue;
			OutConditions.Add(Condition);
			return true;
		}
		// pc.count_item(vnum) / pc.countitem(vnum)
		if (ExtractCallArguments(Left, TEXT("pc.count_item"), CallArguments) ||
			ExtractCallArguments(Left, TEXT("pc.countitem"), CallArguments))
		{
			int32 ItemVnum = 0;
			// "count >= N" is the natural has-item form; anything else (or a computed vnum) is left to
			// the runtime expression instead of being forced into a test that means something different.
			if (!ParseInt(CallArguments, ItemVnum) ||
				(Comparison != EMT2QuestCompare::GreaterOrEqual &&
				 Comparison != EMT2QuestCompare::Greater && Comparison != EMT2QuestCompare::Equal))
			{
				return TryExpressionFallback();
			}
			UMT2QuestCondition_HasItem* Condition = NewObject<UMT2QuestCondition_HasItem>(State.Outer);
			Condition->ItemVnum = ItemVnum;
			Condition->Count = FMath::Max(RightValue, 1);
			OutConditions.Add(Condition);
			return true;
		}
		// pc.getqf("flag")
		if (ExtractCallArguments(Left, TEXT("pc.getqf"), CallArguments))
		{
			bool bResolved = false;
			const FString FlagName = ResolveText(CallArguments, *State.Locale, bResolved);
			UMT2QuestCondition_Flag* Condition = NewObject<UMT2QuestCondition_Flag>(State.Outer);
			Condition->FlagName = FName(*FlagName);
			Condition->Comparison = Comparison;
			Condition->Value = RightValue;
			OutConditions.Add(Condition);
			return true;
		}
		// pc.get_empire()
		if (Left.Contains(TEXT("pc.get_empire")) || Left == TEXT("pc.empire"))
		{
			UMT2QuestCondition_Empire* Condition = NewObject<UMT2QuestCondition_Empire>(State.Outer);
			Condition->Empire = RightValue;
			Condition->bInvert = (Comparison == EMT2QuestCompare::NotEqual);
			OutConditions.Add(Condition);
			return true;
		}
		// pc.get_money() / pc.gold
		if (Left.Contains(TEXT("pc.get_money")) || Left == TEXT("pc.gold") || Left == TEXT("pc.money"))
		{
			UMT2QuestCondition_Gold* Condition = NewObject<UMT2QuestCondition_Gold>(State.Outer);
			Condition->Comparison = Comparison;
			Condition->Gold = RightValue;
			OutConditions.Add(Condition);
			return true;
		}
		// Not one of the simple tests, but still evaluable at runtime.
		return TryExpressionFallback();
	}

	// Handles "local s = select(...)" plus the if/elseif chain that dispatches on it.
	bool TranslateSelect(
		const TArray<FString>& Lines, int32& Index, int32 End, const FString& JoinedLine,
		FTranslateState& State, TArray<TObjectPtr<UMT2QuestNode>>& OutNodes)
	{
		FString SelectArguments;
		const bool bTableSelect = ExtractCallArguments(JoinedLine, TEXT("select_table"), SelectArguments);
		// The caller already joined any continuation lines, so a select() whose options span several
		// lines arrives here as one statement.
		const FString Line = JoinedLine;
		if (!ExtractCallArguments(Line, TEXT("select"), SelectArguments) &&
			!ExtractCallArguments(Line, TEXT("select_table"), SelectArguments))
		{
			return false;
		}

		const TArray<FString> RawOptions = SplitArguments(SelectArguments);
		if (RawOptions.IsEmpty())
		{
			return false;
		}

		UMT2QuestNode_Select* SelectNode = NewObject<UMT2QuestNode_Select>(State.Outer);
		if (bTableSelect)
		{
			FString Reason;
			if (RawOptions.Num() != 1 || !CanConvertExpression(RawOptions[0], State, Reason)) { return false; }
			SelectNode->TableExpression = RawOptions[0];
		}
		else for (const FString& RawOption : RawOptions)
		{
			FMT2QuestChoice& Choice = SelectNode->Choices.AddDefaulted_GetRef();
			Choice.Label = FText::FromString(ResolveDisplayText(RawOption, *State.Locale));
		}

		// The variable the result was assigned to, so the following if-chain can read it.
		FString Variable;
		int32 Assign = INDEX_NONE;
		if (Line.FindChar(TEXT('='), Assign))
		{
			// Trim *before* stripping "local": the raw line is indented, so RemoveFromStart would
			// otherwise miss and leave the name as "local s".
			FString Left = Line.Left(Assign).TrimStartAndEnd();
			Left.RemoveFromStart(TEXT("local"));
			Variable = Left.TrimStartAndEnd();
			// A compound target ("a, b = ...") isn't a plain variable.
			if (Variable.Contains(TEXT(" ")) || Variable.Contains(TEXT(",")))
			{
				Variable.Reset();
			}
		}

		// Assigned form (`local s = select(...)`): publish the chosen index into that variable and let the
		// following if-chain convert as ordinary expression conditions. That handles every dispatch shape -
		// chains split across blank lines, nested ifs, reuse of `s` later - which pattern-matching the
		// chain here could not.
		if (!Variable.IsEmpty() && !Variable.Contains(TEXT(".")))
		{
			SelectNode->ResultVariable = FName(*Variable);
			int32 CallStart = INDEX_NONE;
			int32 CallEnd = INDEX_NONE;
			FString IgnoredArguments;
			if ((FindFunctionCallSpan(Line, TEXT("select"), CallStart, CallEnd, IgnoredArguments) ||
				 FindFunctionCallSpan(Line, TEXT("select_table"), CallStart, CallEnd, IgnoredArguments)) &&
				 CallEnd < Line.Len())
			{
				const FString Suffix = Line.Mid(CallEnd).TrimStartAndEnd();
				if (!Suffix.IsEmpty())
				{
					SelectNode->ResultExpression = TEXT("__mt2_selected_index ") + Suffix;
				}
			}
			State.KnownVariables.Add(Variable);
			OutNodes.Add(SelectNode);
			++State.Result->NodesCreated;
			return true;
		}

		// Map each "if var == N" branch onto the matching choice.
		int32 Cursor = Index + 1;
		while (Cursor < End && StripComment(Lines[Cursor]).TrimStartAndEnd().IsEmpty())
		{
			++Cursor;
		}
		if (!Variable.IsEmpty() && Cursor < End && LineHasWord(Lines[Cursor], TEXT("if")))
		{
			int32 ElseLine = INDEX_NONE;
			const int32 EndLine = FindMatchingEnd(Lines, Cursor, ElseLine);
			if (EndLine != INDEX_NONE && EndLine <= End)
			{
				// Segment boundaries: the opening if plus every elseif/else at this block's level.
				TArray<int32> Boundaries;
				Boundaries.Add(Cursor);
				int32 Depth = BlockDelta(Lines[Cursor]);
				for (int32 Scan = Cursor + 1; Scan < EndLine; ++Scan)
				{
					if (Depth == 1 &&
						(LineHasWord(Lines[Scan], TEXT("elseif")) ||
						 (LineHasWord(Lines[Scan], TEXT("else")) && !LineHasWord(Lines[Scan], TEXT("elseif")))))
					{
						Boundaries.Add(Scan);
					}
					Depth += BlockDelta(Lines[Scan]);
				}
				Boundaries.Add(EndLine);

				for (int32 Segment = 0; Segment + 1 < Boundaries.Num(); ++Segment)
				{
					const int32 HeaderLine = Boundaries[Segment];
					const FString Header = StripComment(Lines[HeaderLine]);
					// Which choice does this branch belong to? ("s == 2")
					int32 ChoiceNumber = INDEX_NONE;
					const int32 VariableIndex = Header.Find(Variable, ESearchCase::CaseSensitive);
					if (VariableIndex != INDEX_NONE)
					{
						const int32 EqualsIndex = Header.Find(TEXT("=="), ESearchCase::CaseSensitive, ESearchDir::FromStart, VariableIndex);
						if (EqualsIndex != INDEX_NONE)
						{
							FString Number = Header.Mid(EqualsIndex + 2);
							Number.RemoveFromEnd(TEXT("then"));
							int32 Parsed = 0;
							if (ParseInt(Number.TrimStartAndEnd(), Parsed))
							{
								ChoiceNumber = Parsed;
							}
						}
					}
					if (SelectNode->Choices.IsValidIndex(ChoiceNumber - 1))
					{
						SelectNode->Choices[ChoiceNumber - 1].Nodes =
							TranslateBlock(Lines, HeaderLine + 1, Boundaries[Segment + 1], State);
					}
				}
				Index = EndLine; // consumed the select and its dispatch chain
			}
		}

		OutNodes.Add(SelectNode);
		++State.Result->NodesCreated;
		return true;
	}

	// Translates an if/elseif/else chain. HeaderLine is the 'if' or 'elseif' line; EndLine is the 'end'
	// closing the whole chain. Each 'elseif' becomes a nested If in the previous node's Else, which is
	// how a flat Lua chain maps onto the node model.
	TArray<TObjectPtr<UMT2QuestNode>> TranslateIfChain(
		const TArray<FString>& Lines, int32 HeaderLine, int32 EndLine, FTranslateState& State)
	{
		TArray<TObjectPtr<UMT2QuestNode>> Nodes;

		FString Condition = StripComment(Lines[HeaderLine]).TrimStartAndEnd();
		Condition.RemoveFromStart(TEXT("elseif"));
		Condition.RemoveFromStart(TEXT("if"));

		// A condition may run over several lines, broken after a trailing `and`/`or` rather than inside
		// parentheses (the letter block of levelup.quest is written that way). The header ends at the
		// line carrying `then`, so keep pulling lines in until it turns up - otherwise the condition is
		// only its first fragment and the whole branch is dropped.
		int32 HeaderEndLine = HeaderLine;
		while (!LineHasWord(Lines[HeaderEndLine], TEXT("then")) && HeaderEndLine + 1 < EndLine)
		{
			++HeaderEndLine;
			Condition += TEXT(" ") + StripComment(Lines[HeaderEndLine]).TrimStartAndEnd();
		}

		const int32 ThenIndex = Condition.Find(TEXT("then"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (ThenIndex != INDEX_NONE)
		{
			Condition = Condition.Left(ThenIndex);
		}
		Condition = ExpandPureFunctionCalls(Lines, Condition, State);
		TArray<TObjectPtr<UMT2QuestNode>> FunctionPrelude;
		if (!ExtractRuntimeFunctionCalls(Lines, Condition, State, FunctionPrelude))
		{
			RecordUnconverted(State, Nodes, Condition, HeaderLine);
			return Nodes;
		}
		Nodes.Append(FunctionPrelude);

		bool bIsElseIf = false;
		const int32 BranchLine = FindNextBranchLine(Lines, HeaderEndLine, EndLine, bIsElseIf);
		const int32 ThenEnd = (BranchLine != INDEX_NONE) ? BranchLine : EndLine;

		// The shipped give_basic_weapon_new.quest contains a stale guard using an undefined global
		// named `item`: pc.countitem(item) == 0 and pc.weapon != item. The enclosing basic_weapon
		// quest flag already makes this block one-shot. The old server effectively reaches the body,
		// while strict expression validation correctly refuses the undefined identifier. Preserve the
		// intended starter rewards instead of importing an inert TODO node.
		if ((State.ScriptName == TEXT("give_basic_weapon") ||
			 State.ScriptName == TEXT("give_basic_weapon_new")) &&
			Condition.Contains(TEXT("countitem(item)"), ESearchCase::IgnoreCase) &&
			Condition.Contains(TEXT("weapon"), ESearchCase::IgnoreCase))
		{
			return TranslateBlock(Lines, HeaderEndLine + 1, ThenEnd, State);
		}

		UMT2QuestNode_If* IfNode = NewObject<UMT2QuestNode_If>(State.Outer);
		if (!TranslateCondition(Condition, State, IfNode->Test))
		{
			FString UnsupportedReason;
			CanConvertExpression(Condition, State, UnsupportedReason);
			// The condition isn't expressible. Keep the whole construct as a TODO rather than emitting
			// a branch that would silently take the wrong path at runtime.
			RecordUnconverted(
				State, Nodes,
				FString::Printf(TEXT("if%s then ... end [unsupported: %s]"),
					*Condition, *UnsupportedReason),
				HeaderLine);
			return Nodes;
		}

		IfNode->Then = TranslateBlock(Lines, HeaderEndLine + 1, ThenEnd, State);
		if (BranchLine != INDEX_NONE)
		{
			IfNode->Else = bIsElseIf
				? TranslateIfChain(Lines, BranchLine, EndLine, State)
				: TranslateBlock(Lines, BranchLine + 1, EndLine, State);
		}
		Nodes.Add(IfNode);
		++State.Result->NodesCreated;
		return Nodes;
	}

	TArray<TObjectPtr<UMT2QuestNode>> TranslateBlock(
		const TArray<FString>& Lines, int32 Start, int32 End, FTranslateState& State)
	{
		TArray<TObjectPtr<UMT2QuestNode>> Nodes;
		UMT2QuestNode_Say* PendingSay = nullptr;

		// say_title/say accumulate into one Say node until another kind of statement breaks the run.
		auto FlushSay = [&PendingSay]() { PendingSay = nullptr; };
		auto EnsureSay = [&](void) -> UMT2QuestNode_Say*
		{
			if (!PendingSay)
			{
				PendingSay = NewObject<UMT2QuestNode_Say>(State.Outer);
				Nodes.Add(PendingSay);
				++State.Result->NodesCreated;
			}
			return PendingSay;
		};

		for (int32 Index = Start; Index < End; ++Index)
		{
			const int32 StatementStart = Index;
			const FString Raw = Lines[Index];
			FString Line = NormalizeStatement(Raw);
			if (Line.IsEmpty())
			{
				continue;
			}

			// A statement may span several lines (long select() argument lists in particular). Join
			// following lines until the parentheses balance, so the statement is parsed as one unit
			// instead of being reported as an unconvertible fragment. Block keywords are never joined:
			// their line indices drive the if/else structure.
			if (!LineHasWord(Line, TEXT("if")) && !LineHasWord(Line, TEXT("while")) &&
				!LineHasWord(Line, TEXT("for")) && !LineHasWord(Line, TEXT("begin")))
			{
				int32 Balance = CountUnclosedParens(Line);
				int32 Lookahead = Index;
				while (Balance > 0 && Lookahead + 1 < End)
				{
					++Lookahead;
					const FString Continuation = StripComment(Lines[Lookahead]).TrimStartAndEnd();
					Line += TEXT(" ") + Continuation;
					Balance += CountUnclosedParens(Continuation);
				}
				Index = Lookahead; // the joined lines are consumed by this statement
			}
			Line = ExpandPureFunctionCalls(Lines, Line, State);

			FString Arguments;

			// Anonymous table callbacks are function frames, not generic for-loops: return nil
			// continues iteration while any other return ends only this table call.
			if (Line.Contains(TEXT("table.foreach")))
			{
				FlushSay();
				FString Source;
				for (int32 SourceIndex = StatementStart; SourceIndex <= Index; ++SourceIndex)
				{
					if (!Source.IsEmpty()) { Source += TEXT("\n"); }
					Source += StripComment(Lines[SourceIndex]);
				}
				int32 CallStart = INDEX_NONE, CallEnd = INDEX_NONE;
				FString CallArguments;
				const bool bSequence = FindFunctionCallSpan(Source, TEXT("table.foreachi"), CallStart, CallEnd, CallArguments);
				const bool bFound = bSequence || FindFunctionCallSpan(Source, TEXT("table.foreach"), CallStart, CallEnd, CallArguments);
				const TArray<FString> Parts = SplitArguments(CallArguments);
				FString Prefix = bFound ? Source.Left(CallStart).TrimStartAndEnd() : FString();
				const bool bLocal = Prefix.RemoveFromStart(TEXT("local "));
				FRegexMatcher Assignment(FRegexPattern(TEXT("^([A-Za-z_][A-Za-z_0-9]*)\\s*=$")), Prefix);
				const bool bAssignment = Assignment.FindNext();
				const FString ResultName = bAssignment ? Assignment.GetCaptureGroup(1) : FString();
				FString Suffix = bFound ? NormalizeStatement(Source.Mid(CallEnd)) : FString();
				FString TableExpression = Parts.IsEmpty() ? FString() : Parts[0];
				// Split only at the first argument delimiter. Commas in a callback's Lua local
				// declaration are not arguments of the enclosing table call.
				int32 CallbackOffset = Parts.IsEmpty() ? INDEX_NONE : CallArguments.Find(TableExpression) + TableExpression.Len();
				while (CallbackOffset >= 0 && CallbackOffset < CallArguments.Len() && FChar::IsWhitespace(CallArguments[CallbackOffset])) { ++CallbackOffset; }
				FString Callback = CallbackOffset >= 0 && CallbackOffset < CallArguments.Len() && CallArguments[CallbackOffset] == TEXT(',')
					? CallArguments.Mid(CallbackOffset + 1).TrimStartAndEnd() : FString();
				FRegexMatcher Function(FRegexPattern(TEXT("^function\\s*\\(([^)]*)\\)\\s*([\\s\\S]*)\\bend\\s*$")), Callback);
				FString Reason;
				bool bValid = bFound && (Prefix.IsEmpty() || bAssignment) && Suffix.IsEmpty() &&
					CanConvertExpression(TableExpression, State, Reason) && Function.FindNext();
				TArray<FString> Parameters;
				FString BodyText;
				if (bValid)
				{
					Parameters = SplitArguments(Function.GetCaptureGroup(1));
					BodyText = Function.GetCaptureGroup(2);
					bValid = Parameters.Num() >= 1 && Parameters.Num() <= 2;
					for (const FString& Parameter : Parameters)
					{
						FRegexMatcher Identifier(FRegexPattern(TEXT("^[A-Za-z_][A-Za-z_0-9]*$")), Parameter);
						bValid &= Identifier.FindNext();
					}
					if (Parameters.Num() == 2 && Parameters[0] == Parameters[1]) { bValid = false; }
				}
				UMT2QuestNode_TableCallback* Node = nullptr;
				if (bValid)
				{
					Node = NewObject<UMT2QuestNode_TableCallback>(State.Outer);
					Node->TableExpression = TableExpression;
					Node->KeyVariable = FName(*Parameters[0]);
					Node->ValueVariable = Parameters.Num() == 2 ? FName(*Parameters[1]) : NAME_None;
					Node->bSequence = bSequence;
					Node->ResultVariable = FName(*ResultName);
					Node->bResultQuestScoped = !bLocal && State.GlobalVariables && State.GlobalVariables->Contains(ResultName);
					FTranslateState BodyState = State;
					BodyState.LoopDepth = 0;
					TSet<FString> Globals = State.GlobalVariables ? *State.GlobalVariables : TSet<FString>();
					for (const FString& Parameter : Parameters) { BodyState.KnownVariables.Add(Parameter); Globals.Remove(Parameter); }
					FRegexMatcher Locals(FRegexPattern(TEXT("\\blocal\\s+([A-Za-z_][A-Za-z_0-9]*(?:\\s*,\\s*[A-Za-z_][A-Za-z_0-9]*)*)")), BodyText);
					while (Locals.FindNext())
					{
						for (const FString& Name : SplitArguments(Locals.GetCaptureGroup(1)))
						{
							Node->LocalVariables.AddUnique(FName(*Name));
							Globals.Remove(Name);
						}
					}
					BodyState.GlobalVariables = &Globals;
					const int32 BodyOffset = Source.Find(Callback, ESearchCase::CaseSensitive) + Function.GetCaptureGroupBeginning(2);
					int32 BodyStart = StatementStart;
					for (int32 Char = 0; Char < BodyOffset; ++Char) { if (Source[Char] == TEXT('\n')) { ++BodyStart; } }
					// Keep the full source at its original indices: callback statements may call
					// quest functions whose definitions are outside this callback.
					TArray<FString> BodyLines = Lines;
					TArray<FString> ParsedLines;
					BodyText.ParseIntoArrayLines(ParsedLines, false);
					for (int32 BodyIndex = 0; BodyIndex < ParsedLines.Num(); ++BodyIndex)
					{
						BodyLines[BodyStart + BodyIndex] = ParsedLines[BodyIndex];
					}
					const int32 UnconvertedBefore = State.Result->StatementsUnconverted;
					const int32 ReportBefore = State.Result->ConversionReport.Num();
					const int32 NodesBefore = State.Result->NodesCreated;
					Node->Body = TranslateBlock(BodyLines, BodyStart, BodyStart + ParsedLines.Num(), BodyState);
					bValid = State.Result->StatementsUnconverted == UnconvertedBefore;
					if (!bValid)
					{
						Reason = State.Result->ConversionReport[ReportBefore];
						State.Result->StatementsUnconverted = UnconvertedBefore;
						State.Result->ConversionReport.SetNum(ReportBefore);
						State.Result->NodesCreated = NodesBefore;
					}
					else { State.GeneratedTemporaryIndex = BodyState.GeneratedTemporaryIndex; }
				}
				if (bValid)
				{
					Nodes.Add(Node);
					++State.Result->NodesCreated;
					if (!ResultName.IsEmpty()) { State.KnownVariables.Add(ResultName); }
				}
				else
				{
					RecordUnconverted(State, Nodes, Line + TEXT(" [unsupported table callback: ") + Reason + TEXT("]"), StatementStart);
					CastChecked<UMT2QuestNode_Unconverted>(Nodes.Last().Get())->bStopExecution = true;
				}
				continue;
			}

			// Literal tables retain their compact serialized form; assignments instantiate private
			// runtime values. Dynamic constructors are evaluated when the statement executes.
			{
				FString Assignment = Line;
				const bool bLocalTable = Assignment.StartsWith(TEXT("local "));
				Assignment.RemoveFromStart(TEXT("local "));
				int32 EqualsIndex = INDEX_NONE;
				if (Assignment.FindChar(TEXT('='), EqualsIndex) && EqualsIndex > 0)
				{
					const FString VariableName = Assignment.Left(EqualsIndex).TrimStartAndEnd();
					FString Constructor = Assignment.Mid(EqualsIndex + 1).TrimStartAndEnd();
					int32 ConstructorEndLine = Index;
					if (Constructor.IsEmpty() && Index + 1 < End &&
						NormalizeStatement(Lines[Index + 1]).StartsWith(TEXT("{")))
					{
						ConstructorEndLine = Index + 1;
						Constructor = StripComment(Lines[ConstructorEndLine]).TrimStartAndEnd();
					}
					FMT2QuestTable InlineTable;
					if (!VariableName.IsEmpty() && !VariableName.Contains(TEXT(".")) &&
						!VariableName.Contains(TEXT("[")) &&
						TryConvertStaticTableFunction(
							Lines, Constructor, VariableName, Index, State, InlineTable))
					{
						UMT2QuestNode_SetVariable* Node =
							NewObject<UMT2QuestNode_SetVariable>(State.Outer);
						Node->VariableName = FName(*VariableName);
						Node->bHasInlineTable = true;
						Node->InlineTable = MoveTemp(InlineTable);
						Node->bQuestScoped = !bLocalTable && State.GlobalVariables &&
							State.GlobalVariables->Contains(VariableName);
						Nodes.Add(Node);
						++State.Result->NodesCreated;
						State.KnownVariables.Add(VariableName);
						continue;
					}
					if (Constructor.StartsWith(TEXT("{")))
					{
						int32 BraceDepth = 0;
						bool bInString = false;
						TCHAR Quote = TEXT('\0');
						auto CountBraces = [&BraceDepth, &bInString, &Quote](const FString& Text)
						{
							for (int32 CharIndex = 0; CharIndex < Text.Len(); ++CharIndex)
							{
								const TCHAR Char = Text[CharIndex];
								if (bInString)
								{
									if (Char == TEXT('\\')) { ++CharIndex; continue; }
									if (Char == Quote) { bInString = false; }
									continue;
								}
								if (Char == TEXT('"') || Char == TEXT('\''))
								{
									bInString = true;
									Quote = Char;
								}
								else if (Char == TEXT('{')) { ++BraceDepth; }
								else if (Char == TEXT('}')) { --BraceDepth; }
							}
						};
						CountBraces(Constructor);
						while (BraceDepth > 0 && ConstructorEndLine + 1 < End)
						{
							++ConstructorEndLine;
							const FString Continuation = StripComment(Lines[ConstructorEndLine]).TrimStartAndEnd();
							Constructor += TEXT("\n") + Continuation;
							CountBraces(Continuation);
						}

						Constructor = ResolveStaticTableConstants(Constructor, *State.Locale);
						FString ConstructorReason;
						if (BraceDepth == 0 && !VariableName.IsEmpty() && !VariableName.Contains(TEXT(".")) &&
							!VariableName.Contains(TEXT("[")) && !IsLiteralLuaTableConstructor(Constructor) &&
							CanConvertExpression(Constructor, State, ConstructorReason))
						{
							FlushSay();
							UMT2QuestNode_SetVariable* Node = NewObject<UMT2QuestNode_SetVariable>(State.Outer);
							Node->VariableName = FName(*VariableName);
							Node->Expression = Constructor;
							Node->bQuestScoped = !bLocalTable && State.GlobalVariables && State.GlobalVariables->Contains(VariableName);
							Nodes.Add(Node);
							++State.Result->NodesCreated;
							State.KnownVariables.Add(VariableName);
							Index = ConstructorEndLine;
							continue;
						}
						if (BraceDepth == 0 && !VariableName.IsEmpty() &&
							!VariableName.Contains(TEXT(".")) && !VariableName.Contains(TEXT("[")) &&
							IsLiteralLuaTableConstructor(Constructor))
						{
							FMT2QuestTable Table;
							const FString TableName = FString::Printf(TEXT("__%s_%s_L%d"),
								*State.ScriptName, *VariableName, Index + 1);
							Table.Name = FName(*TableName);
							int32 Cursor = 0;
							ParseLuaValue(Constructor, Cursor, Table.Nodes);
							if (Table.Nodes.Num() > 1)
							{
								UMT2QuestNode_SetVariable* Node =
									NewObject<UMT2QuestNode_SetVariable>(State.Outer);
								Node->VariableName = FName(*VariableName);
								Node->bHasInlineTable = true;
								Node->InlineTable = MoveTemp(Table);
								Node->bQuestScoped = !bLocalTable && State.GlobalVariables &&
									State.GlobalVariables->Contains(VariableName);
								Nodes.Add(Node);
								++State.Result->NodesCreated;
								State.KnownVariables.Add(VariableName);
								Index = ConstructorEndLine;
								continue;
							}
						}
					}
				}
			}

			// Mutable table writes and table.insert use the same runtime alias-preserving model.
			{
				FString TableExpression, KeyExpression, ValueExpression;
				bool bInsert = ExtractCallArguments(Line, TEXT("table.insert"), Arguments);
				if (bInsert)
				{
					const TArray<FString> Parts = SplitArguments(Arguments);
					if (Parts.Num() == 2 || Parts.Num() == 3)
					{
						TableExpression = Parts[0];
						KeyExpression = Parts.Num() == 3 ? Parts[1] : FString();
						ValueExpression = Parts.Last();
					}
				}
				else
				{
					FRegexMatcher Indexed(FRegexPattern(TEXT("^([A-Za-z_][A-Za-z_0-9]*(?:\\.[A-Za-z_][A-Za-z_0-9]*|\\[.*\\])*)\\[(.+)\\]\\s*=(?!=)\\s*(.+)$")), Line);
					FRegexMatcher Named(FRegexPattern(TEXT("^([A-Za-z_][A-Za-z_0-9]*(?:\\.[A-Za-z_][A-Za-z_0-9]*|\\[.*\\])*)\\.([A-Za-z_][A-Za-z_0-9]*)\\s*=(?!=)\\s*(.+)$")), Line);
					if (Indexed.FindNext())
					{
						TableExpression = Indexed.GetCaptureGroup(1).TrimStartAndEnd();
						KeyExpression = Indexed.GetCaptureGroup(2);
						ValueExpression = Indexed.GetCaptureGroup(3);
					}
					else if (Named.FindNext())
					{
						TableExpression = Named.GetCaptureGroup(1).TrimStartAndEnd();
						KeyExpression = TEXT("\"") + Named.GetCaptureGroup(2) + TEXT("\"");
						ValueExpression = Named.GetCaptureGroup(3);
					}
				}
				if (!TableExpression.IsEmpty())
				{
					ValueExpression = ResolveStaticTableConstants(ValueExpression, *State.Locale);
					KeyExpression = ResolveStaticTableConstants(KeyExpression, *State.Locale);
					FString Reason;
					if (CanConvertExpression(TableExpression, State, Reason) && CanConvertExpression(ValueExpression, State, Reason) &&
						(KeyExpression.IsEmpty() || CanConvertExpression(KeyExpression, State, Reason)))
					{
						FlushSay();
						UMT2QuestNode_MutateTable* Node = NewObject<UMT2QuestNode_MutateTable>(State.Outer);
						Node->TableExpression = TableExpression;
						Node->KeyExpression = KeyExpression;
						Node->ValueExpression = ValueExpression;
						Node->bInsert = bInsert;
						Nodes.Add(Node);
						++State.Result->NodesCreated;
					}
					else { RecordUnconverted(State, Nodes, Raw, Index); }
					continue;
				}
			}

			// ---- control flow ----
			if (Line == TEXT("break"))
			{
				FlushSay();
				if (State.LoopDepth > 0)
				{
					Nodes.Add(NewObject<UMT2QuestNode_Break>(State.Outer));
					++State.Result->NodesCreated;
				}
				else { RecordUnconverted(State, Nodes, Raw, Index); }
				continue;
			}
			// A bare `return`: end this block. Anything else after `return` is a value, which only
			// matters inside a function and is handled where those are inlined.
			if (Line == TEXT("return"))
			{
				FlushSay();
				Nodes.Add(NewObject<UMT2QuestNode_Return>(State.Outer));
				++State.Result->NodesCreated;
				continue;
			}

			// ---- dialog ----
			if (ExtractCallArguments(Line, TEXT("say_title"), Arguments) ||
				ExtractCallArguments(Line, TEXT("say_important_title"), Arguments))
			{
				const FString Text = ResolveDisplayText(Arguments, *State.Locale);
				UMT2QuestNode_Say* SayNode = EnsureSay();
				// The first say_title is the page's heading (usually the NPC). Scripts commonly call it
				// again mid-page as a section header, so keep those as lines instead of overwriting the
				// heading and losing it.
				if (SayNode->Title.IsEmpty())
				{
					SayNode->Title = FText::FromString(Text);
				}
				else
				{
					SayNode->Lines.Add(FText::FromString(Text));
				}
				continue;
			}
			if (ExtractCallArguments(Line, TEXT("say_npc"), Arguments))
			{
				UMT2QuestNode_Say* SayNode = EnsureSay();
				if (SayNode->Title.IsEmpty()) { SayNode->Title = FText::FromString(TEXT("{npc}")); }
				continue;
			}
			if (ExtractCallArguments(Line, TEXT("say_reward"), Arguments) ||
				ExtractCallArguments(Line, TEXT("say_important"), Arguments) ||
				ExtractCallArguments(Line, TEXT("say"), Arguments))
			{
				const FString Text = ResolveDisplayText(Arguments, *State.Locale);
				EnsureSay()->Lines.Append(SplitDialogLines(Text));
				continue;
			}

			// ---- page break ----
			if (ExtractCallArguments(Line, TEXT("wait"), Arguments))
			{
				FlushSay();
				Nodes.Add(NewObject<UMT2QuestNode_Wait>(State.Outer));
				++State.Result->NodesCreated;
				continue;
			}

			// ---- select ----
			// Boundary-aware: a plain Contains("select(") also matches pc.give_item2_select(...) and
			// would misroute it into the select path.
			{
				FString SelectProbe;
				if (ExtractCallArguments(Line, TEXT("select"), SelectProbe) ||
					ExtractCallArguments(Line, TEXT("select_table"), SelectProbe))
			{
					FlushSay();
					if (TranslateSelect(Lines, Index, End, Line, State, Nodes))
					{
						continue;
					}
					RecordUnconverted(State, Nodes, Raw, Index);
					continue;
				}
			}

			// ---- if / elseif / else ----
			// while <condition> do
			if (LineHasWord(Line, TEXT("while")) && LineHasWord(Line, TEXT("do")))
			{
				FlushSay();
				int32 UnusedElse = INDEX_NONE;
				const int32 EndLine = FindMatchingEnd(Lines, Index, UnusedElse);
				FString Condition = Line;
				Condition.RemoveFromStart(TEXT("while"));
				const int32 DoIndex = Condition.Find(TEXT("do"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
				if (DoIndex != INDEX_NONE) { Condition = Condition.Left(DoIndex); }
				Condition.TrimStartAndEndInline();
				FString Reason;
				if (EndLine == INDEX_NONE || EndLine > End || !CanConvertExpression(Condition, State, Reason))
				{
					RecordUnconverted(State, Nodes, Raw, Index);
					if (EndLine != INDEX_NONE) { Index = EndLine; }
					continue;
				}
				UMT2QuestNode_While* Loop = NewObject<UMT2QuestNode_While>(State.Outer);
				Loop->ConditionExpression = Condition;
				++State.LoopDepth;
				Loop->Body = TranslateBlock(Lines, Index + 1, EndLine, State);
				--State.LoopDepth;
				Nodes.Add(Loop);
				++State.Result->NodesCreated;
				Index = EndLine;
				continue;
			}

			// repeat ... until <condition>: execute the body once, then use the shared While node.
			if (Line == TEXT("repeat"))
			{
				FlushSay();
				int32 UnusedElse = INDEX_NONE;
				const int32 UntilLine = FindMatchingEnd(Lines, Index, UnusedElse);
				FString UntilCondition = UntilLine != INDEX_NONE
					? NormalizeStatement(Lines[UntilLine]) : FString();
				UntilCondition.RemoveFromStart(TEXT("until"));
				UntilCondition.TrimStartAndEndInline();
				FString Reason;
				if (UntilLine == INDEX_NONE || UntilLine > End ||
					!CanConvertExpression(UntilCondition, State, Reason))
				{
					RecordUnconverted(State, Nodes, Raw, Index);
					if (UntilLine != INDEX_NONE) { Index = UntilLine; }
					continue;
				}
				++State.LoopDepth;
				TArray<TObjectPtr<UMT2QuestNode>> Body =
					TranslateBlock(Lines, Index + 1, UntilLine, State);
				--State.LoopDepth;
				UMT2QuestNode_While* Loop = NewObject<UMT2QuestNode_While>(State.Outer);
				Loop->bExecuteBodyFirst = true;
				Loop->ConditionExpression = FString::Printf(TEXT("not (%s)"), *UntilCondition);
				Loop->Body = MoveTemp(Body);
				Nodes.Add(Loop);
				++State.Result->NodesCreated;
				Index = UntilLine;
				continue;
			}

			// Numeric Lua for: for i = start, finish[, step] do. Lower to ordinary variable and loop
			// nodes so the runtime has one bounded loop implementation.
			if (LineHasWord(Line, TEXT("for")) && LineHasWord(Line, TEXT("do")))
			{
				FlushSay();
				int32 UnusedElse = INDEX_NONE;
				const int32 EndLine = FindMatchingEnd(Lines, Index, UnusedElse);
				const FRegexPattern IpairsPattern(TEXT("^for\\s+([A-Za-z_][A-Za-z_0-9]*)(?:\\s*,\\s*([A-Za-z_][A-Za-z_0-9]*))?\\s+in\\s+ipairs\\s*\\((.*)\\)\\s+do$"));
				FRegexMatcher Ipairs(IpairsPattern, Line);
				if (Ipairs.FindNext())
				{
					const FString Key = Ipairs.GetCaptureGroup(1);
					const FString Value = Ipairs.GetCaptureGroup(2);
					const FString Table = Ipairs.GetCaptureGroup(3).TrimStartAndEnd();
					FString Reason;
					if (EndLine == INDEX_NONE || EndLine > End || Key == Value ||
						!CanConvertExpression(Table, State, Reason))
					{
						RecordUnconverted(State, Nodes, Raw, Index);
						if (EndLine != INDEX_NONE) { Index = EndLine; }
						continue;
					}
					FTranslateState BodyState = State;
					++BodyState.LoopDepth;
					BodyState.KnownVariables.Add(Key);
					if (!Value.IsEmpty()) { BodyState.KnownVariables.Add(Value); }
					TSet<FString> Globals = State.GlobalVariables ? *State.GlobalVariables : TSet<FString>();
					Globals.Remove(Key);
					Globals.Remove(Value);
					BodyState.GlobalVariables = &Globals;
					UMT2QuestNode_ForEach* Loop = NewObject<UMT2QuestNode_ForEach>(State.Outer);
					Loop->TableExpression = Table;
					Loop->IndexVariable = FName(*Key);
					Loop->ValueVariable = FName(*Value);
					const int32 UnconvertedBefore = State.Result->StatementsUnconverted;
					const int32 ReportBefore = State.Result->ConversionReport.Num();
					const int32 NodesBefore = State.Result->NodesCreated;
					Loop->Body = TranslateBlock(Lines, Index + 1, EndLine, BodyState);
					if (State.Result->StatementsUnconverted != UnconvertedBefore)
					{
						const FString Failure = State.Result->ConversionReport[ReportBefore];
						State.Result->StatementsUnconverted = UnconvertedBefore;
						State.Result->ConversionReport.SetNum(ReportBefore);
						State.Result->NodesCreated = NodesBefore;
						RecordUnconverted(State, Nodes, Raw + TEXT(" [unsupported loop body: ") + Failure + TEXT("]"), Index);
						CastChecked<UMT2QuestNode_Unconverted>(Nodes.Last().Get())->bStopExecution = true;
						Index = EndLine;
						continue;
					}
					State.GeneratedTemporaryIndex = BodyState.GeneratedTemporaryIndex;
					Nodes.Add(Loop);
					++State.Result->NodesCreated;
					Index = EndLine;
					continue;
				}
				FString Header = Line;
				Header.RemoveFromStart(TEXT("for"));
				const int32 DoIndex = Header.Find(TEXT("do"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
				if (DoIndex != INDEX_NONE) { Header = Header.Left(DoIndex); }
				int32 EqualsIndex = INDEX_NONE;
				const bool bHasEquals = Header.FindChar(TEXT('='), EqualsIndex);
				const FString Variable = bHasEquals ? Header.Left(EqualsIndex).TrimStartAndEnd() : FString();
				const TArray<FString> Range = bHasEquals
					? SplitArguments(Header.Mid(EqualsIndex + 1)) : TArray<FString>();
				const FString Step = Range.Num() >= 3 ? Range[2] : TEXT("1");
				FString Reason;
				int32 LiteralStep = 0;
				const bool bSupported = EndLine != INDEX_NONE && EndLine <= End &&
					!Variable.IsEmpty() && !Variable.Contains(TEXT(" ")) && Range.Num() >= 2 &&
					CanConvertExpression(Range[0], State, Reason) &&
					CanConvertExpression(Range[1], State, Reason) &&
					CanConvertExpression(Step, State, Reason) && ParseInt(Step, LiteralStep) && LiteralStep != 0;
				if (!bSupported)
				{
					RecordUnconverted(State, Nodes, Raw, Index);
					if (EndLine != INDEX_NONE) { Index = EndLine; }
					continue;
				}

				UMT2QuestNode_SetVariable* Initialize = NewObject<UMT2QuestNode_SetVariable>(State.Outer);
				Initialize->VariableName = FName(*Variable);
				Initialize->Expression = Range[0];
				Nodes.Add(Initialize);
				++State.Result->NodesCreated;
				State.KnownVariables.Add(Variable);

				UMT2QuestNode_While* Loop = NewObject<UMT2QuestNode_While>(State.Outer);
				Loop->ConditionExpression = LiteralStep > 0
					? FString::Printf(TEXT("%s <= (%s)"), *Variable, *Range[1])
					: FString::Printf(TEXT("%s >= (%s)"), *Variable, *Range[1]);
				++State.LoopDepth;
				Loop->Body = TranslateBlock(Lines, Index + 1, EndLine, State);
				--State.LoopDepth;
				UMT2QuestNode_SetVariable* Increment = NewObject<UMT2QuestNode_SetVariable>(State.Outer);
				Increment->VariableName = FName(*Variable);
				Increment->Expression = FString::Printf(TEXT("%s + (%s)"), *Variable, *Step);
				Loop->Body.Add(Increment);
				Nodes.Add(Loop);
				State.Result->NodesCreated += 2;
				Index = EndLine;
				continue;
			}

			if (LineHasWord(Line, TEXT("if")) && !LineHasWord(Line, TEXT("elseif")) &&
				IfHeaderHasThen(Lines, Index, End))
			{
				const int32 InlineThen = Line.Find(TEXT(" then "), ESearchCase::CaseSensitive);
				if (InlineThen != INDEX_NONE && Line.EndsWith(TEXT(" end")))
				{
					FString Condition = Line.Mid(2, InlineThen - 2).TrimStartAndEnd();
					Condition = ExpandPureFunctionCalls(Lines, Condition, State);
					const FString Statement = Line.Mid(
						InlineThen + 6, Line.Len() - (InlineThen + 6) - 4).TrimStartAndEnd();
					FString Reason;
					TArray<TObjectPtr<UMT2QuestNode>> Prelude;
					if (!Statement.IsEmpty() && ExtractRuntimeFunctionCalls(Lines, Condition, State, Prelude) &&
						CanConvertExpression(Condition, State, Reason))
					{
						FlushSay();
						UMT2QuestNode_If* Node = NewObject<UMT2QuestNode_If>(State.Outer);
						if (!TranslateCondition(Condition, State, Node->Test))
						{
							RecordUnconverted(State, Nodes, Raw, Index);
							continue;
						}
						// Function definitions retain their source indices when translating the inline body.
						TArray<FString> InlineBody = Lines;
						InlineBody.Add(Statement);
						Node->Then = TranslateBlock(InlineBody, Lines.Num(), InlineBody.Num(), State);
						Nodes.Append(Prelude);
						Nodes.Add(Node);
						++State.Result->NodesCreated;
						continue;
					}
				}
				FlushSay();
				int32 UnusedElse = INDEX_NONE;
				const int32 EndLine = FindMatchingEnd(Lines, Index, UnusedElse);
				if (EndLine == INDEX_NONE || EndLine > End)
				{
					RecordUnconverted(State, Nodes, Raw, Index);
					continue;
				}
				Nodes.Append(TranslateIfChain(Lines, Index, EndLine, State));
				Index = EndLine;
				continue;
			}

			// ---- rewards / inventory ----
			bool bNamedGiveItem = false;
			if (ExtractCallArguments(Line, TEXT("pc.give_item2_select"), Arguments) ||
				ExtractCallArguments(Line, TEXT("pc.give_item2"), Arguments) ||
				(bNamedGiveItem = ExtractCallArguments(Line, TEXT("pc.give_item"), Arguments)))
			{
				const TArray<FString> Parts = SplitArguments(Arguments);
				const int32 VnumPart = bNamedGiveItem ? 1 : 0;
				const int32 CountPart = VnumPart + 1;
				FString Reason;
				TMap<EMT2CharacterRace, int32> RaceVnums;
				const bool bRaceItemFunction = Parts.IsValidIndex(VnumPart) &&
					TryParseRaceItemFunction(Lines, Parts[VnumPart], State, RaceVnums);
				if (Parts.IsValidIndex(VnumPart) &&
					(bRaceItemFunction || CanConvertExpression(Parts[VnumPart], State, Reason)))
				{
					FlushSay();
					UMT2QuestNode_GiveItem* Node = NewObject<UMT2QuestNode_GiveItem>(State.Outer);
					int32 ItemVnum = 0;
					// Literals stay literal (readable in the editor); computed values ride as expressions.
					if (bRaceItemFunction) { Node->ItemVnumByRace = MoveTemp(RaceVnums); }
					else if (ParseInt(Parts[VnumPart], ItemVnum)) { Node->ItemVnum = ItemVnum; }
					else { Node->ItemVnumExpression = Parts[VnumPart]; }
					int32 Count = 1;
					if (Parts.IsValidIndex(CountPart))
					{
						if (ParseInt(Parts[CountPart], Count)) { Node->Count = FMath::Max(Count, 1); }
						else if (CanConvertExpression(Parts[CountPart], State, Reason))
						{
							Node->CountExpression = Parts[CountPart];
						}
					}
					Nodes.Add(Node);
					++State.Result->NodesCreated;
					continue;
				}
				RecordUnconverted(State, Nodes, Raw, Index);
				continue;
			}
			if (ExtractCallArguments(Line, TEXT("pc.remove_item"), Arguments) ||
				ExtractCallArguments(Line, TEXT("pc.removeitem"), Arguments))
			{
				const TArray<FString> Parts = SplitArguments(Arguments);
				FString Reason;
				if (Parts.Num() >= 1 && CanConvertExpression(Parts[0], State, Reason))
				{
					FlushSay();
					UMT2QuestNode_TakeItem* Node = NewObject<UMT2QuestNode_TakeItem>(State.Outer);
					int32 ItemVnum = 0;
					if (ParseInt(Parts[0], ItemVnum)) { Node->ItemVnum = ItemVnum; }
					else { Node->ItemVnumExpression = Parts[0]; }
					int32 Count = 1;
					if (Parts.Num() >= 2)
					{
						if (ParseInt(Parts[1], Count)) { Node->Count = FMath::Max(Count, 1); }
						else if (CanConvertExpression(Parts[1], State, Reason)) { Node->CountExpression = Parts[1]; }
					}
					Nodes.Add(Node);
					++State.Result->NodesCreated;
					continue;
				}
				RecordUnconverted(State, Nodes, Raw, Index);
				continue;
			}
			if (ExtractCallArguments(Line, TEXT("pc.set_skill_level"), Arguments))
			{
				const TArray<FString> Parts = SplitArguments(Arguments);
				FString Reason;
				if (Parts.Num() >= 2 && CanConvertExpression(Parts[0], State, Reason) &&
					CanConvertExpression(Parts[1], State, Reason))
				{
					FlushSay();
					UMT2QuestNode_SetSkillLevel* Node =
						NewObject<UMT2QuestNode_SetSkillLevel>(State.Outer);
					if (!ParseInt(Parts[0], Node->SkillVnum)) { Node->SkillVnumExpression = Parts[0]; }
					if (!ParseInt(Parts[1], Node->Level)) { Node->LevelExpression = Parts[1]; }
					Nodes.Add(Node);
					++State.Result->NodesCreated;
					continue;
				}
				RecordUnconverted(State, Nodes, Raw, Index);
				continue;
			}
			if (ExtractCallArguments(Line, TEXT("horse.set_level"), Arguments))
			{
				FString Reason;
				if (CanConvertExpression(Arguments, State, Reason))
				{
					FlushSay();
					UMT2QuestNode_SetSkillLevel* Node =
						NewObject<UMT2QuestNode_SetSkillLevel>(State.Outer);
					Node->SkillVnum = 130; // SKILL_HORSE in the original server.
					if (!ParseInt(Arguments, Node->Level)) { Node->LevelExpression = Arguments; }
					Nodes.Add(Node);
					++State.Result->NodesCreated;
					continue;
				}
				RecordUnconverted(State, Nodes, Raw, Index);
				continue;
			}
			// pc.set_skill_group / clear_skill / clear_one_skill.
			{
				EMT2QuestSkillOperation Operation = EMT2QuestSkillOperation::SetGroup;
				bool bMatched = ExtractCallArguments(Line, TEXT("pc.set_skill_group"), Arguments);
				if (!bMatched && ExtractCallArguments(Line, TEXT("pc.clear_skill"), Arguments))
				{
					Operation = EMT2QuestSkillOperation::ClearAll;
					bMatched = true;
				}
				if (!bMatched && ExtractCallArguments(Line, TEXT("pc.clear_one_skill"), Arguments))
				{
					Operation = EMT2QuestSkillOperation::ClearOne;
					bMatched = true;
				}
				if (bMatched)
				{
					const bool bNeedsValue = Operation != EMT2QuestSkillOperation::ClearAll;
					FString Reason;
					if (!bNeedsValue || CanConvertExpression(Arguments, State, Reason))
					{
						FlushSay();
						UMT2QuestNode_ModifySkills* Node =
							NewObject<UMT2QuestNode_ModifySkills>(State.Outer);
						Node->Operation = Operation;
						if (bNeedsValue && !ParseInt(Arguments, Node->Value))
						{
							Node->ValueExpression = Arguments;
						}
						Nodes.Add(Node);
						++State.Result->NodesCreated;
						continue;
					}
					RecordUnconverted(State, Nodes, Raw, Index);
					continue;
				}
			}
			if (ExtractCallArguments(Line, TEXT("horse.summon"), Arguments))
			{
				FlushSay();
				Nodes.Add(NewObject<UMT2QuestNode_CallHorse>(State.Outer));
				++State.Result->NodesCreated;
				continue;
			}
			{
				EMT2QuestHorseOperation Operation = EMT2QuestHorseOperation::Ride;
				bool bMatched = ExtractCallArguments(Line, TEXT("horse.ride"), Arguments);
				if (!bMatched && ExtractCallArguments(Line, TEXT("horse.unride"), Arguments))
				{
					Operation = EMT2QuestHorseOperation::Unride;
					bMatched = true;
				}
				if (!bMatched && ExtractCallArguments(Line, TEXT("horse.unsummon"), Arguments))
				{
					Operation = EMT2QuestHorseOperation::Unsummon;
					bMatched = true;
				}
				if (!bMatched && ExtractCallArguments(Line, TEXT("horse.advance"), Arguments))
				{
					Operation = EMT2QuestHorseOperation::Advance;
					bMatched = true;
				}
				if (bMatched && Arguments.TrimStartAndEnd().IsEmpty())
				{
					FlushSay();
					UMT2QuestNode_ModifyHorse* Node =
						NewObject<UMT2QuestNode_ModifyHorse>(State.Outer);
					Node->Operation = Operation;
					Nodes.Add(Node);
					++State.Result->NodesCreated;
					continue;
				}
			}
			// pc.give_exp_perc(name, level, percent): the reward the hunting missions promise ("you get
			// 10% of a level"). Level and percent both matter, so it is handled apart from the flat forms.
			if (ExtractCallArguments(Line, TEXT("pc.give_exp_perc"), Arguments))
			{
				const TArray<FString> Parts = SplitArguments(Arguments);
				FString Reason;
				const FString LevelArgument = Parts.Num() >= 2 ? Parts[Parts.Num() - 2] : FString();
				const FString PercentArgument = Parts.Num() >= 1 ? Parts.Last() : FString();
				int32 LiteralLevel = 0;
				int32 LiteralPercent = 0;
				const bool bLevelOk =
					ParseInt(LevelArgument, LiteralLevel) || CanConvertExpression(LevelArgument, State, Reason);
				const bool bPercentOk =
					ParseInt(PercentArgument, LiteralPercent) || CanConvertExpression(PercentArgument, State, Reason);
				if (Parts.Num() >= 3 && bLevelOk && bPercentOk)
				{
					FlushSay();
					UMT2QuestNode_GiveReward* Node = NewObject<UMT2QuestNode_GiveReward>(State.Outer);
					if (ParseInt(LevelArgument, LiteralLevel)) { Node->ExperienceLevel = LiteralLevel; }
					else { Node->ExperienceLevelExpression = LevelArgument; }
					if (ParseInt(PercentArgument, LiteralPercent))
					{
						Node->ExperiencePercent = static_cast<float>(LiteralPercent);
					}
					else { Node->ExperiencePercentExpression = PercentArgument; }
					Nodes.Add(Node);
					++State.Result->NodesCreated;
					continue;
				}
				RecordUnconverted(State, Nodes, Raw, Index);
				continue;
			}
			if (ExtractCallArguments(Line, TEXT("pc.give_exp2"), Arguments) ||
				ExtractCallArguments(Line, TEXT("pc.change_money"), Arguments) ||
				ExtractCallArguments(Line, TEXT("pc.change_gold"), Arguments) ||
				ExtractCallArguments(Line, TEXT("pc.changegold"), Arguments) ||
				ExtractCallArguments(Line, TEXT("pc.changemoney"), Arguments))
			{
				const TArray<FString> Parts = SplitArguments(Arguments);
				const FString AmountArgument = Parts.Num() > 0 ? Parts.Last() : Arguments;
				int64 Amount = 0;
				FString Reason;
				const bool bLiteral = ParseInt64(AmountArgument, Amount);
				if (bLiteral || CanConvertExpression(AmountArgument, State, Reason))
				{
					FlushSay();
					UMT2QuestNode_GiveReward* Node = NewObject<UMT2QuestNode_GiveReward>(State.Outer);
					const bool bExperience = Line.Contains(TEXT("give_exp2"));
					if (bLiteral)
					{
						if (bExperience) { Node->Experience = Amount; } else { Node->Gold = Amount; }
					}
					else if (bExperience) { Node->ExperienceExpression = AmountArgument; }
					else { Node->GoldExpression = AmountArgument; }
					Nodes.Add(Node);
					++State.Result->NodesCreated;
					continue;
				}
				RecordUnconverted(State, Nodes, Raw, Index);
				continue;
			}

			// ---- quest flags / state ----
			if (ExtractCallArguments(Line, TEXT("next_time_set"), Arguments))
			{
				const TArray<FString> Parts = SplitArguments(Arguments);
				FString Reason;
				if (Parts.Num() == 2 && CanConvertExpression(Parts[0], State, Reason) &&
					CanConvertExpression(Parts[1], State, Reason))
				{
					FlushSay();
					// Lower questlib.lua's helper to existing nodes, preserving quest flag scoping and
					// the test-server delay branch without a separate persistent cooldown mechanism.
					UMT2QuestNode_If* Branch = NewObject<UMT2QuestNode_If>(State.Outer);
					UMT2QuestCondition_Expression* Test =
						NewObject<UMT2QuestCondition_Expression>(State.Outer);
					Test->Expression = TEXT("is_test_server()");
					Branch->Test.Add(Test);
					for (int32 DelayIndex = 0; DelayIndex < 2; ++DelayIndex)
					{
						UMT2QuestNode_SetFlag* Flag = NewObject<UMT2QuestNode_SetFlag>(State.Outer);
						Flag->FlagName = TEXT("__NEXT_TIME__");
						Flag->ValueExpression = FString::Printf(TEXT("get_time() + (%s)"), *Parts[DelayIndex]);
						if (DelayIndex == 0) { Branch->Else.Add(Flag); }
						else { Branch->Then.Add(Flag); }
					}
					Nodes.Add(Branch);
					State.Result->NodesCreated += 3;
					continue;
				}
				RecordUnconverted(State, Nodes, Raw, Index);
				continue;
			}
			if (ExtractCallArguments(Line, TEXT("pc.setqf"), Arguments))
			{
				const TArray<FString> Parts = SplitArguments(Arguments);
				int32 Value = 0;
				FString Reason;
				const bool bLiteral = Parts.Num() >= 2 && ParseInt(Parts[1], Value);
				// The increment idiom pc.setqf("f", pc.getqf("f") + 1) rides as an expression.
				if (Parts.Num() >= 2 && (bLiteral || CanConvertExpression(Parts[1], State, Reason)))
				{
					bool bResolved = false;
					FlushSay();
					UMT2QuestNode_SetFlag* Node = NewObject<UMT2QuestNode_SetFlag>(State.Outer);
					Node->FlagName = FName(*ResolveText(Parts[0], *State.Locale, bResolved));
					if (bLiteral) { Node->Value = Value; } else { Node->ValueExpression = Parts[1]; }
					Nodes.Add(Node);
					++State.Result->NodesCreated;
					continue;
				}
				RecordUnconverted(State, Nodes, Raw, Index);
				continue;
			}
			// set_state / setstate / set_quest_state are the same operation in the old scripts.
			if (ExtractCallArguments(Line, TEXT("set_state"), Arguments) ||
				ExtractCallArguments(Line, TEXT("setstate"), Arguments) ||
				ExtractCallArguments(Line, TEXT("set_quest_state"), Arguments))
			{
				const TArray<FString> Parts = SplitArguments(Arguments);
				// set_quest_state("quest", "state") passes the quest first; the state is the last argument.
				const FString StateArgument = Parts.Num() > 0 ? Parts.Last() : Arguments;
				FlushSay();
				UMT2QuestNode_SetState* Node = NewObject<UMT2QuestNode_SetState>(State.Outer);
				Node->StateName = FName(*StateArgument.TrimStartAndEnd().Replace(TEXT("\""), TEXT("")));
				Nodes.Add(Node);
				++State.Result->NodesCreated;
				continue;
			}

			// ---- quest journal (q.* / send_letter family) ----
			{
				auto MakeJournal = [&](EMT2QuestJournalOp Operation) -> UMT2QuestNode_Journal*
				{
					FlushSay();
					UMT2QuestNode_Journal* Node = NewObject<UMT2QuestNode_Journal>(State.Outer);
					Node->Operation = Operation;
					Nodes.Add(Node);
					++State.Result->NodesCreated;
					return Node;
				};
				bool bResolved = false;

				// send_letter(text) both opens the entry and titles it.
				if (ExtractCallArguments(Line, TEXT("send_letter"), Arguments))
				{
					UMT2QuestNode_Journal* Node = MakeJournal(EMT2QuestJournalOp::Start);
					Node->Text = FText::FromString(ResolveDisplayText(Arguments, *State.Locale));
					continue;
				}
				if (ExtractCallArguments(Line, TEXT("q.start"), Arguments) ||
					ExtractCallArguments(Line, TEXT("makequestbutton"), Arguments))
				{
					UMT2QuestNode_Journal* Node = MakeJournal(EMT2QuestJournalOp::Start);
					if (!Arguments.TrimStartAndEnd().IsEmpty())
					{
						Node->Text = FText::FromString(ResolveDisplayText(Arguments, *State.Locale));
					}
					continue;
				}
				if (ExtractCallArguments(Line, TEXT("q.set_title"), Arguments))
				{
					MakeJournal(EMT2QuestJournalOp::SetTitle)->Text =
						FText::FromString(ResolveDisplayText(Arguments, *State.Locale));
					continue;
				}
				if (ExtractCallArguments(Line, TEXT("q.set_counter_name"), Arguments) ||
					ExtractCallArguments(Line, TEXT("q.set_clock_name"), Arguments))
				{
					MakeJournal(EMT2QuestJournalOp::SetSummary)->Text =
						FText::FromString(ResolveDisplayText(Arguments, *State.Locale));
					continue;
				}
				if (ExtractCallArguments(Line, TEXT("q.set_counter_value"), Arguments) ||
					ExtractCallArguments(Line, TEXT("q.set_counter"), Arguments) ||
					ExtractCallArguments(Line, TEXT("q.set_clock_value"), Arguments))
				{
					const TArray<FString> Parts = SplitArguments(Arguments);
					const FString CounterArgument = Parts.Num() > 0 ? Parts.Last() : Arguments;
					UMT2QuestNode_Journal* Node = MakeJournal(EMT2QuestJournalOp::SetCounter);
					int32 CounterValue = 0;
					FString Reason;
					if (ParseInt(CounterArgument, CounterValue)) { Node->CounterValue = CounterValue; }
					else if (CanConvertExpression(CounterArgument, State, Reason))
					{
						Node->CounterExpression = CounterArgument;
					}
					continue;
				}
				if (ExtractCallArguments(Line, TEXT("q.done"), Arguments))
				{
					MakeJournal(EMT2QuestJournalOp::Done);
					continue;
				}
				if (ExtractCallArguments(Line, TEXT("restart_quest"), Arguments))
				{
					MakeJournal(EMT2QuestJournalOp::Restart);
					continue;
				}
				if (ExtractCallArguments(Line, TEXT("clear_letter"), Arguments) ||
					ExtractCallArguments(Line, TEXT("q.clear"), Arguments))
				{
					MakeJournal(EMT2QuestJournalOp::Clear);
					continue;
				}
			}

			// ---- quest target markers (target.* family) ----
			{
				auto MakeTarget = [&](EMT2QuestTargetOp Operation) -> UMT2QuestNode_Target*
				{
					FlushSay();
					UMT2QuestNode_Target* Node = NewObject<UMT2QuestNode_Target>(State.Outer);
					Node->Operation = Operation;
					Nodes.Add(Node);
					++State.Result->NodesCreated;
					return Node;
				};

				if (ExtractCallArguments(Line, TEXT("target.delete"), Arguments) ||
					ExtractCallArguments(Line, TEXT("target.clear"), Arguments))
				{
					const TArray<FString> Parts = SplitArguments(Arguments);
					UMT2QuestNode_Target* Node = MakeTarget(EMT2QuestTargetOp::Clear);
					if (Parts.Num() >= 1)
					{
						Node->TargetName = FName(*Parts[0].Replace(TEXT("\""), TEXT("")));
					}
					continue;
				}
				// target.pos(name, x, y)
				if (ExtractCallArguments(Line, TEXT("target.pos"), Arguments))
				{
					const TArray<FString> Parts = SplitArguments(Arguments);
					UMT2QuestNode_Target* Node = MakeTarget(EMT2QuestTargetOp::SetPosition);
					if (Parts.Num() >= 1) { Node->TargetName = FName(*Parts[0].Replace(TEXT("\""), TEXT(""))); }
					FString Reason;
					int32 Coordinate = 0;
					if (Parts.Num() >= 2)
					{
						if (ParseInt(Parts[1], Coordinate)) { Node->Position.X = Coordinate; }
						else if (CanConvertExpression(Parts[1], State, Reason)) { Node->PositionXExpression = Parts[1]; }
					}
					if (Parts.Num() >= 3)
					{
						if (ParseInt(Parts[2], Coordinate)) { Node->Position.Y = Coordinate; }
						else if (CanConvertExpression(Parts[2], State, Reason)) { Node->PositionYExpression = Parts[2]; }
					}
					continue;
				}
				// target.vid(name, vid, ...) - point at an NPC/mob.
				if (ExtractCallArguments(Line, TEXT("target.vid"), Arguments))
				{
					const TArray<FString> Parts = SplitArguments(Arguments);
					FString Reason;
					if (Parts.Num() < 2 || !CanConvertExpression(Parts[1], State, Reason))
					{
						RecordUnconverted(State, Nodes, Raw, Index);
						continue;
					}
					UMT2QuestNode_Target* Node = MakeTarget(EMT2QuestTargetOp::SetActor);
					if (Parts.Num() >= 1) { Node->TargetName = FName(*Parts[0].Replace(TEXT("\""), TEXT(""))); }
					// The identifier is nearly always a variable (`target.vid("teacher1", v)` where
					// v = find_npc_by_vnum(...)), so accept an expression as well as a literal.
					int32 TargetVnum = 0;
					if (Parts.Num() >= 2)
					{
						if (ParseInt(Parts[1], TargetVnum)) { Node->Vnum = TargetVnum; }
						else if (CanConvertExpression(Parts[1], State, Reason)) { Node->VnumExpression = Parts[1]; }
					}
					continue;
				}
			}

			// ---- misc ----
			// Pure window styling with no gameplay meaning; dropping it is correct, not a TODO.
			if (ExtractCallArguments(Line, TEXT("setskin"), Arguments))
			{
				continue;
			}
			// say_pc_name() prints the player's name; {name} is substituted at runtime.
			if (ExtractCallArguments(Line, TEXT("say_pc_name"), Arguments))
			{
				EnsureSay()->Lines.Add(FText::FromString(TEXT("{name}")));
				continue;
			}
			// say_item / say_item_vnum show an item line inside the page; the reward text around them
			// carries the meaning, so the item itself becomes a plain line.
			if (ExtractCallArguments(Line, TEXT("say_item_vnum"), Arguments) ||
				ExtractCallArguments(Line, TEXT("say_item"), Arguments))
			{
				bool bResolved = false;
				const TArray<FString> Parts = SplitArguments(Arguments);
				// A literal vnum has no display name here; a localized label does.
				FString Text;
				for (const FString& Part : Parts)
				{
					const FString Resolved = ResolveText(Part, *State.Locale, bResolved);
					if (bResolved && !Resolved.IsEmpty()) { Text = Resolved; break; }
				}
				if (!Text.IsEmpty())
				{
					EnsureSay()->Lines.Append(SplitDialogLines(Text));
				}
				continue;
			}
			int32 NpcCallStart = INDEX_NONE, NpcCallEnd = INDEX_NONE;
			if (FindFunctionCallSpan(Line, TEXT("npc.open_shop"), NpcCallStart, NpcCallEnd, Arguments) &&
				NpcCallStart == 0 && NpcCallEnd == Line.Len())
			{
				FlushSay();
				// Explicit shop-vnum selection needs a separate stock-table backend.
				if (!Arguments.TrimStartAndEnd().IsEmpty())
				{
					RecordUnconverted(State, Nodes, Raw, Index);
					continue;
				}
				UMT2QuestNode_OpenShop* Node = NewObject<UMT2QuestNode_OpenShop>(State.Outer);
				Node->bContinueQuest = true;
				Nodes.Add(Node);
				++State.Result->NodesCreated;
				continue;
			}
			if (FindFunctionCallSpan(Line, TEXT("npc.purge"), NpcCallStart, NpcCallEnd, Arguments) &&
				NpcCallStart == 0 && NpcCallEnd == Line.Len())
			{
				FlushSay();
				if (!Arguments.TrimStartAndEnd().IsEmpty())
				{
					RecordUnconverted(State, Nodes, Raw, Index);
					continue;
				}
				Nodes.Add(NewObject<UMT2QuestNode_PurgeNpc>(State.Outer));
				++State.Result->NodesCreated;
				continue;
			}
			if ((FindFunctionCallSpan(Line, TEXT("npc.lock"), NpcCallStart, NpcCallEnd, Arguments) ||
				FindFunctionCallSpan(Line, TEXT("npc.unlock"), NpcCallStart, NpcCallEnd, Arguments)) &&
				NpcCallStart == 0 && NpcCallEnd == Line.Len())
			{
				FlushSay();
				if (!Arguments.TrimStartAndEnd().IsEmpty())
				{
					RecordUnconverted(State, Nodes, Raw, Index);
					continue;
				}
				UMT2QuestNode_NpcLock* Node = NewObject<UMT2QuestNode_NpcLock>(State.Outer);
				Node->bUnlock = Line.Contains(TEXT("npc.unlock"));
				Nodes.Add(Node);
				++State.Result->NodesCreated;
				continue;
			}
			// Server-side logging with no client-visible effect.
			if (ExtractCallArguments(Line, TEXT("char_log"), Arguments) ||
				ExtractCallArguments(Line, TEXT("syschat"), Arguments) ||
				ExtractCallArguments(Line, TEXT("test_chat"), Arguments))
			{
				continue;
			}
			if ((FindFunctionCallSpan(Line, TEXT("party.setf"), NpcCallStart, NpcCallEnd, Arguments) ||
				FindFunctionCallSpan(Line, TEXT("pc.remove_skill_book_no_delay"), NpcCallStart, NpcCallEnd, Arguments) ||
				FindFunctionCallSpan(Line, TEXT("pc.learn_grand_master_skill"), NpcCallStart, NpcCallEnd, Arguments) ||
				FindFunctionCallSpan(Line, TEXT("pc.change_alignment"), NpcCallStart, NpcCallEnd, Arguments) ||
				FindFunctionCallSpan(Line, TEXT("pc.changealignment"), NpcCallStart, NpcCallEnd, Arguments)) &&
				NpcCallStart == 0 && NpcCallEnd == Line.Len())
			{
				FlushSay();
				FString Call = Line;
				TArray<TObjectPtr<UMT2QuestNode>> Prelude;
				if (ExtractRuntimeFunctionCalls(Lines, Call, State, Prelude, true)) { Nodes.Append(Prelude); }
				else { RecordUnconverted(State, Nodes, Raw, Index); }
				continue;
			}
			if (ExtractCallArguments(Line, TEXT("party.setqf"), Arguments))
			{
				const TArray<FString> Parts = SplitArguments(Arguments);
				int32 Value = 0;
				FString Reason;
				if (Parts.Num() >= 2 && CanConvertExpression(Parts[0], State, Reason) &&
					(ParseInt(Parts[1], Value) || CanConvertExpression(Parts[1], State, Reason)))
				{
					bool bResolved = false;
					FlushSay();
					UMT2QuestNode_SetFlag* Node = NewObject<UMT2QuestNode_SetFlag>(State.Outer);
					Node->FlagName = FName(*ResolveText(Parts[0], *State.Locale, bResolved));
					Node->bPartyScoped = true;
					Node->PartyFlagNameExpression = Parts[0];
					if (ParseInt(Parts[1], Value)) { Node->Value = Value; }
					else { Node->ValueExpression = Parts[1]; }
					Nodes.Add(Node);
					++State.Result->NodesCreated;
					continue;
				}
			}
			// affect.add/apply-based collect and point-based collect share the same runtime node after
			// converting old EPointTypes to their corresponding modern EApplyTypes channel.
			{
				const bool bQuestAffect = ExtractCallArguments(Line, TEXT("affect.add"), Arguments);
				const bool bHairAffect = !bQuestAffect &&
					ExtractCallArguments(Line, TEXT("affect.add_hair"), Arguments);
				const bool bCollectPoint = !bQuestAffect && !bHairAffect &&
					ExtractCallArguments(Line, TEXT("affect.add_collect_point"), Arguments);
				const bool bCollect = !bQuestAffect && !bHairAffect && !bCollectPoint &&
					ExtractCallArguments(Line, TEXT("affect.add_collect"), Arguments);
				if (bQuestAffect || bHairAffect || bCollectPoint || bCollect)
				{
					const TArray<FString> Parts = SplitArguments(Arguments);
					int32 ApplyType = 0;
					FString ValueReason;
					FString DurationReason;
					if (Parts.Num() >= 3 && ResolveLegacyAffectType(Parts[0], bCollectPoint, ApplyType) &&
						CanConvertExpression(Parts[1], State, ValueReason) &&
						CanConvertExpression(Parts[2], State, DurationReason))
					{
						FlushSay();
						UMT2QuestNode_ModifyAffect* Node =
							NewObject<UMT2QuestNode_ModifyAffect>(State.Outer);
						Node->Operation = bQuestAffect ? EMT2QuestAffectOperation::AddQuest
							: (bHairAffect ? EMT2QuestAffectOperation::AddHair
								: EMT2QuestAffectOperation::AddCollect);
						Node->ApplyType = ApplyType;
						Node->ValueExpression = Parts[1];
						Node->DurationExpression = Parts[2];
						Nodes.Add(Node);
						++State.Result->NodesCreated;
						continue;
					}
				}
			}
			if (ExtractCallArguments(Line, TEXT("pc.change_sp"), Arguments))
			{
				const TArray<FString> Parts = SplitArguments(Arguments);
				FString Reason;
				if (Parts.Num() >= 1 && CanConvertExpression(Parts[0], State, Reason))
				{
					FlushSay();
					UMT2QuestNode_ChangeMana* Node = NewObject<UMT2QuestNode_ChangeMana>(State.Outer);
					int32 Literal = 0;
					if (ParseInt(Parts[0], Literal)) { Node->Delta = Literal; }
					else { Node->DeltaExpression = Parts[0]; }
					Nodes.Add(Node);
					++State.Result->NodesCreated;
					continue;
				}
			}
			if (ExtractCallArguments(Line, TEXT("affect.remove_all_collect"), Arguments) ||
				ExtractCallArguments(Line, TEXT("affect.remove_hair"), Arguments))
			{
				FlushSay();
				UMT2QuestNode_ModifyAffect* Node =
					NewObject<UMT2QuestNode_ModifyAffect>(State.Outer);
				Node->Operation = Line.Contains(TEXT("remove_hair"))
					? EMT2QuestAffectOperation::RemoveHair
					: EMT2QuestAffectOperation::RemoveAllCollect;
				Nodes.Add(Node);
				++State.Result->NodesCreated;
				continue;
			}
			// game.set_event_flag(name, value) - a server-global flag, stored under the "game." scope.
			if (ExtractCallArguments(Line, TEXT("game.set_event_flag"), Arguments))
			{
				const TArray<FString> Parts = SplitArguments(Arguments);
				int32 FlagValue = 0;
				FString Reason;
				if (Parts.Num() >= 2 &&
					(ParseInt(Parts[1], FlagValue) || CanConvertExpression(Parts[1], State, Reason)))
				{
					bool bResolved = false;
					FlushSay();
					UMT2QuestNode_SetFlag* Node = NewObject<UMT2QuestNode_SetFlag>(State.Outer);
					Node->FlagName = FName(*(FString(TEXT("game.")) +
						ResolveText(Parts[0], *State.Locale, bResolved)));
					if (ParseInt(Parts[1], FlagValue)) { Node->Value = FlagValue; }
					else { Node->ValueExpression = Parts[1]; }
					Nodes.Add(Node);
					++State.Result->NodesCreated;
					continue;
				}
			}
			// timer(name, seconds) / server_timer(name, seconds, arg) / loop_timer(...)
			{
				const bool bServerLoop = ExtractCallArguments(Line, TEXT("server_loop_timer"), Arguments);
				const bool bServer = !bServerLoop &&
					ExtractCallArguments(Line, TEXT("server_timer"), Arguments);
				const bool bLoop = !bServerLoop && !bServer &&
					ExtractCallArguments(Line, TEXT("loop_timer"), Arguments);
				const bool bPlain = !bServerLoop && !bServer && !bLoop &&
					ExtractCallArguments(Line, TEXT("timer"), Arguments);
				if (bServerLoop || bServer || bLoop || bPlain)
				{
					const TArray<FString> Parts = SplitArguments(Arguments);
					if (Parts.Num() >= 2)
					{
						FlushSay();
						UMT2QuestNode_StartTimer* Node = NewObject<UMT2QuestNode_StartTimer>(State.Outer);
						Node->TimerName = FName(*Parts[0].Replace(TEXT("\""), TEXT("")).Replace(TEXT("'"), TEXT("")));
						Node->bServerTimer = bServerLoop || bServer;
						Node->bLooping = bServerLoop || bLoop;
						int32 DelaySeconds = 0;
						FString Reason;
						if (ParseInt(Parts[1], DelaySeconds)) { Node->Seconds = DelaySeconds; }
						else if (CanConvertExpression(Parts[1], State, Reason)) { Node->SecondsExpression = Parts[1]; }
						if (Parts.Num() >= 3 && CanConvertExpression(Parts[2], State, Reason))
						{
							Node->ArgumentExpression = Parts[2];
						}
						Nodes.Add(Node);
						++State.Result->NodesCreated;
						continue;
					}
				}
			}
			if (ExtractCallArguments(Line, TEXT("clear_server_timer"), Arguments) ||
				ExtractCallArguments(Line, TEXT("cleartimer"), Arguments) ||
				ExtractCallArguments(Line, TEXT("clear_timer"), Arguments))
			{
				const TArray<FString> Parts = SplitArguments(Arguments);
				FlushSay();
				UMT2QuestNode_ClearTimer* Node = NewObject<UMT2QuestNode_ClearTimer>(State.Outer);
				if (Parts.Num() >= 1)
				{
					Node->TimerName = FName(*Parts[0].Replace(TEXT("\""), TEXT("")).Replace(TEXT("'"), TEXT("")));
				}
				Nodes.Add(Node);
				++State.Result->NodesCreated;
				continue;
			}

			// item.set_socket(index, value) - scripted per-item scratch storage on the used item.
			if (ExtractCallArguments(Line, TEXT("item.set_socket"), Arguments))
			{
				const TArray<FString> Parts = SplitArguments(Arguments);
				int32 SocketIndex = 0;
				if (Parts.Num() >= 2 && ParseInt(Parts[0], SocketIndex))
				{
					int32 SocketValue = 0;
					FString Reason;
					const bool bLiteral = ParseInt(Parts[1], SocketValue);
					if (bLiteral || CanConvertExpression(Parts[1], State, Reason))
					{
						FlushSay();
						UMT2QuestNode_SetItemSocket* Node =
							NewObject<UMT2QuestNode_SetItemSocket>(State.Outer);
						Node->SocketIndex = SocketIndex;
						if (bLiteral) { Node->Value = SocketValue; }
						else { Node->ValueExpression = Parts[1]; }
						Nodes.Add(Node);
						++State.Result->NodesCreated;
						continue;
					}
				}
			}

			// pc.setf("quest", "flag", value) - a fully qualified flag write.
			if (ExtractCallArguments(Line, TEXT("pc.setf"), Arguments))
			{
				const TArray<FString> Parts = SplitArguments(Arguments);
				int32 FlagValue = 0;
				FString Reason;
				if (Parts.Num() >= 3 &&
					(ParseInt(Parts[2], FlagValue) || CanConvertExpression(Parts[2], State, Reason)))
				{
					bool bResolved = false;
					FlushSay();
					UMT2QuestNode_SetFlag* Node = NewObject<UMT2QuestNode_SetFlag>(State.Outer);
					Node->FlagName = FName(*(ResolveText(Parts[0], *State.Locale, bResolved) +
						TEXT(".") + ResolveText(Parts[1], *State.Locale, bResolved)));
					if (ParseInt(Parts[2], FlagValue)) { Node->Value = FlagValue; }
					else { Node->ValueExpression = Parts[2]; }
					Nodes.Add(Node);
					++State.Result->NodesCreated;
					continue;
				}
			}
			if (ExtractCallArguments(Line, TEXT("warp_to_village"), Arguments) && Arguments.TrimStartAndEnd().IsEmpty())
			{
				FlushSay();
				UMT2QuestNode_Warp* Node = NewObject<UMT2QuestNode_Warp>(State.Outer);
				Node->bCoordinatesAreLocalMetres = false;
				Node->bToEmpireVillage = true;
				Nodes.Add(Node);
				++State.Result->NodesCreated;
				continue;
			}
			// pc.warp(x, y[, private_map_index]); pc.warp_local(map_index, x, y).
			{
				const bool bWarpLocal = ExtractCallArguments(Line, TEXT("pc.warp_local"), Arguments);
				if (bWarpLocal || ExtractCallArguments(Line, TEXT("pc.warp"), Arguments))
				{
					const TArray<FString> Parts = SplitArguments(Arguments);
					FString Reason;
					const int32 XIndex = bWarpLocal ? 1 : 0;
					const int32 YIndex = XIndex + 1;
					const int32 MapIndex = bWarpLocal ? 0 : 2;
					if ((bWarpLocal ? Parts.Num() == 3 : (Parts.Num() == 2 || Parts.Num() == 3)) &&
						CanConvertExpression(Parts[XIndex], State, Reason) &&
						CanConvertExpression(Parts[YIndex], State, Reason) &&
						(!Parts.IsValidIndex(MapIndex) || CanConvertExpression(Parts[MapIndex], State, Reason)))
					{
						FlushSay();
						UMT2QuestNode_Warp* Node = NewObject<UMT2QuestNode_Warp>(State.Outer);
						Node->bCoordinatesAreLocalMetres = false;
						Node->bUsesLegacyMapLocalCoordinates = bWarpLocal;
						if (Parts.IsValidIndex(MapIndex)) { Node->MapIndexExpression = Parts[MapIndex]; }
						int32 Coordinate = 0;
						if (ParseInt(Parts[XIndex], Coordinate)) { Node->Position.X = Coordinate; }
						else { Node->PositionXExpression = Parts[XIndex]; }
						if (ParseInt(Parts[YIndex], Coordinate)) { Node->Position.Y = Coordinate; }
						else { Node->PositionYExpression = Parts[YIndex]; }
						Nodes.Add(Node);
						++State.Result->NodesCreated;
						continue;
					}
				}
			}

			// mob.spawn(vnum, x, y) / d.spawn_mob(vnum, x, y)
			if (ExtractCallArguments(Line, TEXT("mob.spawn"), Arguments) ||
				ExtractCallArguments(Line, TEXT("d.spawn_mob"), Arguments) ||
				ExtractCallArguments(Line, TEXT("mob.spawn_group"), Arguments))
			{
				const TArray<FString> Parts = SplitArguments(Arguments);
				FString Reason;
				if (Parts.Num() >= 1 && CanConvertExpression(Parts[0], State, Reason))
				{
					FlushSay();
					UMT2QuestNode_SpawnMob* Node = NewObject<UMT2QuestNode_SpawnMob>(State.Outer);
					int32 SpawnVnum = 0;
					if (ParseInt(Parts[0], SpawnVnum)) { Node->MobVnum = SpawnVnum; }
					else { Node->MobVnumExpression = Parts[0]; }
					// Literal coordinates only; a computed position falls back to spawning at the player.
					int32 Coordinate = 0;
					if (Parts.Num() >= 3)
					{
						if (ParseInt(Parts[1], Coordinate)) { Node->Position.X = Coordinate; }
						if (ParseInt(Parts[2], Coordinate)) { Node->Position.Y = Coordinate; }
					}
					Nodes.Add(Node);
					++State.Result->NodesCreated;
					continue;
				}
			}

			// game.drop_item(vnum, count) / game.drop_item_with_ownership(vnum, count)
			const bool bOwnedDrop = ExtractCallArguments(
				Line, TEXT("game.drop_item_with_ownership"), Arguments);
			if (bOwnedDrop || ExtractCallArguments(Line, TEXT("game.drop_item"), Arguments) ||
				ExtractCallArguments(Line, TEXT("pc.drop_item"), Arguments))
			{
				const TArray<FString> Parts = SplitArguments(Arguments);
				FString Reason;
				if (Parts.Num() >= 1 && CanConvertExpression(Parts[0], State, Reason))
				{
					FlushSay();
					UMT2QuestNode_DropItem* Node = NewObject<UMT2QuestNode_DropItem>(State.Outer);
					int32 DropVnum = 0;
					if (ParseInt(Parts[0], DropVnum)) { Node->ItemVnum = DropVnum; }
					else { Node->ItemVnumExpression = Parts[0]; }
					int32 DropCount = 1;
					if (Parts.Num() >= 2 && ParseInt(Parts[1], DropCount))
					{
						Node->Count = FMath::Max(DropCount, 1);
					}
					else if (Parts.Num() >= 2 && CanConvertExpression(Parts[1], State, Reason))
					{
						Node->CountExpression = Parts[1];
					}
					Node->bOwnedByPlayer = bOwnedDrop;
					Nodes.Add(Node);
					++State.Result->NodesCreated;
					continue;
				}
			}

			// Fold the copy + exact material-removal loop into one server-side transaction. Keeping
			// them separate would let dialog-time inventory changes produce partial consumption.
			if (ExtractCallArguments(Line, TEXT("item.copy_and_give_before_remove"), Arguments))
			{
				const TArray<FString> Parts = SplitArguments(Arguments);
				int32 LoopStart = Index + 1;
				while (LoopStart < End && NormalizeStatement(Lines[LoopStart]).IsEmpty()) { ++LoopStart; }
				const FString Header = LoopStart < End ? NormalizeStatement(Lines[LoopStart]) : FString();
				FRegexMatcher LoopHeader(FRegexPattern(TEXT("^for\\s+([A-Za-z_][A-Za-z_0-9]*)\\s*,\\s*([A-Za-z_][A-Za-z_0-9]*)\\s+in\\s+ipairs\\s*\\((.*)\\)\\s+do$")), Header);
				FString Reason;
				bool bConverted = false;
				if (Parts.Num() == 1 && CanConvertExpression(Parts[0], State, Reason) && LoopHeader.FindNext())
				{
					int32 UnusedElse = INDEX_NONE;
					const int32 LoopEnd = FindMatchingEnd(Lines, LoopStart, UnusedElse);
					int32 BodyLine = LoopStart + 1;
					while (BodyLine < End && NormalizeStatement(Lines[BodyLine]).IsEmpty()) { ++BodyLine; }
					int32 AfterBody = BodyLine + 1;
					while (AfterBody < End && NormalizeStatement(Lines[AfterBody]).IsEmpty()) { ++AfterBody; }
					const FString ValueVariable = LoopHeader.GetCaptureGroup(2);
					const FString MaterialTable = LoopHeader.GetCaptureGroup(3).TrimStartAndEnd();
					FString Removal = BodyLine < End ? NormalizeStatement(Lines[BodyLine]) : FString();
					Removal.ReplaceInline(TEXT(" "), TEXT(""));
					Removal.ReplaceInline(TEXT("\t"), TEXT(""));
					if (LoopEnd != INDEX_NONE && LoopEnd < End && AfterBody == LoopEnd &&
						Removal == FString::Printf(TEXT("pc.remove_item(%s.vnum,%s.count)"), *ValueVariable, *ValueVariable) &&
						CanConvertExpression(MaterialTable, State, Reason))
					{
						FlushSay();
						UMT2QuestNode_CopyItem* Node = NewObject<UMT2QuestNode_CopyItem>(State.Outer);
						Node->ResultVnumExpression = Parts[0];
						Node->MaterialTableExpression = MaterialTable;
						Nodes.Add(Node);
						++State.Result->NodesCreated;
						Index = LoopEnd;
						bConverted = true;
					}
				}
				if (!bConverted) { RecordUnconverted(State, Nodes, Raw, Index); }
				continue;
			}

			// item.remove() - consume the item this event came from.
			if (ExtractCallArguments(Line, TEXT("item.remove"), Arguments))
			{
				FlushSay();
				Nodes.Add(NewObject<UMT2QuestNode_RemoveUsedItem>(State.Outer));
				++State.Result->NodesCreated;
				continue;
			}
			if (ExtractCallArguments(Line, TEXT("item.set_value"), Arguments))
			{
				const TArray<FString> Parts = SplitArguments(Arguments);
				FString Reason;
				if (Parts.Num() >= 3 && CanConvertExpression(Parts[0], State, Reason) &&
					CanConvertExpression(Parts[1], State, Reason) &&
					CanConvertExpression(Parts[2], State, Reason))
				{
					FlushSay();
					UMT2QuestNode_SetItemAttribute* Node =
						NewObject<UMT2QuestNode_SetItemAttribute>(State.Outer);
					Node->IndexExpression = Parts[0];
					Node->ApplyTypeExpression = Parts[1];
					Node->ValueExpression = Parts[2];
					Nodes.Add(Node);
					++State.Result->NodesCreated;
					continue;
				}
			}

			// q.set_clock("label", value) - the journal's countdown display.
			if (ExtractCallArguments(Line, TEXT("q.set_clock"), Arguments))
			{
				const TArray<FString> Parts = SplitArguments(Arguments);
				bool bResolved = false;
				FlushSay();
				if (Parts.Num() >= 1)
				{
					UMT2QuestNode_Journal* LabelNode = NewObject<UMT2QuestNode_Journal>(State.Outer);
					LabelNode->Operation = EMT2QuestJournalOp::SetSummary;
					LabelNode->Text = FText::FromString(ResolveDisplayText(Parts[0], *State.Locale));
					Nodes.Add(LabelNode);
					++State.Result->NodesCreated;
				}
				if (Parts.Num() >= 2)
				{
					FString Reason;
					int32 ClockValue = 0;
					UMT2QuestNode_Journal* ValueNode = NewObject<UMT2QuestNode_Journal>(State.Outer);
					ValueNode->Operation = EMT2QuestJournalOp::SetCounter;
					if (ParseInt(Parts[1], ClockValue)) { ValueNode->CounterValue = ClockValue; }
					else if (CanConvertExpression(Parts[1], State, Reason))
					{
						ValueNode->CounterExpression = Parts[1];
					}
					Nodes.Add(ValueNode);
					++State.Result->NodesCreated;
				}
				continue;
			}
			if (ExtractCallArguments(Line, TEXT("notice"), Arguments) ||
				ExtractCallArguments(Line, TEXT("notice_multiline"), Arguments) ||
				ExtractCallArguments(Line, TEXT("chat"), Arguments) ||
				ExtractCallArguments(Line, TEXT("d.notice"), Arguments))
			{
				bool bResolved = false;
				FlushSay();
				UMT2QuestNode_Notice* Node = NewObject<UMT2QuestNode_Notice>(State.Outer);
				Node->Message = FText::FromString(ResolveDisplayText(Arguments, *State.Locale));
				Nodes.Add(Node);
				++State.Result->NodesCreated;
				continue;
			}
			// Lua return either exits the current quest-local function or, at top level, the trigger.
			if (Line == TEXT("return") || Line.StartsWith(TEXT("return ")))
			{
				FlushSay();
				UMT2QuestNode_Return* Node = NewObject<UMT2QuestNode_Return>(State.Outer);
				if (Line.Len() > 6)
				{
					TArray<FString> ReturnExpressions = SplitArguments(Line.Mid(6).TrimStartAndEnd());
					TArray<TObjectPtr<UMT2QuestNode>> Prelude;
					FString Reason;
					bool bConvertible = !ReturnExpressions.IsEmpty();
					for (FString& Expression : ReturnExpressions)
					{
						Expression = ExpandPureFunctionCalls(Lines, Expression, State);
						// Preserve tuple tail semantics for native calls. Resumable quest calls in a list
						// still need ordered capture rather than hoisting every RHS before evaluation.
						bConvertible &= ReturnExpressions.Num() == 1
							? ExtractRuntimeFunctionCalls(Lines, Expression, State, Prelude) && CanConvertExpression(Expression, State, Reason)
							: CanConvertExpression(Expression, State, Reason);
					}
					if (!bConvertible)
					{
						RecordUnconverted(State, Nodes, Raw, Index);
						continue;
					}
					Node->ValueExpressions = MoveTemp(ReturnExpressions);
					Nodes.Append(Prelude);
				}
				Nodes.Add(Node);
				++State.Result->NodesCreated;
				continue;
			}
			if (Line == TEXT("end") || Line == TEXT("else"))
			{
				continue; // block punctuation
			}

			// ---- variable assignment: "local x = <expr>" or "x = <expr>" ----
			{
				FString Assignment = Line;
				const bool bLocal = Assignment.StartsWith(TEXT("local "));
				if (bLocal)
				{
					Assignment.RemoveFromStart(TEXT("local "));
				}

				// A bare declaration ("local pass_percent") introduces the name with no value. Emit it as
				// nil so type/truthiness checks see an actual uninitialized Lua local.
				if (bLocal && !Assignment.Contains(TEXT("=")) && !Assignment.Contains(TEXT("(")))
				{
					const TArray<FString> DeclaredNames = SplitArguments(Assignment);
					bool bValidDeclaration = !DeclaredNames.IsEmpty();
					for (const FString& DeclaredName : DeclaredNames)
					{
						bValidDeclaration &= !DeclaredName.IsEmpty() && !DeclaredName.Contains(TEXT(" ")) &&
							!DeclaredName.Contains(TEXT(".")) && !DeclaredName.Contains(TEXT("["));
					}
					if (bValidDeclaration)
					{
						FlushSay();
						for (const FString& DeclaredName : DeclaredNames)
						{
							UMT2QuestNode_SetVariable* Node =
								NewObject<UMT2QuestNode_SetVariable>(State.Outer);
							Node->VariableName = FName(*DeclaredName);
							Node->Expression = TEXT("nil");
							Nodes.Add(Node);
							++State.Result->NodesCreated;
							State.KnownVariables.Add(DeclaredName);
						}
						continue;
					}
				}
				int32 EqualsIndex = INDEX_NONE;
				if (Assignment.FindChar(TEXT('='), EqualsIndex) && EqualsIndex > 0 &&
					// A lone '=' (not ==, >=, <=, ~=, !=) is an assignment.
					Assignment[EqualsIndex - 1] != TEXT('=') && Assignment[EqualsIndex - 1] != TEXT('>') &&
					Assignment[EqualsIndex - 1] != TEXT('<') && Assignment[EqualsIndex - 1] != TEXT('~') &&
					Assignment[EqualsIndex - 1] != TEXT('!') &&
					(EqualsIndex + 1 >= Assignment.Len() || Assignment[EqualsIndex + 1] != TEXT('=')))
				{
					const FString VariableName = Assignment.Left(EqualsIndex).TrimStartAndEnd();
					FString ValueExpression = Assignment.Mid(EqualsIndex + 1).TrimStartAndEnd();
					const TArray<FString> VariableNames = SplitArguments(VariableName);
					const TArray<FString> ValueExpressions = SplitArguments(ValueExpression);
					if (VariableNames.Num() > 1 || ValueExpressions.Num() > 1)
					{
						bool bConvertible = true;
						for (const FString& Name : VariableNames)
						{
							bConvertible &= !Name.IsEmpty() && (FChar::IsAlpha(Name[0]) || Name[0] == TEXT('_'));
							for (TCHAR Character : Name) { bConvertible &= FChar::IsAlnum(Character) || Character == TEXT('_'); }
						}
						TArray<FString> Expressions;
						for (const FString& Expression : ValueExpressions)
						{
							const FString Resolved = ResolveStaticTableConstants(Expression, *State.Locale);
							FString Reason;
							bConvertible &= CanConvertExpression(Resolved, State, Reason);
							Expressions.Add(Resolved);
						}
						if (bConvertible)
						{
							FlushSay();
							UMT2QuestNode_AssignValues* Node = NewObject<UMT2QuestNode_AssignValues>(State.Outer);
							Node->Expressions = MoveTemp(Expressions);
							for (const FString& Name : VariableNames)
							{
								Node->VariableNames.Add(FName(*Name));
								Node->QuestScopedTargets.Add(!bLocal && State.GlobalVariables && State.GlobalVariables->Contains(Name));
								State.KnownVariables.Add(Name);
							}
							Nodes.Add(Node); ++State.Result->NodesCreated;
							continue;
						}
					}
					FString Reason;
					FString InputArguments;
					const bool bNumericInput = ExtractCallArguments(
						ValueExpression, TEXT("input_number"), InputArguments);
					const bool bTextInput = !bNumericInput && ExtractCallArguments(
						ValueExpression, TEXT("input"), InputArguments);
					if ((bNumericInput || (bTextInput && InputArguments.TrimStartAndEnd().IsEmpty())) &&
						VariableNames.Num() == 1 && !VariableName.IsEmpty() && !VariableName.Contains(TEXT(".")) &&
						!VariableName.Contains(TEXT("[")))
					{
						if (bNumericInput && !InputArguments.TrimStartAndEnd().IsEmpty())
						{
							EnsureSay()->Lines.Append(SplitDialogLines(
								ResolveDisplayText(InputArguments, *State.Locale)));
						}
						FlushSay();
						UMT2QuestNode_Input* Node = NewObject<UMT2QuestNode_Input>(State.Outer);
						Node->ResultVariable = FName(*VariableName);
						Node->bNumericOnly = bNumericInput;
						Nodes.Add(Node);
						++State.Result->NodesCreated;
						State.KnownVariables.Add(VariableName);
						continue;
					}

					// Assigning a locale key ("x = gameforge.quest._10_say") is a dotted name the
					// evaluator can't resolve, but the text is known at import time - bake it in as a
					// string literal so the variable still carries the right value.
					if (ValueExpression.Contains(TEXT(".")) && !ValueExpression.Contains(TEXT("(")))
					{
						bool bLocaleResolved = false;
						const FString LocaleText = ResolveText(ValueExpression, *State.Locale, bLocaleResolved);
						if (bLocaleResolved)
						{
							ValueExpression = LocaleText.IsNumeric()
								? LocaleText
								: FString::Printf(TEXT("\"%s\""), *LocaleText.ReplaceCharWithEscapedChar());
						}
					}
					ValueExpression = ResolveStaticTableConstants(ValueExpression, *State.Locale);
					// A simple name (not a table field) with an expression we can evaluate.
					TArray<TObjectPtr<UMT2QuestNode>> Prelude;
					if (VariableNames.Num() == 1 && !VariableName.IsEmpty() && !VariableName.Contains(TEXT(".")) &&
						!VariableName.Contains(TEXT("[")) &&
						ExtractRuntimeFunctionCalls(Lines, ValueExpression, State, Prelude) &&
						CanConvertExpression(ValueExpression, State, Reason))
					{
						FlushSay();
						Nodes.Append(Prelude);
						UMT2QuestNode_SetVariable* Node = NewObject<UMT2QuestNode_SetVariable>(State.Outer);
						Node->VariableName = FName(*VariableName);
						Node->Expression = ValueExpression;
						Node->bQuestScoped = !bLocal && State.GlobalVariables &&
							State.GlobalVariables->Contains(VariableName);
						Nodes.Add(Node);
						++State.Result->NodesCreated;
						// Later expressions in this block may now reference it.
						State.KnownVariables.Add(VariableName);
						continue;
					}
				}
			}

			// A subroutine this quest defines; represented by a runtime function frame.
			{
				TArray<TObjectPtr<UMT2QuestNode>> Inlined;
				MT2QuestExpressionSyntax::FParser CallParser(Line);
				const auto CallTree = CallParser.Expression();
				FString CallExpression = Line;
				if (CallParser.bOk && CallParser.Cursor == CallParser.Tokens.Num() && CallTree &&
					CallTree->Kind == MT2QuestExpressionSyntax::EKind::Call &&
					ExtractRuntimeFunctionCalls(Lines, CallExpression, State, Inlined) && !Inlined.IsEmpty())
				{
					FlushSay();
					Nodes.Append(Inlined);
					continue;
				}
				if (TranslateScriptFunctionCall(Lines, Line, State, Inlined))
				{
					FlushSay();
					Nodes.Append(MoveTemp(Inlined));
					continue;
				}
			}

			// A pure function whose return value is discarded has no Lua side effect.
			for (const TCHAR* PureFunction : {
				TEXT("math.ceil"), TEXT("math.floor"), TEXT("math.abs"), TEXT("math.min"),
				TEXT("math.max"), TEXT("table.getn"), TEXT("tonumber")})
			{
				FString PureArguments;
				if (ExtractCallArguments(Line, PureFunction, PureArguments) &&
					Line.TrimStartAndEnd().StartsWith(PureFunction))
				{
					Line.Reset();
					break;
				}
			}
			if (Line.IsEmpty()) { continue; }

			FlushSay();
			RecordUnconverted(State, Nodes, Raw, Index);
		}
		return Nodes;
	}

	// ---------------------------------------------------------------------------------------------
	// Blueprint emission
	// ---------------------------------------------------------------------------------------------

	UBlueprint* CreateOrLoadQuestBlueprint(
		const FString& PackagePath, const FString& AssetName, bool& bOutCreated)
	{
		const FString PackageName = PackagePath / AssetName;
		const FString ObjectPath = PackageName + TEXT(".") + AssetName;

		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
		bOutCreated = (Blueprint == nullptr);
		if (!Blueprint)
		{
			UPackage* Package = CreatePackage(*PackageName);
			Blueprint = FKismetEditorUtilities::CreateBlueprint(
				UMT2Quest::StaticClass(), Package, *AssetName, BPTYPE_Normal,
				UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), TEXT("MT2QuestImporter"));
			if (!Blueprint)
			{
				return nullptr;
			}
			// The generated class and its CDO only exist after the first compile.
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
			FAssetRegistryModule::AssetCreated(Blueprint);
		}
		return Blueprint;
	}

	struct FClientMapDefinition
	{
		FString MapName;
		FVector2D GlobalOrigin = FVector2D::ZeroVector;
		FVector2D WorldSize = FVector2D::ZeroVector;
	};

	void LoadClientMapDefinitions(
		const FString& ClientSourceRoot, TArray<FClientMapDefinition>& OutMaps)
	{
		TArray<FString> AtlasLines;
		if (ClientSourceRoot.IsEmpty() || !FFileHelper::LoadFileToStringArray(
			AtlasLines, *(ClientSourceRoot / UMT2PathSettings::Path(TEXT("Part_atlasinfo")))))
		{
			return;
		}

		for (const FString& RawLine : AtlasLines)
		{
			TArray<FString> Parts;
			StripComment(RawLine).ParseIntoArrayWS(Parts);
			if (Parts.Num() < 5 || !Parts[1].IsNumeric() || !Parts[2].IsNumeric() ||
				!Parts[3].IsNumeric() || !Parts[4].IsNumeric())
			{
				continue;
			}

			FClientMapDefinition& Map = OutMaps.AddDefaulted_GetRef();
			Map.MapName = Parts[0];
			Map.GlobalOrigin = FVector2D(FCString::Atod(*Parts[1]), FCString::Atod(*Parts[2]));
			Map.WorldSize = FVector2D(FCString::Atod(*Parts[3]), FCString::Atod(*Parts[4])) *
				(128.0 * 200.0);
		}
	}

	void LoadQuestMapDefinitions(
		const FString& LocaleRoot, const FString& ClientSourceRoot,
		TArray<FMT2QuestMapDefinition>& OutMaps)
	{
		TArray<FClientMapDefinition> ClientMaps;
		LoadClientMapDefinitions(ClientSourceRoot, ClientMaps);

		TArray<FString> IndexLines;
		if (!FFileHelper::LoadFileToStringArray(IndexLines, *(LocaleRoot / UMT2PathSettings::Path(TEXT("Part_map")) / UMT2PathSettings::Path(TEXT("Part_index")))))
		{
			return;
		}

		for (const FString& RawIndexLine : IndexLines)
		{
			TArray<FString> IndexParts;
			StripComment(RawIndexLine).ParseIntoArrayWS(IndexParts);
			if (IndexParts.Num() < 2 || !IndexParts[0].IsNumeric())
			{
				continue;
			}

			TArray<FString> SettingLines;
			if (!FFileHelper::LoadFileToStringArray(
				SettingLines, *(LocaleRoot / UMT2PathSettings::Path(TEXT("Part_map")) / IndexParts[1] / UMT2PathSettings::Path(TEXT("Part_Setting")))))
			{
				continue;
			}

			FIntPoint BasePosition = FIntPoint::ZeroValue;
			FIntPoint MapSize = FIntPoint::ZeroValue;
			double CellScale = 0.0;
			for (const FString& RawSettingLine : SettingLines)
			{
				TArray<FString> Parts;
				StripComment(RawSettingLine).ParseIntoArrayWS(Parts);
				if (Parts.Num() >= 3 && Parts[0].Equals(TEXT("BasePosition"), ESearchCase::IgnoreCase))
				{
					BasePosition = FIntPoint(FCString::Atoi(*Parts[1]), FCString::Atoi(*Parts[2]));
				}
				else if (Parts.Num() >= 3 && Parts[0].Equals(TEXT("MapSize"), ESearchCase::IgnoreCase))
				{
					MapSize = FIntPoint(FCString::Atoi(*Parts[1]), FCString::Atoi(*Parts[2]));
				}
				else if (Parts.Num() >= 2 && Parts[0].Equals(TEXT("CellScale"), ESearchCase::IgnoreCase))
				{
					CellScale = FCString::Atod(*Parts[1]);
				}
			}

			if (MapSize.X <= 0 || MapSize.Y <= 0 || CellScale <= 0.0)
			{
				continue;
			}
			FMT2QuestMapDefinition& Map = OutMaps.AddDefaulted_GetRef();
			Map.MapIndex = FCString::Atoi(*IndexParts[0]);
			Map.MapId = IndexParts[1];
			Map.GlobalOrigin = FVector2D(BasePosition.X, BasePosition.Y);
			Map.WorldSize = FVector2D(MapSize.X, MapSize.Y) * (128.0 * CellScale);
			Map.WorldPackagePath = UMT2PathSettings::Format(TEXT("GameMapTemplate"), TEXT("%s"), *FPaths::GetCleanFilename(Map.MapId));

			const FClientMapDefinition* ClientMap = ClientMaps.FindByPredicate(
				[&Map](const FClientMapDefinition& Candidate)
				{
					return Candidate.GlobalOrigin.Equals(Map.GlobalOrigin, 1.0) &&
						Candidate.WorldSize.Equals(Map.WorldSize, 1.0);
				});
			if (ClientMap)
			{
				Map.WorldPackagePath = UMT2PathSettings::Format(TEXT("GameMapTemplate"), TEXT("%s"), *FPaths::GetCleanFilename(ClientMap->MapName));
			}
		}
	}
}

bool FMT2QuestImporter::Import(
	const FString& QuestSourceRoot, const FString& DestinationRoot, FMT2QuestImportResult& OutResult,
	const FString& ClientSourceRoot)
{
	const FString SourceRoot = QuestSourceRoot.TrimStartAndEnd();
	if (!IFileManager::Get().DirectoryExists(*SourceRoot))
	{
		OutResult.Errors.Add(FString::Printf(TEXT("Quest source folder not found: %s"), *SourceRoot));
		return false;
	}

	// Dialog text lives in the locale's translate.lua, normally one level above the quest folder.
	TMap<FString, FString> Locale;
	const FString LocaleRoot = FPaths::GetPath(SourceRoot.TrimChar(TEXT('/')));
	for (const FString& Candidate : {SourceRoot / UMT2PathSettings::Path(TEXT("Part_translate")), LocaleRoot / UMT2PathSettings::Path(TEXT("Part_translate"))})
	{
		if (Locale.IsEmpty())
		{
			LoadLocaleTable(Candidate, Locale);
		}
	}
	LoadLocaleAliases(SourceRoot / UMT2PathSettings::Path(TEXT("Part_locale")), Locale);
	OutResult.LocaleStringsLoaded = Locale.Num();
	if (Locale.IsEmpty())
	{
		OutResult.Warnings.Add(
			TEXT("translate.lua not found; quests will carry raw locale keys instead of readable text."));
	}

	// The original quest manager resolves symbolic NPC names through questnpc.txt before dispatching
	// click/chat events. Import the same registry instead of leaving those triggers with vnum zero.
	TMap<FString, int32> QuestNpcAliases;
	LoadQuestNpcAliases(SourceRoot / UMT2PathSettings::Path(TEXT("Part_questnpc")), QuestNpcAliases);

	TArray<FString> ScriptFiles;
	const FString ActiveQuestList = SourceRoot / UMT2PathSettings::Path(TEXT("Part_locale_list"));
	TArray<FString> ActiveQuestEntries;
	if (FFileHelper::LoadFileToStringArray(ActiveQuestEntries, *ActiveQuestList))
	{
		for (const FString& RawEntry : ActiveQuestEntries)
		{
			const FString Entry = StripComment(RawEntry).TrimStartAndEnd();
			if (Entry.IsEmpty())
			{
				continue;
			}
			const FString ScriptFile = FPaths::ConvertRelativePathToFull(SourceRoot / Entry);
			if (IFileManager::Get().FileExists(*ScriptFile))
			{
				ScriptFiles.Add(ScriptFile);
			}
			else
			{
				OutResult.Warnings.Add(FString::Printf(
					TEXT("Active quest '%s' listed by locale_list does not exist."), *Entry));
			}
		}
	}
	else
	{
		IFileManager::Get().FindFilesRecursive(ScriptFiles, *SourceRoot, TEXT("*.quest"), true, false);
		OutResult.Warnings.Add(TEXT("locale_list not found; importing every .quest file as a fallback."));
	}
	if (ScriptFiles.IsEmpty())
	{
		OutResult.Errors.Add(FString::Printf(TEXT("No .quest files found under %s"), *SourceRoot));
		return false;
	}

	const FString QuestPackagePath = DestinationRoot / UMT2PathSettings::Path(TEXT("Part_Quests"));
	TArray<UPackage*> PackagesToSave;
	UMT2QuestTableAsset* SharedTableAsset = nullptr;
	TMap<FString, FString> LibraryConstants;
	TSet<FString> InitializedQuestNames;
	ParseLuaLibraryScalars(SourceRoot / UMT2PathSettings::Path(TEXT("Part_questlib")), LibraryConstants);

	// The constant tables the scripts index (special.levelup_quest, special.questscroll, ...) are
	// converted first: the expression validator consults them to decide whether a table read can be
	// converted, so they have to exist before any quest is translated.
	{
		TArray<FMT2QuestTable> Tables;
		TArray<FMT2QuestMapDefinition> Maps;
		for (const FString& LibraryName : {TEXT("questlib.lua"), TEXT("locale.lua"), TEXT("questing.lua")})
		{
			ParseLuaTables(SourceRoot / LibraryName, Tables);
		}
		LoadQuestMapDefinitions(LocaleRoot, ClientSourceRoot, Maps);
		if (!Tables.IsEmpty() || !Maps.IsEmpty())
		{
			const FString TablePackageName = QuestPackagePath / UMT2PathSettings::Path(TEXT("Part_DA_MT2QuestTables"));
			UPackage* TablePackage = CreatePackage(*TablePackageName);
			UMT2QuestTableAsset* TableAsset = FindObject<UMT2QuestTableAsset>(
				TablePackage, TEXT("DA_MT2QuestTables"));
			if (!TableAsset)
			{
				TableAsset = NewObject<UMT2QuestTableAsset>(
					TablePackage, TEXT("DA_MT2QuestTables"), RF_Public | RF_Standalone);
				FAssetRegistryModule::AssetCreated(TableAsset);
			}
			TableAsset->Modify();
			TableAsset->Tables = MoveTemp(Tables);
			TableAsset->Maps = MoveTemp(Maps);
			SharedTableAsset = TableAsset;
			TablePackage->MarkPackageDirty();
			PackagesToSave.Add(TablePackage);
			OutResult.Warnings.Add(FString::Printf(
				TEXT("Converted %d Lua constant tables and %d map definitions."),
				TableAsset->Tables.Num(), TableAsset->Maps.Num()));
		}
	}

	for (const FString& ScriptFile : ScriptFiles)
	{
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *ScriptFile))
		{
			OutResult.Warnings.Add(FString::Printf(TEXT("Could not read %s"), *ScriptFile));
			continue;
		}
		++OutResult.ScriptsParsed;

		FParsedQuest Parsed;
		const FString ScriptName = FPaths::GetBaseFilename(ScriptFile);
		if (!ParseQuestScript(Lines, Parsed) || Parsed.States.IsEmpty())
		{
			OutResult.Warnings.Add(FString::Printf(TEXT("%s: no quest/state block found."), *ScriptName));
			continue;
		}
		if (Parsed.Name.IsEmpty())
		{
			Parsed.Name = ScriptName;
		}
		for (const TPair<FString, FString>& Constant : LibraryConstants)
		{
			// A quest-local declaration shadows the library value, matching Lua lookup rules.
			if (!Parsed.Constants.ContainsByPredicate(
				[&Constant](const TPair<FString, FString>& Existing)
				{ return Existing.Key == Constant.Key; }))
			{
				Parsed.Constants.Emplace(Constant.Key, Constant.Value);
			}
		}

		const FString AssetName = TEXT("BP_Quest_") + Parsed.Name;
		bool bCreated = false;
		UBlueprint* Blueprint = CreateOrLoadQuestBlueprint(QuestPackagePath, AssetName, bCreated);
		if (!Blueprint || !Blueprint->GeneratedClass)
		{
			OutResult.Warnings.Add(FString::Printf(TEXT("%s: could not create quest Blueprint."), *ScriptName));
			continue;
		}
		UMT2Quest* Defaults = Cast<UMT2Quest>(Blueprint->GeneratedClass->GetDefaultObject());
		if (!Defaults)
		{
			continue;
		}

		FTranslateState State;
		State.Locale = &Locale;
		State.Functions = &Parsed.Functions;
		State.GlobalVariables = &Parsed.GlobalVariables;
		State.Outer = Defaults;
		State.ScriptName = ScriptName;
		State.QuestName = Parsed.Name;
		State.Result = &OutResult;

		const bool bFirstQuestPart = !InitializedQuestNames.Contains(Parsed.Name);
		InitializedQuestNames.Add(Parsed.Name);
		Defaults->Modify();
		Defaults->QuestName = FName(*Parsed.Name);
		Defaults->DisplayName = FText::FromString(Parsed.Name);
		if (bFirstQuestPart)
		{
			Defaults->States.Reset();
			Defaults->Constants.Reset();
		}

		// Quest-scope constants are seeded before any block runs, so register them as known variables
		// first - expressions inside the triggers reference them by name.
		for (const TPair<FString, FString>& Constant : Parsed.Constants)
		{
			State.KnownVariables.Add(Constant.Key);
		}
		State.KnownVariables.Append(Parsed.GlobalVariables);
		for (const TPair<FString, FString>& Constant : Parsed.Constants)
		{
			FString Reason;
			if (!CanConvertExpression(Constant.Value, State, Reason))
			{
				continue; // a constant we can't evaluate stays unset; dependent expressions stay TODOs
			}
			FMT2QuestConstant* Existing = Defaults->Constants.FindByPredicate(
				[&Constant](const FMT2QuestConstant& Entry)
				{
					return Entry.Name == FName(*Constant.Key);
				});
			FMT2QuestConstant& Entry = Existing
				? *Existing : Defaults->Constants.AddDefaulted_GetRef();
			Entry.Name = FName(*Constant.Key);
			Entry.Expression = Constant.Value;
		}
		// Only constants we actually captured may be referenced.
		State.KnownVariables.Reset();
		for (const FMT2QuestConstant& Constant : Defaults->Constants)
		{
			State.KnownVariables.Add(Constant.Name.ToString());
		}
		State.KnownVariables.Append(Parsed.GlobalVariables);

		// Numeric constants double as NPC/mob vnums in trigger specs (`when MOB1_1.kill`).
		TMap<FString, int32> NamedVnums = QuestNpcAliases;
		for (const TPair<FString, FString>& Constant : Parsed.Constants)
		{
			int32 Numeric = 0;
			if (ParseInt(Constant.Value, Numeric))
			{
				NamedVnums.Add(Constant.Key, Numeric);
			}
		}

		for (const FParsedState& ParsedState : Parsed.States)
		{
			FMT2QuestState* ExistingState = Defaults->States.FindByPredicate(
				[&ParsedState](const FMT2QuestState& Entry)
				{
					return Entry.StateName == FName(*ParsedState.Name);
				});
			FMT2QuestState& QuestState = ExistingState
				? *ExistingState : Defaults->States.AddDefaulted_GetRef();
			QuestState.StateName = FName(*ParsedState.Name);
			for (const FParsedTrigger& ParsedTrigger : ParsedState.Triggers)
			{
				// `when a or b with <cond>` becomes one trigger per event, all sharing the gate.
				TArray<FString> EventSpecs;
				FString WithCondition;
				SplitTriggerSpec(ParsedTrigger.Spec, EventSpecs, WithCondition);
				const FString OriginalWithCondition = WithCondition;

				// The block is translated once and shared by every event it fires on.
				State.KnownVariables.Reset();
				for (const FMT2QuestConstant& Constant : Defaults->Constants)
				{
					State.KnownVariables.Add(Constant.Name.ToString());
				}
				State.KnownVariables.Append(Parsed.GlobalVariables);
				const int32 StatementsBefore = OutResult.StatementsUnconverted;
				const TArray<TObjectPtr<UMT2QuestNode>> BlockNodes =
					TranslateBlock(Lines, ParsedTrigger.BodyStart, ParsedTrigger.BodyEnd, State);
				const bool bIncompleteRecoveredTrigger = ParsedTrigger.bMultilineHeader &&
					OutResult.StatementsUnconverted != StatementsBefore;

				for (const FString& EventSpec : EventSpecs)
				{
					WithCondition = OriginalWithCondition;
					// A gate is a separate Lua chunk: body locals do not exist while probing it.
					State.KnownVariables.Reset();
					for (const FMT2QuestConstant& Constant : Defaults->Constants)
					{
						State.KnownVariables.Add(Constant.Name.ToString());
					}
					State.KnownVariables.Append(Parsed.GlobalVariables);
				EMT2QuestEvent Event = EMT2QuestEvent::Click;
				int32 Vnum = 0;
				FText ChatOption;
				FName TriggerName;
				if (!ParseTriggerSpec(EventSpec, Locale, Event, Vnum, ChatOption, &NamedVnums, &TriggerName))
				{
					OutResult.ConversionReport.Add(FString::Printf(
						TEXT("%s: unsupported trigger '%s'"), *ScriptName, *EventSpec));
					continue;
				}
				// A click/chat trigger that names its NPC instead of using a vnum can't be resolved here.
				// It is written out inert (dispatch ignores vnum 0 for NPC events) and reported, so the
				// NPC vnum can be filled in on the trigger in the editor.
				if ((Event == EMT2QuestEvent::Click || Event == EMT2QuestEvent::Chat) && Vnum <= 0)
				{
					OutResult.ConversionReport.Add(FString::Printf(
						TEXT("%s: trigger '%s' needs an NPC vnum (named NPC could not be resolved)"),
						*ScriptName, *ParsedTrigger.Spec));
				}
				FMT2QuestTrigger& Trigger = QuestState.Triggers.AddDefaulted_GetRef();
				Trigger.Event = Event;
				Trigger.Vnum = Vnum;
				Trigger.TriggerName = TriggerName;
				Trigger.ChatOption = ChatOption;
				Trigger.Nodes = BlockNodes;
				if (bIncompleteRecoveredTrigger)
				{
					// Previously omitted triggers must not introduce partially converted side effects.
					// Keep their nodes and diagnostics visible, but require a complete body to enable them.
					UMT2QuestCondition_Expression* Disabled =
						NewObject<UMT2QuestCondition_Expression>(State.Outer);
					Disabled->Expression = TEXT("false");
					Trigger.Conditions.Add(Disabled);
				}

				// The "with" clause gates the trigger.
				if (!WithCondition.IsEmpty())
				{
					WithCondition = ExpandPureFunctionCalls(Lines, WithCondition, State);
					TArray<TObjectPtr<UMT2QuestNode>> FunctionPrelude;
					if (!ExtractRuntimeFunctionCalls(Lines, WithCondition, State, FunctionPrelude))
					{
						OutResult.ConversionReport.Add(FString::Printf(
							TEXT("%s: trigger gate 'with %s' could not be converted"),
							*ScriptName, *WithCondition));
						UMT2QuestCondition_Expression* Disabled =
							NewObject<UMT2QuestCondition_Expression>(State.Outer);
						Disabled->Expression = TEXT("false");
						Trigger.Conditions.Add(Disabled);
					}
					else if (FunctionPrelude.IsEmpty())
					{
						if (!TranslateCondition(WithCondition, State, Trigger.Conditions))
						{
							OutResult.ConversionReport.Add(FString::Printf(
								TEXT("%s: trigger gate 'with %s' could not be converted"),
								*ScriptName, *WithCondition));
							UMT2QuestCondition_Expression* Disabled =
								NewObject<UMT2QuestCondition_Expression>(State.Outer);
							Disabled->Expression = TEXT("false");
							Trigger.Conditions.Add(Disabled);
						}
					}
					else
					{
						UMT2QuestNode_If* GateNode = NewObject<UMT2QuestNode_If>(State.Outer);
						if (!TranslateCondition(WithCondition, State, GateNode->Test))
						{
							OutResult.ConversionReport.Add(FString::Printf(
								TEXT("%s: trigger gate 'with %s' could not be converted"),
								*ScriptName, *WithCondition));
							UMT2QuestCondition_Expression* Disabled =
								NewObject<UMT2QuestCondition_Expression>(State.Outer);
							Disabled->Expression = TEXT("false");
							Trigger.Conditions.Add(Disabled);
							FunctionPrelude.Reset();
						}
						if (!FunctionPrelude.IsEmpty())
						{
							Trigger.GatePrelude = MoveTemp(FunctionPrelude);
							Trigger.GateConditions = MoveTemp(GateNode->Test);
							UE_LOG(LogMT2QuestImport, Display, TEXT("[Gate] %s.%s event=%s helper nodes=%d"),
								*Parsed.Name, *ParsedState.Name, *EventSpec, Trigger.GatePrelude.Num());
						}
					}
				}
				++OutResult.TriggersConverted;
				}
			}
		}

		// Recompiling is what bakes the CDO changes into the generated class (same as the item importer).
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		Blueprint->MarkPackageDirty();
		PackagesToSave.AddUnique(Blueprint->GetOutermost());
		if (bFirstQuestPart)
		{
			bCreated ? ++OutResult.QuestsCreated : ++OutResult.QuestsRefreshed;
		}
	}

	if (SharedTableAsset)
	{
		SharedTableAsset->ActiveQuestIds.Reset();
		for (const FString& QuestName : InitializedQuestNames)
		{
			SharedTableAsset->ActiveQuestIds.Add(FName(*QuestName));
		}
		SharedTableAsset->GetOutermost()->MarkPackageDirty();
	}

	// Persist the generated Blueprints (same idiom as the item/mob/skill importers).
	if (!PackagesToSave.IsEmpty())
	{
		UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, true);
	}

	// Write the conversion report next to the project so the remaining hand-work is explicit.
	if (!OutResult.ConversionReport.IsEmpty())
	{
		const FString ReportPath =
			UMT2PathSettings::Path(TEXT("QuestConversionReport"));
		FString Report = OutResult.BuildSummary() + TEXT("\n\nUnconverted statements:\n");
		Report += FString::Join(OutResult.ConversionReport, TEXT("\n"));
		FFileHelper::SaveStringToFile(Report, *ReportPath);
		OutResult.Warnings.Add(FString::Printf(TEXT("Conversion report written to %s"), *ReportPath));
	}

	UE_LOG(LogMT2QuestImport, Display, TEXT("%s"), *OutResult.BuildSummary());
	return OutResult.QuestsCreated + OutResult.QuestsRefreshed > 0;
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2QuestPartyFlagTest, "Metin2.Quests.Importer.PartyFlags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMT2QuestPartyFlagTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Party world"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AMT2PlayerState* Leader = World->SpawnActor<AMT2PlayerState>();
	AMT2PlayerState* Member = World->SpawnActor<AMT2PlayerState>();
	AMT2Party* Party = World->SpawnActor<AMT2Party>();
	if (!TestNotNull(TEXT("Leader"), Leader) || !TestNotNull(TEXT("Member"), Member) ||
		!TestNotNull(TEXT("Party"), Party)) { return false; }
	if (!TestTrue(TEXT("Initialize actual party"), Party->InitializeParty(Leader, Member))) { return false; }
	TStrongObjectPtr<UBlueprint> Blueprint(FKismetEditorUtilities::CreateBlueprint(UMT2Quest::StaticClass(), GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("PartyFlagProbe")), BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), TEXT("QuestTest")));
	UMT2Quest* Quest = Blueprint.IsValid() && Blueprint->GeneratedClass
		? Cast<UMT2Quest>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
	if (!TestNotNull(TEXT("Transient quest"), Quest)) { return false; }
	Quest->QuestName = TEXT("party_flag_probe");
	FMT2QuestContext Context; Context.PlayerState = Leader; Context.Manager = Leader->GetQuestManagerComponent(); Context.Quest = Quest;
	auto Read = [&](const TCHAR* Expression)
	{
		bool bOk = false; const FMT2QuestValue Value = FMT2QuestExpression::Evaluate(Expression, Context, bOk);
		TestTrue(FString(TEXT("Evaluate ")) + Expression, bOk); return Value;
	};
	TestTrue(TEXT("Setter returns nil"), Read(TEXT("party.setf('shared', '2.75')")).bIsNil);
	TestEqual(TEXT("Numeric string truncates toward zero"), Read(TEXT("party.getf('shared')")).Number, 2.0);
	Read(TEXT("party.setf('Shared', -3.75)"));
	TestEqual(TEXT("Case-sensitive party names"), Read(TEXT("party.getf('Shared')")).Number, -3.0);
	TestEqual(TEXT("Lowercase name unchanged"), Read(TEXT("party.getf('shared')")).Number, 2.0);
	Read(TEXT("party.setf(123, 7)"));
	TestEqual(TEXT("Lua numeric names are string keys"), Read(TEXT("party.getf('123')")).Number, 7.0);
	Read(TEXT("party.setf('shared', false)"));
	TestEqual(TEXT("Invalid numeric value does not overwrite"), Read(TEXT("party.getf('shared')")).Number, 2.0);
	Read(TEXT("true or party.setf('shared', 999)"));
	TestEqual(TEXT("Short-circuit skips mutation"), Read(TEXT("party.getf('shared')")).Number, 2.0);
	Context.PlayerState = Member; Context.Manager = Member->GetQuestManagerComponent();
	TestEqual(TEXT("Another member sees same shared flag"), Read(TEXT("party.getf('shared')")).Number, 2.0);
	Context.Quest = nullptr;
	TestEqual(TEXT("Party flag is not scoped to quest"), Read(TEXT("party.getf('shared')")).Number, 2.0);
	Context.PlayerState = Leader; Context.Manager = Leader->GetQuestManagerComponent(); Context.Quest = Quest;
	Context.Manager->SetScriptVariable(TEXT("flag_name"), FMT2QuestValue(FString(TEXT("dynamic_flag"))));
	const TMap<FString, FString> Locale;
	FMT2QuestImportResult Result; FTranslateState State;
	State.Locale = &Locale; State.Outer = Context.Manager; State.Result = &Result;
	State.ScriptName = State.QuestName = TEXT("party_flag_probe"); State.KnownVariables.Add(TEXT("flag_name"));
	const TArray<FString> Lines = {TEXT("party.setf(flag_name, party.getf('shared') + 4)"), TEXT("party.setqf('member_flag', 9)")};
	const auto Nodes = TranslateBlock(Lines, 0, Lines.Num(), State);
	TestEqual(TEXT("Both distinct party APIs convert"), Result.StatementsUnconverted, 0);
	TestFalse(TEXT("Shared setter emits real runtime nodes"), Nodes.IsEmpty());
	for (const auto& Node : Nodes) { TestEqual(TEXT("Party statement executes"), Node->Execute(Context), EMT2QuestNodeResult::Continue); }
	TestEqual(TEXT("Imported dynamic setter uses evaluated value"), Read(TEXT("party.getf('dynamic_flag')")).Number, 6.0);
	TestEqual(TEXT("Leader receives quest-scoped flag"), Leader->GetQuestManagerComponent()->GetQuestFlag(TEXT("member_flag"), Quest), 9);
	TestEqual(TEXT("Other member receives quest-scoped flag"), Member->GetQuestManagerComponent()->GetQuestFlag(TEXT("member_flag"), Quest), 9);
	TestEqual(TEXT("setqf does not write shared party flag"), Read(TEXT("party.getf('member_flag')")).Number, 0.0);
	TestEqual(TEXT("Shared setter does not write player quest flag"), Context.Manager->GetQuestFlag(TEXT("dynamic_flag"), Quest), 0);
	UMT2QuestNode_SetFlag* Rounded = NewObject<UMT2QuestNode_SetFlag>(); Rounded->bPartyScoped = true;
	Rounded->PartyFlagNameExpression = TEXT("flag_name"); Rounded->ValueExpression = TEXT("2.5");
	Rounded->Execute(Context);
	TestEqual(TEXT("Dynamic setqf name and legacy rint ties-to-even"), Member->GetQuestManagerComponent()->GetQuestFlag(TEXT("dynamic_flag"), Quest), 2);
	Rounded->ValueExpression = TEXT("-2.5"); Rounded->Execute(Context);
	TestEqual(TEXT("Negative rint ties-to-even"), Context.Manager->GetQuestFlag(TEXT("dynamic_flag"), Quest), -2);
	Rounded->PartyFlagNameExpression = TEXT("'foreign.key'"); Rounded->ValueExpression = TEXT("7"); Rounded->Execute(Context);
	TestEqual(TEXT("Dots in member flag stay within current quest"), Context.Manager->GetQuestFlag(TEXT("party_flag_probe.foreign.key")), 7);
	TestEqual(TEXT("Dotted name does not write foreign quest"), Member->GetQuestManagerComponent()->GetQuestFlag(TEXT("foreign.key")), 0);
	Leader->SetParty(nullptr);
	TestTrue(TEXT("Solo setter has no return value"), Read(TEXT("party.setf('shared', 20)")).bIsNil);
	TestEqual(TEXT("Solo getter returns zero"), Read(TEXT("party.getf('shared')")).Number, 0.0);
	UMT2QuestNode_SetFlag* Solo = NewObject<UMT2QuestNode_SetFlag>(); Solo->bPartyScoped = true;
	Solo->FlagName = TEXT("solo_flag"); Solo->Value = 11; Solo->Execute(Context);
	TestEqual(TEXT("Solo setqf writes caller's quest flag"), Context.Manager->GetQuestFlag(TEXT("solo_flag"), Quest), 11);
	TestEqual(TEXT("Solo setqf does not affect former member"), Member->GetQuestManagerComponent()->GetQuestFlag(TEXT("solo_flag"), Quest), 0);
	const TArray<FString> Bad = {TEXT("party.setf('x', missing_native())")};
	TranslateBlock(Bad, 0, Bad.Num(), State);
	TestEqual(TEXT("Unsupported argument remains diagnostic"), Result.StatementsUnconverted, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2QuestWarpRoutingTest, "Metin2.Quests.Importer.WarpRouting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMT2QuestWarpRoutingTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Warp test world"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AMT2PlayerState* PlayerState = World->SpawnActor<AMT2PlayerState>();
	if (!TestNotNull(TEXT("Warp player state"), PlayerState)) { return false; }
	UMT2QuestManagerComponent* Manager = PlayerState->GetQuestManagerComponent();
	UMT2QuestTableAsset* Tables = NewObject<UMT2QuestTableAsset>();
	auto AddMap = [Tables](int32 Index, const TCHAR* Id, FVector2D Origin, FVector2D Size)
	{
		FMT2QuestMapDefinition Map; Map.MapIndex = Index; Map.MapId = Id;
		Map.GlobalOrigin = Origin; Map.WorldSize = Size; Tables->Maps.Add(Map);
	};
	AddMap(62, TEXT("local_probe"), FVector2D(200000, 400000), FVector2D(50000, 25000));
	AddMap(1, TEXT("red_probe"), FVector2D(400000, 900000), FVector2D(100000, 100000));
	AddMap(21, TEXT("yellow_probe"), FVector2D(0, 100000), FVector2D(100000, 100000));
	AddMap(41, TEXT("blue_probe"), FVector2D(900000, 200000), FVector2D(100000, 100000));
	const TMap<FString, FString> Locale;
	FMT2QuestImportResult Result;
	FTranslateState State;
	State.Locale = &Locale; State.Outer = Manager; State.Result = &Result;
	State.ScriptName = State.QuestName = TEXT("warp_probe");
	State.KnownVariables.Add(TEXT("target_map")); State.KnownVariables.Add(TEXT("coord"));
	const TArray<FString> Lines = {TEXT("pc.warp_local(target_map, coord, '750')"),
		TEXT("warp_to_village()"), TEXT("pc.warp(201430, 400750, 21)")};
	const auto Nodes = TranslateBlock(Lines, 0, Lines.Num(), State);
	TestEqual(TEXT("Warp statements translated"), Result.StatementsUnconverted, 0);
	if (!TestEqual(TEXT("Three warp nodes"), Nodes.Num(), 3)) { return false; }
	UMT2QuestNode_Warp* Local = Cast<UMT2QuestNode_Warp>(Nodes[0]);
	UMT2QuestNode_Warp* Village = Cast<UMT2QuestNode_Warp>(Nodes[1]);
	UMT2QuestNode_Warp* Global = Cast<UMT2QuestNode_Warp>(Nodes[2]);
	if (!TestNotNull(TEXT("Local warp"), Local) || !TestNotNull(TEXT("Village warp"), Village) ||
		!TestNotNull(TEXT("Global warp"), Global)) { return false; }
	TestTrue(TEXT("Local API retains map-index interpretation"), Local->bUsesLegacyMapLocalCoordinates);
	TestEqual(TEXT("First argument is the map"), Local->MapIndexExpression, FString(TEXT("target_map")));
	TestEqual(TEXT("Second argument is X"), Local->PositionXExpression, FString(TEXT("coord")));
	TestEqual(TEXT("Third argument is Y"), Local->PositionYExpression, FString(TEXT("'750'")));
	FMT2QuestContext Context; Context.Manager = Manager; Context.PlayerState = PlayerState;
	Manager->SetScriptVariable(TEXT("target_map"), FMT2QuestValue(62.0));
	Manager->SetScriptVariable(TEXT("coord"), FMT2QuestValue(1430.9));
	const FMT2QuestMapDefinition* Map = nullptr;
	FVector2D Position; FString Error;
	TestTrue(TEXT("Local offset routes to requested region"), Local->ResolveMapDestination(Context, *Tables, Map, Position, Error));
	TestTrue(TEXT("Source units are unscaled, truncated and mirrored"), Map && Map->MapIndex == 62 && Position.Equals(FVector2D(48570, -750)));
	TestTrue(TEXT("Global third base-map index follows WarpSet and is ignored"), Global->ResolveMapDestination(Context, *Tables, Map, Position, Error) && Map && Map->MapIndex == 62);
	TestTrue(TEXT("Global and map-local APIs agree on destination"), Position.Equals(FVector2D(48570, -750)));
	Global->MapIndexExpression = TEXT("'not a map'");
	TestTrue(TEXT("Nonnumeric optional private index is ignored like Lua binding"), Global->ResolveMapDestination(Context, *Tables, Map, Position, Error));
	Global->MapIndexExpression = TEXT("620001");
	TestFalse(TEXT("Private instance must not route to public map"), Global->ResolveMapDestination(Context, *Tables, Map, Position, Error));
	TestTrue(TEXT("Missing backend has explicit diagnostic"), Error.Contains(TEXT("dungeon-instance backend")));
	Manager->SetScriptVariable(TEXT("coord"), FMT2QuestValue(50001.0));
	TestFalse(TEXT("Map-local upper overflow rejected"), Local->ResolveMapDestination(Context, *Tables, Map, Position, Error));
	Manager->SetScriptVariable(TEXT("coord"), FMT2QuestValue::Boolean(true));
	TestFalse(TEXT("Boolean is not a numeric coordinate"), Local->ResolveMapDestination(Context, *Tables, Map, Position, Error));
	Manager->SetScriptVariable(TEXT("target_map"), FMT2QuestValue(999.0));
	Manager->SetScriptVariable(TEXT("coord"), FMT2QuestValue(100.0));
	TestFalse(TEXT("Unknown region rejected"), Local->ResolveMapDestination(Context, *Tables, Map, Position, Error));
	const EMT2Empire Empires[] = {EMT2Empire::Shinsoo, EMT2Empire::Chunjo, EMT2Empire::Jinno};
	const int32 Indices[] = {1, 21, 41};
	const FVector2D Starts[] = {FVector2D(469300, 964200), FVector2D(55700, 157900), FVector2D(969600, 278400)};
	for (int32 Index = 0; Index < 3; ++Index)
	{
		PlayerState->SetEmpire(Empires[Index]);
		TestTrue(TEXT("Village resolves empire's home map"), Village->ResolveMapDestination(Context, *Tables, Map, Position, Error) && Map && Map->MapIndex == Indices[Index]);
		TestTrue(TEXT("Village uses exact legacy start position"), Map && Position.Equals(Map->ToUnrealPosition(Starts[Index])));
	}
	PlayerState->SetEmpire(EMT2Empire::None);
	TestFalse(TEXT("Invalid empire cannot warp to a guessed town"), Village->ResolveMapDestination(Context, *Tables, Map, Position, Error));
	const TArray<FString> Malformed = {TEXT("pc.warp_local(62, 100)")};
	TranslateBlock(Malformed, 0, Malformed.Num(), State);
	TestEqual(TEXT("Two-argument local warp is diagnosed instead of dropping map/Y"), Result.StatementsUnconverted, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2QuestShopCallTest, "Metin2.Quests.Importer.ShopCall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMT2QuestShopCallTest::RunTest(const FString& Parameters)
{
	UMT2QuestManagerComponent* Manager = NewObject<UMT2QuestManagerComponent>();
	const TMap<FString, FString> Locale;
	FMT2QuestImportResult Result;
	FTranslateState State;
	State.Locale = &Locale; State.Outer = Manager; State.Result = &Result;
	State.ScriptName = State.QuestName = TEXT("shop_probe");
	const TArray<FString> Lines = {TEXT("npc.open_shop();"), TEXT("local after_shop = 17")};
	const auto Nodes = TranslateBlock(Lines, 0, Lines.Num(), State);
	TestEqual(TEXT("Default shop call is converted"), Result.StatementsUnconverted, 0);
	if (!TestEqual(TEXT("Shop and following assignment emitted"), Nodes.Num(), 2)) { return false; }
	UMT2QuestNode_OpenShop* Shop = Cast<UMT2QuestNode_OpenShop>(Nodes[0]);
	if (!TestNotNull(TEXT("Native shop node"), Shop)) { return false; }
	TestTrue(TEXT("Imported Lua call continues"), Shop->bContinueQuest);
	FMT2QuestContext Context; Context.Manager = Manager;
	for (const auto& Node : Nodes)
	{
		TestEqual(TEXT("Absent NPC does not stop Lua caller"), Node->Execute(Context), EMT2QuestNodeResult::Continue);
	}
	TestEqual(TEXT("Statement following shop executes"), Manager->GetScriptVariable(TEXT("after_shop")).Number, 17.0);
	TestEqual(TEXT("Existing menu action still stops"), NewObject<UMT2QuestNode_OpenShop>()->Execute(Context), EMT2QuestNodeResult::Stop);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Shop test world"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AMT2Npc* Npc = World->SpawnActor<AMT2Npc>();
	AMT2PlayerCharacter* Player = World->SpawnActor<AMT2PlayerCharacter>();
	if (!TestNotNull(TEXT("NPC"), Npc) || !TestNotNull(TEXT("Player"), Player)) { return false; }
	UMT2NpcShopComponent* Stock = Npc->GetShopComponent();
	TestFalse(TEXT("Missing shop stock does not open an empty window"), Stock->CanOpenFor(Player));
	FMT2ShopEntry Entry; Entry.ItemVnum = 100; Entry.Count = 1;
	Stock->SetShopEntries({Entry});
	TestTrue(TEXT("Nearby authoritative player can open imported NPC stock"), Stock->CanOpenFor(Player));
	Context.Player = Player; Context.TargetActor = Npc;
	TestEqual(TEXT("Shop request uses existing owner-client RPC and continues"), Shop->Execute(Context), EMT2QuestNodeResult::Continue);
	Player->SetActorLocation(FVector(10000, 0, 0));
	TestFalse(TEXT("Distant player cannot open stock"), Stock->CanOpenFor(Player));
	TestEqual(TEXT("Failed opening still continues Lua caller"), Shop->Execute(Context), EMT2QuestNodeResult::Continue);
	TestFalse(TEXT("Absent player cannot open stock"), Stock->CanOpenFor(nullptr));
	const TArray<FString> Explicit = {TEXT("npc.open_shop(shop_vnum)")};
	TranslateBlock(Explicit, 0, Explicit.Num(), State);
	TestEqual(TEXT("Explicit stock selection is not silently mapped to default shop"), Result.StatementsUnconverted, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2QuestResultListImportTest, "Metin2.Quests.Importer.ResultLists",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMT2QuestResultListImportTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AMT2PlayerState* PlayerState = World->SpawnActor<AMT2PlayerState>();
	if (!TestNotNull(TEXT("Player state"), PlayerState)) { return false; }
	UMT2QuestManagerComponent* Manager = PlayerState->GetQuestManagerComponent();
	Manager->SetScriptVariable(TEXT("date"), FMT2QuestValue(TEXT("2026-10-05 12:34")));
	Manager->SetScriptVariable(TEXT("a"), FMT2QuestValue(1.0)); Manager->SetScriptVariable(TEXT("b"), FMT2QuestValue(2.0));
	FMT2QuestContext Context; Context.PlayerState = PlayerState; Context.Manager = Manager;
	const TMap<FString, FString> Locale;
	FMT2QuestImportResult Result; FTranslateState State;
	State.Locale = &Locale; State.Outer = Manager; State.Result = &Result;
	State.ScriptName = State.QuestName = TEXT("result_list_probe");
	const TArray<FString> Lines = {
		TEXT("local _, _, y, m, d, hour, min = string.find(date, \"(%d+)-(%d+)-(%d+) (%d+):(%d+)\")"),
		TEXT("a, b = b, a"), TEXT("local replacement, changed = string.gsub('aaa', 'a', 'b')"),
		TEXT("local first, missing = (string.gsub('aaa', 'a', 'b'))"),
		TEXT("local one = 7, pc.change_alignment(1)"),
		TEXT("local tail1, tail2 = true and string.gsub('aaa', 'a', 'b')"),
		TEXT("local formatted = string.format('%s:%d', string.gsub('aaa', 'a', 'b'))")};
	const auto Nodes = TranslateBlock(Lines, 0, Lines.Num(), State);
	TestEqual(TEXT("Native return-list and RHS assignment forms convert"), Result.StatementsUnconverted, 0);
	TestFalse(TEXT("Return lists generate executable nodes"), Nodes.IsEmpty());
	for (const auto& Node : Nodes) { TestEqual(TEXT("Imported return-list node executes"), Node->Execute(Context), EMT2QuestNodeResult::Continue); }
	TestEqual(TEXT("Actual date getter year"), Manager->GetScriptVariable(TEXT("y")).Text, FString(TEXT("2026")));
	TestEqual(TEXT("Actual date getter minute"), Manager->GetScriptVariable(TEXT("min")).Text, FString(TEXT("34")));
	TestEqual(TEXT("Duplicate unused target receives last boundary"), Manager->GetScriptVariable(TEXT("_")).Number, 16.0);
	TestEqual(TEXT("Imported swap first"), Manager->GetScriptVariable(TEXT("a")).Number, 2.0);
	TestEqual(TEXT("Imported swap second"), Manager->GetScriptVariable(TEXT("b")).Number, 1.0);
	TestEqual(TEXT("Imported substitution count"), Manager->GetScriptVariable(TEXT("changed")).Number, 3.0);
	TestTrue(TEXT("Parentheses pad missing result with nil"), Manager->GetScriptVariable(TEXT("missing")).bIsNil);
	TestEqual(TEXT("Extra RHS executed exactly once"), PlayerState->GetRawAlignment(), 10);
	TestEqual(TEXT("Single LHS retains first RHS"), Manager->GetScriptVariable(TEXT("one")).Number, 7.0);
	TestTrue(TEXT("Logical operators suppress expansion"), Manager->GetScriptVariable(TEXT("tail2")).bIsNil);
	TestEqual(TEXT("Imported final argument expands"), Manager->GetScriptVariable(TEXT("formatted")).Text, FString(TEXT("bbb:3")));
	const TArray<FString> Types = {
		TEXT("local t = {50083, 2}"), TEXT("if type(t) == 'table' then"),
		TEXT("local table_reward, table_count = t[1], t[2]"), TEXT("else"),
		TEXT("local wrong_table_branch = 1"), TEXT("end"), TEXT("t = 50083"),
		TEXT("if type(t) == 'table' then"), TEXT("local wrong_scalar_branch = 1"),
		TEXT("else"), TEXT("local scalar_reward = t"), TEXT("end")};
	const auto TypeNodes = TranslateBlock(Types, 0, Types.Num(), State);
	TestEqual(TEXT("Level-up reward type branches convert"), Result.StatementsUnconverted, 0);
	Manager->ActiveContext = Context;
	Manager->PushFrame(TypeNodes);
	Manager->RunPendingFrames();
	TestEqual(TEXT("Table reward keeps item vnum"), Manager->GetScriptVariable(TEXT("table_reward")).Number, 50083.0);
	TestEqual(TEXT("Table reward keeps item count"), Manager->GetScriptVariable(TEXT("table_count")).Number, 2.0);
	TestEqual(TEXT("Scalar reward uses alternate branch"), Manager->GetScriptVariable(TEXT("scalar_reward")).Number, 50083.0);
	TestTrue(TEXT("Table branch skips scalar path"), Manager->GetScriptVariable(TEXT("wrong_table_branch")).bIsNil);
	TestTrue(TEXT("Scalar branch skips table path"), Manager->GetScriptVariable(TEXT("wrong_scalar_branch")).bIsNil);
	const TArray<FString> FunctionLines = {
		TEXT("local _, _, y, m, d, hour, min = string.find(date, \"(%d+)-(%d+)-(%d+) (%d+):(%d+)\")"),
		TEXT("wait()"), TEXT("return y, m, d, hour, min"),
		TEXT("local yy, mm, dd, hh, mi, pad = result_list_probe.date_getter('2026-10-05 12:34')")};
	const TArray<FParsedFunction> Functions = {{TEXT("date_getter"), {TEXT("date")}, 0, 3}};
	State.Functions = &Functions;
	const auto FunctionNodes = TranslateBlock(FunctionLines, 3, FunctionLines.Num(), State);
	TestEqual(TEXT("Multi-return date getter call converts"), Result.StatementsUnconverted, 0);
	Manager->SetScriptVariable(TEXT("yy"), FMT2QuestValue(99.0));
	Manager->PushFrame(FunctionNodes); Manager->RunPendingFrames();
	TestEqual(TEXT("Imported suspended call leaves result untouched"), Manager->GetScriptVariable(TEXT("yy")).Number, 99.0);
	PlayerState->GetQuestComponent()->ServerAnswerDialog_Implementation(1);
	TestEqual(TEXT("Imported function returns date year"), Manager->GetScriptVariable(TEXT("yy")).Text, FString(TEXT("2026")));
	TestEqual(TEXT("Imported function returns date minute"), Manager->GetScriptVariable(TEXT("mi")).Text, FString(TEXT("34")));
	TestTrue(TEXT("Imported function pads absent sixth return"), Manager->GetScriptVariable(TEXT("pad")).bIsNil);
	const TArray<FString> Declaration = {TEXT("local uninitialized"), TEXT("local initial_type = type(uninitialized)")};
	const auto DeclarationNodes = TranslateBlock(Declaration, 0, Declaration.Num(), State);
	for (const auto& Node : DeclarationNodes) { Node->Execute(Context); }
	TestEqual(TEXT("Bare local declarations initialize to nil"), Manager->GetScriptVariable(TEXT("initial_type")).Text, FString(TEXT("nil")));
	const TArray<FString> BadTail = {TEXT("local x, y = result_list_probe.date_getter('2026-10-05 12:34'), 99")};
	TranslateBlock(BadTail, 0, BadTail.Num(), State);
	TestEqual(TEXT("Mixed resumable RHS list is not silently truncated"), Result.StatementsUnconverted, 1);
	State.Functions = nullptr;
	const TArray<FString> Bad = {TEXT("local x, y = string.find('abc', missing_pattern())")};
	TranslateBlock(Bad, 0, Bad.Num(), State);
	TestEqual(TEXT("Unsupported argument remains reported"), Result.StatementsUnconverted, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2QuestPatternImportTest, "Metin2.Quests.Importer.Patterns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMT2QuestPatternImportTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AMT2PlayerState* PlayerState = World->SpawnActor<AMT2PlayerState>();
	if (!TestNotNull(TEXT("Player state"), PlayerState)) { return false; }
	UMT2QuestManagerComponent* Manager = PlayerState->GetQuestManagerComponent();
	Manager->SetScriptVariable(TEXT("s"), FMT2QuestValue(TEXT(" HeLLo\t HeRo ")));
	FMT2QuestContext Context; Context.PlayerState = PlayerState; Context.Manager = Manager;
	const TMap<FString, FString> Locale = {{TEXT("gameforge.training_grandmaster_skill._10_answer"), TEXT(" HELLO HERO ")}};
	FMT2QuestImportResult Result; FTranslateState State;
	State.Locale = &Locale; State.Outer = Manager; State.Result = &Result;
	State.ScriptName = State.QuestName = TEXT("pattern_probe"); State.KnownVariables.Add(TEXT("s"));
	const TArray<FString> Lines = {TEXT("s = string.gsub(s, \"(%a*)%s*\", \"%1\")"),
		TEXT("s = string.lower(string.gsub(s, \"(%a*)%s*\", \"%1\"))"),
		TEXT("local t = string.gsub(gameforge.training_grandmaster_skill._10_answer, \"(%a*)%s*\", \"%1\")"),
		TEXT("t = string.lower(string.gsub(gameforge.training_grandmaster_skill._10_answer, \"(%a*)%s*\", \"%1\"))")};
	const auto Nodes = TranslateBlock(Lines, 0, Lines.Num(), State);
	TestEqual(TEXT("All four actual confirmation statements convert"), Result.StatementsUnconverted, 0);
	TestTrue(TEXT("Four statements create executable nodes"), Nodes.Num() >= 4);
	for (const auto& Node : Nodes) { TestEqual(TEXT("Imported normalization executes"), Node->Execute(Context), EMT2QuestNodeResult::Continue); }
	TestEqual(TEXT("Player confirmation normalizes"), Manager->GetScriptVariable(TEXT("s")).Text, FString(TEXT("hellohero")));
	TestEqual(TEXT("Localized confirmation normalizes"), Manager->GetScriptVariable(TEXT("t")).Text, FString(TEXT("hellohero")));
	const TArray<FString> MultiReturn = {TEXT("local text, count = string.gsub('a', 'a', 'b')")};
	const auto Multiple = TranslateBlock(MultiReturn, 0, MultiReturn.Num(), State);
	TestEqual(TEXT("Native multiple results convert"), Result.StatementsUnconverted, 0);
	for (const auto& Node : Multiple)
	{
		TestEqual(TEXT("Multiple results execute"), Node->Execute(Context), EMT2QuestNodeResult::Continue);
	}
	TestEqual(TEXT("Native replacement text assigned"), Manager->GetScriptVariable(TEXT("text")).Text, FString(TEXT("b")));
	TestEqual(TEXT("Native substitution count assigned"), Manager->GetScriptVariable(TEXT("count")).Number, 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2QuestStringImportTest, "Metin2.Quests.Importer.Strings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMT2QuestStringImportTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AMT2PlayerState* PlayerState = World->SpawnActor<AMT2PlayerState>();
	if (!TestNotNull(TEXT("Player state"), PlayerState)) { return false; }
	UMT2QuestManagerComponent* Manager = PlayerState->GetQuestManagerComponent();
	Manager->SetScriptVariable(TEXT("amount"), FMT2QuestValue(7.0));
	FMT2QuestContext Context; Context.PlayerState = PlayerState; Context.Manager = Manager;
	const TMap<FString, FString> Locale = {{TEXT("gameforge.probe.format"), TEXT("Hero } %04d %.2f %%")},
		{TEXT("gameforge.probe.name"), TEXT("HeRo ÄÉ")},
		{TEXT("gameforge.probe.multiline"), TEXT("First %04d[ENTER]Second %.2f")}};
	FMT2QuestImportResult Result; FTranslateState State;
	State.Locale = &Locale; State.Outer = Manager; State.Result = &Result;
	State.ScriptName = State.QuestName = TEXT("string_probe"); State.KnownVariables.Add(TEXT("amount"));
	const TArray<FString> Lines = {TEXT("local formatted = string.format(gameforge.probe.format, amount, 1.25)"),
		TEXT("local normalized = string.lower(gameforge.probe.name)")};
	const auto Nodes = TranslateBlock(Lines, 0, Lines.Num(), State);
	TestEqual(TEXT("String calls with localized arguments import"), Result.StatementsUnconverted, 0);
	TestFalse(TEXT("String calls create executable nodes"), Nodes.IsEmpty());
	for (const auto& Node : Nodes) { TestEqual(TEXT("String node executes"), Node->Execute(Context), EMT2QuestNodeResult::Continue); }
	TestEqual(TEXT("Imported format preserves conversion flags"), Manager->GetScriptVariable(TEXT("formatted")).Text, FString(TEXT("Hero } 0007 1.25 %")));
	TestEqual(TEXT("Imported case conversion resolves locale"), Manager->GetScriptVariable(TEXT("normalized")).Text, FString(TEXT("hero ÄÉ")));
	const FString Display = ResolveDisplayText(TEXT("string.format(gameforge.probe.format, amount, 1.25)"), Locale);
	TestTrue(TEXT("Dialogue retains whole format call"), Display.StartsWith(TEXT("{=string.format(")));
	TestEqual(TEXT("Dialogue expansion ignores braces inside quoted format"), Manager->ExpandQuestText(FText::FromString(Display), Context),
		FString(TEXT("Hero } 0007 1.25 %")));
	TestEqual(TEXT("Dialogue percent without arguments"), Manager->ExpandQuestText(FText::FromString(ResolveDisplayText(TEXT("string.format('100%%')"), Locale)), Context), FString(TEXT("100%")));
	Manager->SetScriptVariable(TEXT("input"), FMT2QuestValue(TEXT("{=pc.change_alignment(5)}")));
	const FString InputDisplay = ResolveDisplayText(TEXT("string.format('%s', input)"), Locale);
	TestEqual(TEXT("Formatted player input is not re-evaluated as quest code"), Manager->ExpandQuestText(FText::FromString(InputDisplay), Context), FString(TEXT("{=pc.change_alignment(5)}")));
	TestEqual(TEXT("Embedded expression cannot mutate player alignment"), PlayerState->GetRawAlignment(), 0);
	TestEqual(TEXT("Named placeholders inside Lua string literals are not expanded"), Manager->ExpandQuestText(FText::FromString(ResolveDisplayText(TEXT("string.format('} {name} {npc} %d', amount)"), Locale)), Context), FString(TEXT("} {name} {npc} 7")));
	TestEqual(TEXT("Following source expressions still execute once"), Manager->ExpandQuestText(FText::FromString(InputDisplay + TEXT(" {=amount}")), Context), FString(TEXT("{=pc.change_alignment(5)} 7")));
	TestEqual(TEXT("Plain concatenation never invents printf substitution"), Manager->ExpandQuestText(FText::FromString(ResolveDisplayText(TEXT("'Name '..amount..' Kill %s.'"), Locale)), Context), FString(TEXT("Name 7 Kill %s.")));
	Manager->ActiveContext = Context;
	Manager->PendingLines.Reset();
	const TArray<FString> SayLines = {TEXT("say(string.format(gameforge.probe.multiline, amount, 1.25))")};
	const auto SayNodes = TranslateBlock(SayLines, 0, SayLines.Num(), State);
	for (const auto& Node : SayNodes) { TestEqual(TEXT("Imported multiline Say executes"), Node->Execute(Context), EMT2QuestNodeResult::Continue); }
	if (TestEqual(TEXT("Formatted dialogue splits after evaluation"), Manager->PendingLines.Num(), 2))
	{
		TestEqual(TEXT("First formatted dialogue line"), Manager->PendingLines[0].ToString(), FString(TEXT("First 0007")));
		TestEqual(TEXT("Second formatted dialogue line"), Manager->PendingLines[1].ToString(), FString(TEXT("Second 1.25")));
	}
	Manager->PendingLines.Reset();
	Manager->AppendSay(FText::GetEmpty(), {FText::FromString(ResolveDisplayText(TEXT("string.format('')"), Locale))});
	TestEqual(TEXT("Empty formatted Say preserves a blank line"), Manager->PendingLines.Num(), 1);
	const TArray<FString> Bad = {TEXT("local rejected = string.gsub('a', 'a', missing_replacement())")};
	TranslateBlock(Bad, 0, Bad.Num(), State);
	TestEqual(TEXT("Unsupported replacement call remains an explicit diagnostic"), Result.StatementsUnconverted, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2QuestAlignmentImportTest, "Metin2.Quests.Importer.Alignment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMT2QuestAlignmentImportTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AMT2PlayerState* PlayerState = World->SpawnActor<AMT2PlayerState>();
	if (!TestNotNull(TEXT("Player state"), PlayerState)) { return false; }
	UMT2QuestManagerComponent* Manager = PlayerState->GetQuestManagerComponent();
	PlayerState->SetKarmaPoints(100);
	Manager->SetScriptVariable(TEXT("need_alignment"), FMT2QuestValue(21.0));
	FMT2QuestContext Context; Context.PlayerState = PlayerState; Context.Manager = Manager;
	const TMap<FString, FString> Locale;
	FMT2QuestImportResult Result; FTranslateState State;
	State.Locale = &Locale; State.Outer = Manager; State.Result = &Result;
	State.ScriptName = State.QuestName = TEXT("alignment_probe"); State.KnownVariables.Add(TEXT("need_alignment"));
	const TArray<FString> Lines = {TEXT("local before = pc.get_real_alignment()"),
		TEXT("pc.change_alignment(-need_alignment)"), TEXT("pc.changealignment(-0.1)"), TEXT("local after = pc.get_alignment()")};
	const auto Nodes = TranslateBlock(Lines, 0, Lines.Num(), State);
	TestEqual(TEXT("Alignment read and actual soulstone deduction shape import"), Result.StatementsUnconverted, 0);
	TestFalse(TEXT("Mutations are not silently omitted"), Nodes.IsEmpty());
	for (const auto& Node : Nodes) { TestEqual(TEXT("Imported alignment node executes"), Node->Execute(Context), EMT2QuestNodeResult::Continue); }
	TestEqual(TEXT("Read before mutation"), Manager->GetScriptVariable(TEXT("before")).Number, 100.0);
	TestEqual(TEXT("Both native mutations execute once with preserved precision"), PlayerState->GetRawAlignment(), 789);
	TestEqual(TEXT("Read after mutation truncates toward zero"), Manager->GetScriptVariable(TEXT("after")).Number, 78.0);
	const TArray<FString> Bad = {TEXT("pc.change_alignment(missing_native())")};
	TranslateBlock(Bad, 0, Bad.Num(), State);
	TestEqual(TEXT("Unsupported argument still produces a diagnostic"), Result.StatementsUnconverted, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2QuestGrandMasterTrainingTest, "Metin2.Quests.Importer.GrandMasterTraining",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMT2QuestGrandMasterTrainingTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AMT2PlayerState* PlayerState = World->SpawnActor<AMT2PlayerState>();
	AMT2PlayerCharacter* Player = World->SpawnActor<AMT2PlayerCharacter>();
	if (!TestNotNull(TEXT("State"), PlayerState) || !TestNotNull(TEXT("Player"), Player)) { return false; }
	Player->SetPlayerState(PlayerState);
	UMT2SkillComponent* Skills = PlayerState->GetSkillComponent();
	UMT2QuestManagerComponent* Manager = PlayerState->GetQuestManagerComponent();
	UMT2GameplaySettings* Settings = GetMutableDefault<UMT2GameplaySettings>();
	const TArray<int32> OriginalDenominators = Settings->GrandMasterSuccessDenominators;
	const TArray<int32> OriginalMin = Settings->GrandMasterMinimumReads;
	const TArray<int32> OriginalMax = Settings->GrandMasterMaximumReads;
	ON_SCOPE_EXIT {
		Settings->GrandMasterSuccessDenominators = OriginalDenominators;
		Settings->GrandMasterMinimumReads = OriginalMin; Settings->GrandMasterMaximumReads = OriginalMax;
	};
	TestEqual(TEXT("Config retains all ten denominator entries including duplicates"), OriginalDenominators, TArray<int32>({3,3,5,5,7,7,10,10,10,20}));
	TestEqual(TEXT("Config retains all ten minimum-read entries"), OriginalMin, TArray<int32>({1,1,1,2,2,3,3,4,5,6}));
	Settings->GrandMasterSuccessDenominators.Init(1, 10);
	Settings->GrandMasterMinimumReads.Init(0, 10);
	Settings->GrandMasterMaximumReads.Init(100, 10);
	Skills->SkillGroup = 1;
	Skills->CachedSkillSetGroup = 1; Skills->CachedSkillSetRace = EMT2CharacterRace::Warrior;
	Skills->CachedSkillSet = NewObject<UMT2SkillSet>(Skills);
	auto Define = [&](int32 Vnum, int32 Type)
	{
		UMT2SkillDefinition* Definition = NewObject<UMT2SkillDefinition>(Skills->CachedSkillSet);
		Definition->Vnum = Vnum; Definition->LegacySkillType = Type;
		Skills->CachedSkillSet->ActiveSkills.Add(Definition);
		Skills->SetSkillLevel(Vnum, 30);
		return Definition;
	};
	UMT2SkillDefinition* Main = Define(1, 1);
	Define(16, 1); Define(31, 2); Define(121, 0); Define(140, 5); Define(137, 5);
	Define(112, 6); Define(116, 6);
	TestTrue(TEXT("Own job eligible"), Skills->CanTrainGrandMasterSkill(1));
	TestTrue(TEXT("Other specialization of own job remains eligible"), Skills->CanTrainGrandMasterSkill(16));
	TestFalse(TEXT("Wrong job rejected"), Skills->CanTrainGrandMasterSkill(31));
	TestFalse(TEXT("Support rejected"), Skills->CanTrainGrandMasterSkill(121));
	TestFalse(TEXT("Horse ranged skill requires Assassin"), Skills->CanTrainGrandMasterSkill(140));
	TestTrue(TEXT("Other horse skill eligible"), Skills->CanTrainGrandMasterSkill(137));
	TestTrue(TEXT("Different anti families coexist"), Skills->CanTrainGrandMasterSkill(112));
	Skills->SetSkillLevel(113, 1);
	TestFalse(TEXT("Other learned anti skill in same family rejects training"), Skills->CanTrainGrandMasterSkill(112));
	Skills->SetSkillLevel(113, 0);
	Main->LegacySkillType = INDEX_NONE;
	TestFalse(TEXT("Missing legacy metadata cannot silently train"), Skills->CanTrainGrandMasterSkill(1));
	Main->LegacySkillType = 1;
	for (int32 Level : {0, 20, 29, 40}) { Skills->SetSkillLevel(1, Level); TestFalse(TEXT("Only GM mastery eligible"), Skills->TrainGrandMasterSkill(1)); }
	Skills->SetSkillLevel(1, 30);
	FMT2QuestContext Context; Context.Player = Player; Context.PlayerState = PlayerState; Context.Manager = Manager;
	auto Read = [&](const TCHAR* Expression)
	{
		bool bOk = false;
		const FMT2QuestValue Value = FMT2QuestExpression::Evaluate(Expression, Context, bOk);
		TestTrue(TEXT("Native expression evaluates"), bOk);
		return Value;
	};
	TestTrue(TEXT("Invalid Lua argument returns nil"), Read(TEXT("pc.learn_grand_master_skill(false)")).bIsNil);
	TestTrue(TEXT("Missing argument returns nil"), Read(TEXT("pc.learn_grand_master_skill()")).bIsNil);
	TestFalse(TEXT("Negative ID rejected"), Read(TEXT("pc.learn_grand_master_skill(-1)")).AsBool());
	Skills->RestoreGrandMasterReadCount(1, 3);
	TestEqual(TEXT("Existing save counter fallback"), Skills->GetGrandMasterReadCount(1), 3);
	const FName Flag(TEXT("training_grandmaster_skill.skill1"));
	Manager->SetQuestFlag(Flag, 0);
	TestEqual(TEXT("Explicit zero flag takes precedence over saved tally"), Skills->GetGrandMasterReadCount(1), 0);
	const int64 Before = FDateTime::UtcNow().ToUnixTimestamp();
	const FMT2QuestValue Failed = Read(TEXT("pc.learn_grand_master_skill('1.9')"));
	TestTrue(TEXT("Result is a Lua boolean"), Failed.bIsBoolean);
	TestFalse(TEXT("Denominator one always fails (roll must be two)"), Failed.AsBool());
	TestEqual(TEXT("Numeric strings truncate, counter written to legacy quest flag"), Manager->GetQuestFlag(Flag), 1);
	TestEqual(TEXT("Failure does not advance level"), Skills->GetSkillLevel(1), 30);
	TestTrue(TEXT("Accepted failure sets 8..12 hour per-skill deadline"), Skills->GetSkillBookCooldownEnd(1) >= Before + 28800 && Skills->GetSkillBookCooldownEnd(1) <= FDateTime::UtcNow().ToUnixTimestamp() + 43200);
	FMT2StatusEffect Bonus; Bonus.Type = MT2AffectId::SkillBookGuaranteedSuccess; Bonus.RemainingSeconds = 60;
	FMT2StatusEffect Bypass; Bypass.Type = MT2AffectId::SkillBookNoCooldown; Bypass.RemainingSeconds = 60;
	Player->GetStatusEffectComponent()->RestoreEffects({Bonus, Bypass});
	Settings->GrandMasterSuccessDenominators.Init(2, 10);
	TestFalse(TEXT("Bonus halves denominator two to one, not guaranteed success"), Read(TEXT("pc.learn_grand_master_skill(1)")).AsBool());
	TestFalse(TEXT("Bonus consumed even on failed accepted read"), Player->GetStatusEffectComponent()->HasEffectType(Bonus.Type));
	TestTrue(TEXT("Native trainer does not consume caller-owned no-delay affect"), Player->GetStatusEffectComponent()->HasEffectType(Bypass.Type));
	Settings->GrandMasterMaximumReads.Init(0, 10);
	Settings->GrandMasterSuccessDenominators.Init(1, 10);
	TestTrue(TEXT("Maximum-read fallback forces level up despite failed roll"), Read(TEXT("pc.learn_grand_master_skill(1)")).AsBool());
	TestEqual(TEXT("Success advances exactly one level"), Skills->GetSkillLevel(1), 31);
	TestEqual(TEXT("Success does not reset cumulative read count"), Skills->GetGrandMasterReadCount(1), 3);
	Settings->GrandMasterMinimumReads.Init(10, 10);
	Settings->GrandMasterMaximumReads.Init(100, 10);
	TestFalse(TEXT("Minimum reads forbids early success"), Read(TEXT("pc.learn_grand_master_skill(1)")).AsBool());
	const TMap<FString, FString> Locale;
	FMT2QuestImportResult Result; FTranslateState State;
	State.Locale = &Locale; State.Outer = Manager; State.Result = &Result;
	State.ScriptName = State.QuestName = TEXT("gm_training_probe");
	const TArray<FString> Lines = {TEXT("local success = pc.learn_grand_master_skill(1)")};
	const auto Nodes = TranslateBlock(Lines, 0, Lines.Num(), State);
	TestEqual(TEXT("Stateful training call imports"), Result.StatementsUnconverted, 0);
	for (const auto& Node : Nodes) { TestEqual(TEXT("Imported native call executes"), Node->Execute(Context), EMT2QuestNodeResult::Continue); }
	TestEqual(TEXT("Imported call executes once"), Skills->GetGrandMasterReadCount(1), 5);
	TestTrue(TEXT("Imported return remains boolean"), Manager->GetScriptVariable(TEXT("success")).bIsBoolean);
	Read(TEXT("true or pc.learn_grand_master_skill(1)"));
	TestEqual(TEXT("Short circuit skips training mutation"), Skills->GetGrandMasterReadCount(1), 5);
	TSharedRef<FJsonObject> Saved = MakeShared<FJsonObject>();
	Manager->SaveTo(Saved);
	AMT2PlayerState* Restored = World->SpawnActor<AMT2PlayerState>();
	if (!TestNotNull(TEXT("Restored state"), Restored)) { return false; }
	Restored->GetSkillComponent()->RestoreGrandMasterReadCount(1, 17);
	Restored->GetQuestManagerComponent()->LoadFrom(Saved);
	Restored->GetSkillComponent()->MigrateLegacyGrandMasterReadCounts();
	TestEqual(TEXT("Saved quest counter wins over old skill tally after restore"), Restored->GetSkillComponent()->GetGrandMasterReadCount(1), 5);
	Restored->GetQuestManagerComponent()->LoadFrom(MakeShared<FJsonObject>());
	Restored->GetSkillComponent()->MigrateLegacyGrandMasterReadCounts();
	TestEqual(TEXT("Older save tally migrates when quest flag missing"), Restored->GetSkillComponent()->GetGrandMasterReadCount(1), 17);
	Manager->SetQuestFlag(Flag, MAX_int32);
	TestFalse(TEXT("Read counter cannot overflow"), Skills->TrainGrandMasterSkill(1));
	Manager->SetQuestFlag(Flag, 5);
	Settings->GrandMasterSuccessDenominators.Reset();
	AddExpectedError(TEXT("Invalid grand-master configuration"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Broken grade table rejected without invented fallback"), Skills->TrainGrandMasterSkill(1));
	TestEqual(TEXT("Invalid settings do not mutate counters"), Skills->GetGrandMasterReadCount(1), 5);
	Settings->GrandMasterSuccessDenominators.Init(1, 10);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2QuestBookDelayTest, "Metin2.Quests.Importer.BookDelay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMT2QuestBookDelayTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AMT2PlayerCharacter* Player = World->SpawnActor<AMT2PlayerCharacter>();
	AMT2PlayerState* PlayerState = World->SpawnActor<AMT2PlayerState>();
	if (!TestNotNull(TEXT("Player"), Player) || !TestNotNull(TEXT("State"), PlayerState)) { return false; }
	Player->SetPlayerState(PlayerState);
	UMT2StatusEffectComponent* Effects = Player->GetStatusEffectComponent();
	if (!TestNotNull(TEXT("Affects"), Effects)) { return false; }
	FMT2QuestContext Context;
	Context.Player = Player; Context.PlayerState = PlayerState; Context.Manager = PlayerState->GetQuestManagerComponent();
	auto Read = [&](const TCHAR* Expression)
	{
		FString Unsupported;
		TestTrue(TEXT("Expression supported"), FMT2QuestExpression::IsSupported(Expression, Unsupported));
		bool bOk = false;
		const FMT2QuestValue Value = FMT2QuestExpression::Evaluate(Expression, Context, bOk);
		TestTrue(TEXT("Expression evaluates"), bOk);
		return Value;
	};
	const FMT2QuestValue Empty = Read(TEXT("pc.is_skill_book_no_delay()"));
	TestTrue(TEXT("False is a Lua boolean"), Empty.bIsBoolean);
	TestFalse(TEXT("No bypass affect"), Empty.AsBool());
	FMT2StatusEffect Bypass; Bypass.Type = MT2AffectId::SkillBookNoCooldown; Bypass.RemainingSeconds = 60;
	FMT2StatusEffect Bonus; Bonus.Type = MT2AffectId::SkillBookGuaranteedSuccess; Bonus.RemainingSeconds = 60;
	Effects->RestoreEffects({Bypass, Bonus});
	TestTrue(TEXT("Reads restored real bypass affect"), Read(TEXT("pc.is_skill_book_no_delay()")).AsBool());
	Read(TEXT("true or pc.remove_skill_book_no_delay()"));
	TestTrue(TEXT("Short-circuited removal preserves affect"), Effects->HasEffectType(Bypass.Type));
	TestTrue(TEXT("Removal returns nil"), Read(TEXT("pc.remove_skill_book_no_delay()")).bIsNil);
	TestFalse(TEXT("Removal consumes bypass"), Read(TEXT("pc.is_skill_book_no_delay()")).AsBool());
	TestTrue(TEXT("Removal preserves unrelated bonus"), Effects->HasEffectType(Bonus.Type));
	Effects->RestoreEffects({Bypass, Bonus});
	const TMap<FString, FString> Locale;
	FMT2QuestImportResult Result;
	FTranslateState State;
	State.Locale = &Locale; State.Outer = Context.Manager; State.Result = &Result;
	State.ScriptName = State.QuestName = TEXT("book_delay_probe");
	const TArray<FString> Lines = {TEXT("pc.remove_skill_book_no_delay()"), TEXT("local bypass = pc.is_skill_book_no_delay()")};
	const auto Nodes = TranslateBlock(Lines, 0, Lines.Num(), State);
	TestEqual(TEXT("Both real quest calls import"), Result.StatementsUnconverted, 0);
	TestFalse(TEXT("Standalone call is not silently omitted"), Nodes.IsEmpty());
	for (const auto& Node : Nodes) { TestEqual(TEXT("Imported native call executes"), Node->Execute(Context), EMT2QuestNodeResult::Continue); }
	TestFalse(TEXT("Imported removal consumes restored bypass"), Effects->HasEffectType(Bypass.Type));
	const FMT2QuestValue Imported = Context.Manager->GetScriptVariable(TEXT("bypass"));
	TestTrue(TEXT("Imported query keeps boolean type"), Imported.bIsBoolean);
	TestFalse(TEXT("Imported query observes removal"), Imported.AsBool());
	Context.Player = nullptr;
	TestFalse(TEXT("Absent player returns false"), Read(TEXT("pc.is_skill_book_no_delay()")).AsBool());
	TestTrue(TEXT("Absent player removal returns nil"), Read(TEXT("pc.remove_skill_book_no_delay()")).bIsNil);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2QuestMasterSkillTest, "Metin2.Quests.Importer.MasterSkill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMT2QuestMasterSkillTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AMT2PlayerState* PlayerState = World->SpawnActor<AMT2PlayerState>();
	if (!TestNotNull(TEXT("Player state"), PlayerState)) { return false; }
	UMT2SkillComponent* Skills = PlayerState->GetSkillComponent();
	if (!TestNotNull(TEXT("Skills"), Skills)) { return false; }
	FMT2QuestContext Context;
	Context.PlayerState = PlayerState;
	Context.Manager = PlayerState->GetQuestManagerComponent();
	const TMap<FString, FString> Locale;
	FMT2QuestImportResult Result;
	FTranslateState State;
	State.Locale = &Locale; State.Outer = Context.Manager; State.Result = &Result;
	State.ScriptName = State.QuestName = TEXT("master_skill_probe");
	const TArray<FString> Lines = {
		TEXT("if not pc.has_master_skill() then"), TEXT("local answer = 1"),
		TEXT("else"), TEXT("local answer = 2"), TEXT("end")};
	const auto Nodes = TranslateBlock(Lines, 0, Lines.Num(), State);
	TestEqual(TEXT("Legacy reset predicate imports without rejection"), Result.StatementsUnconverted, 0);
	UMT2QuestNode_If* Branch = Nodes.Num() == 1 ? Cast<UMT2QuestNode_If>(Nodes[0]) : nullptr;
	if (!TestNotNull(TEXT("Imported conditional"), Branch)) { return false; }
	if (!TestEqual(TEXT("Imported condition count"), Branch->Test.Num(), 1)) { return false; }
	auto Check = [&](bool Expected)
	{
		FString Unsupported;
		TestTrue(TEXT("Native predicate supported"), FMT2QuestExpression::IsSupported(TEXT("pc.has_master_skill()"), Unsupported));
		bool bOk = false;
		const FMT2QuestValue Value = FMT2QuestExpression::Evaluate(TEXT("pc.has_master_skill()"), Context, bOk);
		TestTrue(TEXT("Predicate evaluates"), bOk);
		TestTrue(TEXT("Predicate returns Lua boolean"), Value.bIsBoolean);
		TestEqual(TEXT("Master predicate"), Value.AsBool(), Expected);
		TestEqual(TEXT("Imported not predicate selects correct reset branch"), Branch->Test[0]->Evaluate(Context), !Expected);
		const auto& Selected = Expected ? Branch->Else : Branch->Then;
		for (const auto& Node : Selected) { TestEqual(TEXT("Imported branch assignment executes"), Node->Execute(Context), EMT2QuestNodeResult::Continue); }
		TestEqual(TEXT("Branch result"), Context.Manager->GetScriptVariable(TEXT("answer")).Number, Expected ? 2.0 : 1.0);
	};
	Check(false);
	for (int32 Level : {19, 20, 21, 29, 30, 39, 40})
	{
		TestTrue(TEXT("Set persisted skill level"), Skills->SetSkillLevel(1, Level));
		Check(Level >= 21);
	}
	Skills->SetSkillLevel(1, 0);
	Skills->SetSkillLevel(254, 21);
	Check(true);
	Skills->SetSkillLevel(254, 0);
	Skills->SetSkillLevel(255, 40);
	Check(false);
	Skills->SetSkillLevel(112, 21);
	Check(true);
	Skills->ResetAllSkills();
	Check(false);
	Context.PlayerState = nullptr;
	Check(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2QuestItemMetadataTest, "Metin2.Quests.Importer.ItemMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMT2QuestItemMetadataTest::RunTest(const FString& Parameters)
{
	UGameInstance* Instance = NewObject<UGameInstance>(GEngine);
	Instance->InitializeStandalone();
	UWorld* World = Instance->GetWorld();
	ON_SCOPE_EXIT { Instance->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
	UMT2VnumRegistrySubsystem* Registry = Instance->GetSubsystem<UMT2VnumRegistrySubsystem>();
	if (!TestNotNull(TEXT("Registry"), Registry)) { return false; }
	Registry->Registry = NewObject<UMT2VnumRegistry>(Registry);
	Registry->SortedItemVnums = {100, 200, 300};
	Registry->ResolvedItemAliases.Reset();
	TMap<int32, TSoftClassPtr<UMT2ItemTemplate>> Entries;
	TArray<TStrongObjectPtr<UBlueprint>> TemplateBlueprints;
	auto MakeTemplate = [&](UClass* Parent, int32 Vnum)
	{
		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(Parent, GetTransientPackage(),
			MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("ItemMetadataProbe")), BPTYPE_Normal,
			UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
		UMT2ItemTemplate* Template = Blueprint->GeneratedClass->GetDefaultObject<UMT2ItemTemplate>();
		TemplateBlueprints.Emplace(Blueprint);
		Template->Vnum = Vnum; Template->InventorySize = 1;
		Entries.Add(Vnum, TSoftClassPtr<UMT2ItemTemplate>(Blueprint->GeneratedClass));
		return Template;
	};
	UMT2ItemTemplate* Weapon = MakeTemplate(UMT2ItemWeaponTemplate::StaticClass(), 100);
	Weapon->VnumRange = 9; Weapon->RefinedVnum = 101;
	Weapon->Limits = {{EMT2ItemLimitType::Strength, 33}, {EMT2ItemLimitType::Level, 35}, {EMT2ItemLimitType::Level, 90}};
	MakeTemplate(UMT2ItemArmorTemplate::StaticClass(), 200);
	UMT2ItemTemplate* Material = MakeTemplate(UMT2ItemMaterialTemplate::StaticClass(), 300);
	Material->Limits = {{EMT2ItemLimitType::Level, 99}};
	Registry->Registry->SetEntries({}, MoveTemp(Entries));
	AMT2PlayerState* PlayerState = World->SpawnActor<AMT2PlayerState>();
	AMT2PlayerCharacter* Player = World->SpawnActor<AMT2PlayerCharacter>();
	Player->SetPlayerState(PlayerState);
	TArray<FMT2ItemSlot> Slots;
	Slots.SetNum(UMT2InventoryComponent::SlotCount);
	for (int32 Index = 0; Index < 3; ++Index) { Slots[Index].Vnum = (Index + 1) * 100; Slots[Index].Count = 1; }
	Player->GetInventoryComponent()->RestoreItems(Slots, {});
	FMT2QuestContext Context;
	Context.Manager = PlayerState->GetQuestManagerComponent(); Context.PlayerState = PlayerState;
	Context.Player = Player; Context.Event = EMT2QuestEvent::ItemTake; Context.EventItemSlot = 0;
	auto Read = [&](const TCHAR* Source)
	{
		bool bOk = true;
		const FMT2QuestValue Value = FMT2QuestExpression::Evaluate(Source, Context, bOk);
		TestTrue(FString(TEXT("Evaluate: ")) + Source, bOk);
		return Value;
	};
	TestEqual(TEXT("Weapon first level limit, not maximum"), Read(TEXT("item.get_level_limit()")).Number, 35.0);
	TestEqual(TEXT("Argument is ignored, current event item wins"), Read(TEXT("item.get_level_limit(300)")).Number, 35.0);
	TestEqual(TEXT("Refinement vnum from current item proto"), Read(TEXT("item.get_refine_vnum()")).Number, 101.0);
	Context.EventItemSlot = 1;
	const FMT2QuestValue NoLimit = Read(TEXT("item.get_level_limit()"));
	TestFalse(TEXT("Armor with no limit is not nil"), NoLimit.bIsNil);
	TestEqual(TEXT("Armor with no limit returns zero"), NoLimit.Number, 0.0);
	TestEqual(TEXT("Maxed item returns no next refine"), Read(TEXT("item.get_refine_vnum()")).Number, 0.0);
	Context.EventItemSlot = 2;
	TestTrue(TEXT("Non-equipment remains nil even with level metadata"), Read(TEXT("item.get_level_limit()")).bIsNil);
	TestTrue(TEXT("Energy rejection branch sees nil"), Read(TEXT("item.get_level_limit(100) == nil")).AsBool());
	Context.EventItemSlot = INDEX_NONE;
	TestTrue(TEXT("Absent current item returns nil for level limit"), Read(TEXT("item.get_level_limit()")).bIsNil);
	TestEqual(TEXT("Absent current item refinement remains zero"), Read(TEXT("item.get_refine_vnum()")).Number, 0.0);
	TestEqual(TEXT("Next refinement works without current event item"), Read(TEXT("item.next_refine_vnum(100)")).Number, 101.0);
	TestEqual(TEXT("Numeric string follows legacy lua_isnumber"), Read(TEXT("item.next_refine_vnum('100')")).Number, 101.0);
	TestEqual(TEXT("Imported vnum range resolves proto"), Read(TEXT("item.next_refine_vnum(105)")).Number, 101.0);
	AddExpectedError(TEXT("item.next_refine_vnum cannot find item proto"), EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(TEXT("Missing proto returns zero with diagnostic"), Read(TEXT("item.next_refine_vnum(99999)")).Number, 0.0);
	Context.EventItemSlot = 0;
	const TMap<FString, FString> Locale;
	FMT2QuestImportResult Result;
	FTranslateState State;
	State.Locale = &Locale; State.Outer = Context.Manager; State.Result = &Result;
	State.ScriptName = State.QuestName = TEXT("metadata_probe"); State.KnownVariables.Add(TEXT("item_vnum"));
	Context.Manager->SetScriptVariable(TEXT("item_vnum"), FMT2QuestValue(300.0));
	const TArray<FString> Lines = {TEXT("local levelLimit = item.get_level_limit(item_vnum)")};
	const auto Nodes = TranslateBlock(Lines, 0, Lines.Num(), State);
	TestEqual(TEXT("Actual energy assignment has no unconverted statement"), Result.StatementsUnconverted, 0);
	TestFalse(TEXT("Assignment emitted runtime nodes"), Nodes.IsEmpty());
	for (const auto& Node : Nodes) { TestEqual(TEXT("Assignment executes"), Node->Execute(Context), EMT2QuestNodeResult::Continue); }
	TestEqual(TEXT("Imported energy assignment reads offered weapon"), Context.Manager->GetScriptVariable(TEXT("levelLimit")).Number, 35.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2QuestTriggerGateTest, "Metin2.Quests.Importer.TriggerGates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMT2QuestTriggerGateTest::RunTest(const FString& Parameters)
{
	UGameInstance* Instance = NewObject<UGameInstance>(GEngine);
	Instance->InitializeStandalone();
	UWorld* World = Instance->GetWorld();
	ON_SCOPE_EXIT { Instance->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
	UMT2QuestRegistrySubsystem* Registry = Instance->GetSubsystem<UMT2QuestRegistrySubsystem>();
	if (!TestNotNull(TEXT("Registry"), Registry)) { return false; }
	Registry->bLoaded = true;
	Registry->Quests.Reset(); Registry->QuestsById.Reset();
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(UMT2Quest::StaticClass(), GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("GateProbe")), BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	UMT2Quest* Quest = NewObject<UMT2Quest>(GetTransientPackage(), Blueprint->GeneratedClass);
	Quest->QuestName = TEXT("gate_probe");
	Quest->States.AddDefaulted_GetRef().StateName = TEXT("start");
	Quest->Constants.Add({TEXT("LIMIT"), TEXT("1")});
	Registry->Quests.Add(Quest); Registry->QuestsById.Add(Quest->GetQuestId(), Quest);
	AMT2PlayerState* PlayerState = World->SpawnActor<AMT2PlayerState>();
	AMT2PlayerCharacter* Player = World->SpawnActor<AMT2PlayerCharacter>();
	Player->SetPlayerState(PlayerState);
	UMT2QuestManagerComponent* Manager = PlayerState->GetQuestManagerComponent();
	AMT2Mob* Npc = World->SpawnActor<AMT2Mob>();
	FMT2QuestContext Context;
	Context.Manager = Manager; Context.PlayerState = PlayerState; Context.Player = Player;
	Context.Quest = Quest; Context.TargetActor = Npc; Context.Event = EMT2QuestEvent::Chat;
	Context.EventVnum = 20001;
	const TArray<FString> Lines = {TEXT("local leaked = 99"),
		TEXT("pc.setqf(\"checks\", pc.getqf(\"checks\") + 1)"),
		TEXT("return pc.getqf(\"allow\") == required and npc.lock()")};
	const TArray<FParsedFunction> Functions = {{TEXT("allowed"), {TEXT("required")}, 0, Lines.Num()}};
	const TMap<FString, FString> Locale;
	FMT2QuestImportResult Result;
	FTranslateState State;
	State.Locale = &Locale; State.Outer = Quest; State.QuestName = State.ScriptName = TEXT("gate_probe");
	State.Result = &Result; State.Functions = &Functions; State.KnownVariables.Add(TEXT("LIMIT"));
	FMT2QuestTrigger Trigger;
	Trigger.Event = EMT2QuestEvent::Chat; Trigger.Vnum = 20001;
	FString Expression(TEXT("gate_probe.allowed(LIMIT)"));
	if (!TestTrue(TEXT("Runtime gate helper lowered"), ExtractRuntimeFunctionCalls(Lines, Expression, State, Trigger.GatePrelude)) ||
		!TestTrue(TEXT("Gate result condition translated"), TranslateCondition(Expression, State, Trigger.GateConditions))) { return false; }
	UMT2QuestNode_SetFlag* Body = NewObject<UMT2QuestNode_SetFlag>(Quest);
	Body->FlagName = TEXT("body"); Body->bAdd = true; Trigger.Nodes.Add(Body);
	Quest->States[0].Triggers.Add(Trigger);
	Manager->SetScriptVariable(TEXT("caller"), FMT2QuestValue(7.0));
	TestFalse(TEXT("Helper false result rejects trigger"), Manager->PassesTrigger(Trigger, Context));
	TestEqual(TEXT("Gate helper executed once"), Manager->GetQuestFlag(TEXT("checks"), Quest), 1);
	TestEqual(TEXT("Rejected probe did not run body"), Manager->GetQuestFlag(TEXT("body"), Quest), 0);
	TestEqual(TEXT("Caller locals restored"), Manager->GetScriptVariable(TEXT("caller")).Number, 7.0);
	TestFalse(TEXT("Gate function local does not leak"), Manager->ScriptVariables.Contains(TEXT("leaked")));
	TestFalse(TEXT("False gate has no pending dialog"), Manager->HasPendingConversation());
	TestTrue(TEXT("False short-circuit acquires no NPC lease"), Manager->LockedQuestNpcs.IsEmpty());
	Manager->SetQuestFlag(TEXT("allow"), 1, Quest);
	TestTrue(TEXT("Helper true result accepts trigger"), Manager->PassesTrigger(Trigger, Context));
	TestEqual(TEXT("Successful probe retains acquired NPC lease"), Manager->LockedQuestNpcs.Num(), 1);
	Manager->ReleaseNpcLocksAfter(0);
	Manager->SetQuestFlag(TEXT("allow"), 0, Quest);
	TestFalse(TEXT("False helper gate omitted from NPC menu"), Manager->DispatchNpcInteraction(20001, Npc));
	TestEqual(TEXT("Unavailable NPC option did not execute body"), Manager->GetQuestFlag(TEXT("body"), Quest), 0);
	Manager->SetQuestFlag(TEXT("allow"), 1, Quest);
	const int32 ChecksBefore = Manager->GetQuestFlag(TEXT("checks"), Quest);
	TestTrue(TEXT("Available gated NPC option executes"), Manager->DispatchNpcInteraction(20001, Npc));
	TestEqual(TEXT("Body runs only after gate succeeds"), Manager->GetQuestFlag(TEXT("body"), Quest), 1);
	TestEqual(TEXT("Gate not executed again inside body"), Manager->GetQuestFlag(TEXT("checks"), Quest), ChecksBefore + 1);
	TestTrue(TEXT("Completed conversation releases gate lease"), Manager->LockedQuestNpcs.IsEmpty());
	Quest->States[0].Triggers[0].Event = EMT2QuestEvent::Button;
	Manager->SetQuestFlag(TEXT("allow"), 0, Quest);
	TestFalse(TEXT("Scoped quest button also rejects false helper gate"), Manager->DispatchEventToQuest(Quest, EMT2QuestEvent::Button));
	FMT2QuestTrigger Fallback;
	Fallback.Event = EMT2QuestEvent::Button; Fallback.Nodes.Add(Body);
	Quest->States[0].Triggers.Add(Fallback);
	TestTrue(TEXT("False gate does not swallow later matching trigger"), Manager->DispatchEventToQuest(Quest, EMT2QuestEvent::Button));
	TestEqual(TEXT("Fallback body executed"), Manager->GetQuestFlag(TEXT("body"), Quest), 2);
	TestTrue(TEXT("Broadcast dispatcher also reaches fallback"), Manager->DispatchEvent(EMT2QuestEvent::Button));
	TestEqual(TEXT("Broadcast fallback body executed"), Manager->GetQuestFlag(TEXT("body"), Quest), 3);
	FMT2QuestTrigger Pure;
	UMT2QuestCondition_Expression* ConstantCondition = NewObject<UMT2QuestCondition_Expression>(Quest);
	ConstantCondition->Expression = TEXT("LIMIT == 1"); Pure.Conditions.Add(ConstantCondition);
	TestTrue(TEXT("Pure gates can read quest constants before body starts"), Manager->PassesTrigger(Pure, Context));
	FMT2QuestTrigger Suspend;
	Suspend.GatePrelude.Add(NewObject<UMT2QuestNode_Wait>(Quest));
	AddExpectedError(TEXT("trigger gate attempted dialogue/suspension"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Gate cannot suspend on dialogue"), Manager->PassesTrigger(Suspend, Context));
	TestTrue(TEXT("Rejected suspended gate leaves no frames"), Manager->CallStack.IsEmpty());
	TestFalse(TEXT("Rejected suspended gate opens no dialog"), Manager->HasPendingConversation());
	FMT2QuestTrigger FailedLock;
	UMT2QuestNode_NpcLock* Lock = NewObject<UMT2QuestNode_NpcLock>(Quest);
	FailedLock.GatePrelude.Add(Lock);
	UMT2QuestCondition_Expression* False = NewObject<UMT2QuestCondition_Expression>(Quest);
	False->Expression = TEXT("false"); FailedLock.GateConditions.Add(False);
	TestFalse(TEXT("Failed final gate rejects after locking helper"), Manager->PassesTrigger(FailedLock, Context));
	TestTrue(TEXT("Failed final gate rolls back newly acquired lease"), Manager->LockedQuestNpcs.IsEmpty());
	TestFalse(TEXT("Gate execution flags restored"), Manager->bEvaluatingTriggerGate || Manager->bRunning);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2QuestTargetDispatchTest, "Metin2.Quests.Importer.TargetDispatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMT2QuestTargetDispatchTest::RunTest(const FString& Parameters)
{
	const TMap<FString, FString> Locale;
	EMT2QuestEvent Event;
	int32 Vnum;
	FName Name;
	FText Label;
	TestTrue(TEXT("Named target click parses"), ParseTriggerSpec(TEXT("teacher.target.click"), Locale, Event, Vnum, Label, nullptr, &Name));
	TestEqual(TEXT("Click verb retained"), Event, EMT2QuestEvent::TargetClick);
	TestEqual(TEXT("Marker name retained"), Name, FName(TEXT("teacher")));
	TestEqual(TEXT("Target not a vnum"), Vnum, 0);
	TestTrue(TEXT("Named arrival parses"), ParseTriggerSpec(TEXT("teacher.target.arrive"), Locale, Event, Vnum, Label, nullptr, &Name));
	TestEqual(TEXT("Arrival distinct from click"), Event, EMT2QuestEvent::Arrive);
	TestTrue(TEXT("Named target disappearance parses"), ParseTriggerSpec(TEXT("teacher.target.die"), Locale, Event, Vnum, Label, nullptr, &Name));
	TestEqual(TEXT("Disappearance distinct from click/arrival"), Event, EMT2QuestEvent::TargetDie);
	for (const TCHAR* Bad : {TEXT("teacher.target"), TEXT("teacher.target.unknown"), TEXT("teacher.target.click.extra"),
		TEXT("__TARGET__target.click"), TEXT("__TARGET__.click")})
	{
		TestFalse(FString(TEXT("Unsupported/malformed retained: ")) + Bad, ParseTriggerSpec(Bad, Locale, Event, Vnum, Label, nullptr, &Name));
	}

	UGameInstance* Instance = NewObject<UGameInstance>(GEngine);
	Instance->InitializeStandalone();
	UWorld* World = Instance->GetWorld();
	ON_SCOPE_EXIT { Instance->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
	UMT2QuestRegistrySubsystem* Registry = Instance->GetSubsystem<UMT2QuestRegistrySubsystem>();
	if (!TestNotNull(TEXT("Registry"), Registry)) { return false; }
	Registry->bLoaded = true;
	Registry->Quests.Reset();
	Registry->QuestsById.Reset();
	auto MakeQuest = [&](const TCHAR* Id)
	{
		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(UMT2Quest::StaticClass(), GetTransientPackage(),
			MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), FName(Id)), BPTYPE_Normal,
			UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
		UMT2Quest* Quest = NewObject<UMT2Quest>(GetTransientPackage(), Blueprint->GeneratedClass);
		Quest->QuestName = Id;
		Quest->States.AddDefaulted_GetRef().StateName = TEXT("start");
		Registry->Quests.Add(Quest);
		Registry->QuestsById.Add(Quest->GetQuestId(), Quest);
		return Quest;
	};
	UMT2Quest* Quest = MakeQuest(TEXT("target_probe"));
	UMT2Quest* OtherQuest = MakeQuest(TEXT("other_probe"));
	auto AddTrigger = [&](UMT2Quest* Definition, EMT2QuestEvent Kind, const TCHAR* MarkerName, const TCHAR* Flag)
	{
		FMT2QuestTrigger& Trigger = Definition->States[0].Triggers.AddDefaulted_GetRef();
		Trigger.Event = Kind;
		Trigger.TriggerName = MarkerName;
		UMT2QuestNode_SetFlag* Node = NewObject<UMT2QuestNode_SetFlag>(Definition);
		Node->FlagName = Flag;
		Node->bAdd = true;
		Trigger.Nodes.Add(Node);
	};
	// Wrong-name trigger intentionally first: dispatch must skip it, not just run the first event.
	AddTrigger(Quest, EMT2QuestEvent::TargetClick, TEXT("wrong"), TEXT("wrong_click"));
	AddTrigger(Quest, EMT2QuestEvent::Arrive, TEXT("teacher"), TEXT("arrivals"));
	AddTrigger(Quest, EMT2QuestEvent::TargetClick, TEXT("teacher"), TEXT("clicks"));
	AddTrigger(OtherQuest, EMT2QuestEvent::TargetClick, TEXT("teacher"), TEXT("clicks"));
	AddTrigger(OtherQuest, EMT2QuestEvent::Arrive, TEXT("teacher"), TEXT("arrivals"));
	AddTrigger(Quest, EMT2QuestEvent::Timer, TEXT("wrong_timer"), TEXT("wrong_timer"));
	AddTrigger(Quest, EMT2QuestEvent::Timer, TEXT("right_timer"), TEXT("right_timer"));
	AddTrigger(Quest, EMT2QuestEvent::TargetDie, TEXT("teacher"), TEXT("disappearances"));
	AddTrigger(OtherQuest, EMT2QuestEvent::TargetDie, TEXT("teacher"), TEXT("disappearances"));
	AMT2PlayerState* State = World->SpawnActor<AMT2PlayerState>();
	AMT2PlayerCharacter* Player = World->SpawnActor<AMT2PlayerCharacter>();
	Player->SetPlayerState(State);
	UMT2QuestManagerComponent* Manager = State->GetQuestManagerComponent();
	AMT2Mob* Npc = World->SpawnActor<AMT2Mob>();
	AMT2Mob* DuplicateNpc = World->SpawnActor<AMT2Mob>();
	FMT2QuestTargetMarker Marker;
	Marker.QuestId = Quest->GetQuestId(); Marker.TargetName = TEXT("teacher");
	Marker.bTracksActor = true; Marker.TargetActor = Npc;
	Manager->SetTargetMarker(Marker);
	TestFalse(TEXT("Same-vnum different actor does not claim click"), Manager->DispatchNpcInteraction(20001, DuplicateNpc));
	TestTrue(TEXT("Named click runs without a menu"), Manager->DispatchNpcInteraction(20001, Npc, true));
	TestEqual(TEXT("Correct click ran"), Manager->GetQuestFlag(TEXT("clicks"), Quest), 1);
	TestEqual(TEXT("Wrong marker did not run"), Manager->GetQuestFlag(TEXT("wrong_click"), Quest), 0);
	TestEqual(TEXT("Click did not execute arrival"), Manager->GetQuestFlag(TEXT("arrivals"), Quest), 0);
	TestEqual(TEXT("Other quest with same name did not run"), Manager->GetQuestFlag(TEXT("clicks"), OtherQuest), 0);
	TestEqual(TEXT("Click retains target until script deletes"), Manager->GetTargetMarkers().Num(), 1);
	TestFalse(TEXT("Target pre-empts shop menu"), Manager->HasPendingConversation());
	TestTrue(TEXT("Named timer dispatch"), Manager->DispatchNamedEvent(EMT2QuestEvent::Timer, TEXT("right_timer")));
	TestEqual(TEXT("Only requested timer ran"), Manager->GetQuestFlag(TEXT("right_timer"), Quest), 1);
	TestEqual(TEXT("Unrelated timer did not run"), Manager->GetQuestFlag(TEXT("wrong_timer"), Quest), 0);
	Npc->SetActorLocation(FVector(400, 400, 5000));
	Player->SetActorLocation(FVector::ZeroVector);
	Manager->CheckTargetArrivals();
	TestEqual(TEXT("Approximate distance outside 500 does not arrive"), Manager->GetQuestFlag(TEXT("arrivals"), Quest), 0);
	Npc->SetActorLocation(FVector(100, 100, 5000));
	Manager->CheckTargetArrivals();
	TestEqual(TEXT("Actor arrival uses live XY, ignores Z"), Manager->GetQuestFlag(TEXT("arrivals"), Quest), 1);
	TestEqual(TEXT("Arrival scoped to marker quest"), Manager->GetQuestFlag(TEXT("arrivals"), OtherQuest), 0);
	Manager->CheckTargetArrivals();
	TestEqual(TEXT("Arrival retries while marker remains"), Manager->GetQuestFlag(TEXT("arrivals"), Quest), 2);
	UMT2QuestCondition_Flag* Gate = NewObject<UMT2QuestCondition_Flag>(Quest);
	Gate->FlagName = TEXT("allowed"); Gate->Value = 1;
	Quest->States[0].Triggers[1].Conditions.Add(Gate);
	Manager->CheckTargetArrivals();
	TestEqual(TEXT("Failed gate does not execute"), Manager->GetQuestFlag(TEXT("arrivals"), Quest), 2);
	Manager->SetQuestFlag(TEXT("allowed"), 1, Quest);
	Manager->CheckTargetArrivals();
	TestEqual(TEXT("Failed gate did not consume future arrival"), Manager->GetQuestFlag(TEXT("arrivals"), Quest), 3);
	Npc->Destroy();
	Manager->CheckTargetArrivals();
	TestEqual(TEXT("Destroyed entity runs die handler"), Manager->GetQuestFlag(TEXT("disappearances"), Quest), 1);
	TestEqual(TEXT("Die handler scoped to marker quest"), Manager->GetQuestFlag(TEXT("disappearances"), OtherQuest), 0);
	TestTrue(TEXT("Dead actor target removed after die handler"), Manager->GetTargetMarkers().IsEmpty());
	Manager->CheckTargetArrivals();
	TestEqual(TEXT("Removed actor target does not repeat die handler"), Manager->GetQuestFlag(TEXT("disappearances"), Quest), 1);
	Manager->ClearTargetMarker(Marker.TargetName, Marker.QuestId);
	Marker.bTracksActor = false; Marker.TargetActor = nullptr; Marker.WorldPosition = FVector2D::ZeroVector;
	Manager->SetTargetMarker(Marker);
	Manager->CheckTargetArrivals();
	TestEqual(TEXT("World origin is a valid position target"), Manager->GetQuestFlag(TEXT("arrivals"), Quest), 4);
	Manager->ClearTargetMarker(Marker.TargetName, Marker.QuestId);
	Manager->CheckTargetArrivals();
	TestEqual(TEXT("Deleted marker no longer arrives"), Manager->GetQuestFlag(TEXT("arrivals"), Quest), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2QuestLoweringTest, "Metin2.Quests.Importer.ExpressionLowering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMT2QuestLoweringTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AMT2PlayerState* PlayerState = World->SpawnActor<AMT2PlayerState>();
	UMT2QuestManagerComponent* Manager = PlayerState->GetQuestManagerComponent();
	FMT2QuestContext Context;
	Context.Manager = Manager;
	Context.PlayerState = PlayerState;
	Manager->ActiveContext = Context;
	const TArray<FString> Lines = {
		TEXT("pc.setqf(\"calls\", pc.getqf(\"calls\") + 1)"), TEXT("return 7"),
		TEXT("wait()"), TEXT("return 9"), TEXT("x = 99"), TEXT("return 1"), TEXT("return a + a")};
	const TArray<FParsedFunction> Functions = {
		{TEXT("echo"), {TEXT("a")}, 6, 7}, {TEXT("mutate"), {}, 4, 6},
		{TEXT("pause"), {}, 2, 4}, {TEXT("tick"), {}, 0, 2}};
	const TMap<FString, FString> Locale;
	FMT2QuestImportResult Result;
	FTranslateState State;
	State.Locale = &Locale;
	State.Outer = Manager;
	State.ScriptName = State.QuestName = TEXT("probe");
	State.Functions = &Functions;
	State.Result = &Result;
	State.KnownVariables.Add(TEXT("x"));
	State.KnownVariables.Add(TEXT("t"));
	auto Run = [&](const TCHAR* Source)
	{
		Manager->CancelConversation();
		Manager->SetQuestFlag(TEXT("calls"), 0);
		Manager->SetScriptVariable(TEXT("x"), FMT2QuestValue(5.0));
		FString Expression(Source);
		TArray<TObjectPtr<UMT2QuestNode>> Nodes;
		if (!TestTrue(FString(TEXT("Lower: ")) + Source, ExtractRuntimeFunctionCalls(Lines, Expression, State, Nodes))) { return false; }
		UMT2QuestNode_SetVariable* Publish = NewObject<UMT2QuestNode_SetVariable>();
		Publish->VariableName = TEXT("result");
		Publish->Expression = Expression;
		Nodes.Add(Publish);
		Manager->PushFrame(Nodes);
		Manager->RunPendingFrames();
		return true;
	};
	if (!Run(TEXT("false and probe.tick()"))) { return false; }
	TestEqual(TEXT("Skipped and does not invoke helper"), Manager->GetQuestFlag(TEXT("calls")), 0);
	TestTrue(TEXT("and preserves false boolean operand"), Manager->GetScriptVariable(TEXT("result")).bIsBoolean);
	if (!Run(TEXT("0 or probe.tick()"))) { return false; }
	TestEqual(TEXT("Zero is truthy, RHS skipped"), Manager->GetQuestFlag(TEXT("calls")), 0);
	TestEqual(TEXT("or preserves numeric operand"), Manager->GetScriptVariable(TEXT("result")).Number, 0.0);
	if (!Run(TEXT("nil or (false or probe.tick())"))) { return false; }
	TestEqual(TEXT("Nested RHS invokes once"), Manager->GetQuestFlag(TEXT("calls")), 1);
	TestEqual(TEXT("Nested operand result"), Manager->GetScriptVariable(TEXT("result")).Number, 7.0);
	if (!Run(TEXT("not (false and probe.tick())"))) { return false; }
	TestEqual(TEXT("Unary/grouping preserves lazy branch"), Manager->GetQuestFlag(TEXT("calls")), 0);
	if (!Run(TEXT("x + probe.mutate()"))) { return false; }
	TestEqual(TEXT("Left read captured before mutation"), Manager->GetScriptVariable(TEXT("result")).Number, 6.0);
	TestEqual(TEXT("Helper mutation ran"), Manager->GetScriptVariable(TEXT("x")).Number, 99.0);
	if (!Run(TEXT("math.max(x, probe.mutate())"))) { return false; }
	TestEqual(TEXT("Native arguments captured before later helper"), Manager->GetScriptVariable(TEXT("result")).Number, 5.0);
	if (!Run(TEXT("probe.mutate() + x"))) { return false; }
	TestEqual(TEXT("Right read occurs after helper"), Manager->GetScriptVariable(TEXT("result")).Number, 100.0);
	if (!Run(TEXT("probe.echo(probe.tick())"))) { return false; }
	TestEqual(TEXT("Nested argument executes once"), Manager->GetQuestFlag(TEXT("calls")), 1);
	TestEqual(TEXT("Nested argument result"), Manager->GetScriptVariable(TEXT("result")).Number, 14.0);
	TestEqual(TEXT("Stateful argument is not textually duplicated"),
		ExpandPureFunctionCalls(Lines, TEXT("probe.echo(probe.tick())"), State), FString(TEXT("probe.echo(probe.tick())")));
	if (!Run(TEXT("\"probe.tick()\""))) { return false; }
	TestEqual(TEXT("Quoted call is data"), Manager->GetQuestFlag(TEXT("calls")), 0);
	TestEqual(TEXT("Quoted call text retained"), Manager->GetScriptVariable(TEXT("result")).Text, FString(TEXT("probe.tick()")));
	if (!Run(TEXT("false and (probe.tick() + (1 / 0))"))) { return false; }
	TestEqual(TEXT("Skipped subtree has no call"), Manager->GetQuestFlag(TEXT("calls")), 0);
	TestTrue(TEXT("Skipped invalid arithmetic leaves false result"), Manager->GetScriptVariable(TEXT("result")).bIsBoolean);
	if (!Run(TEXT("false and probe.pause()"))) { return false; }
	TestTrue(TEXT("Skipped dialogue helper does not suspend"), Manager->CallStack.IsEmpty());
	if (!Run(TEXT("true and probe.pause() + probe.tick()"))) { return false; }
	TestFalse(TEXT("Taken dialogue helper suspends"), Manager->CallStack.IsEmpty());
	TestEqual(TEXT("Later helper not executed before resume"), Manager->GetQuestFlag(TEXT("calls")), 0);
	PlayerState->GetQuestComponent()->ServerAnswerDialog_Implementation(1);
	TestEqual(TEXT("Resumed expression executes later helper once"), Manager->GetQuestFlag(TEXT("calls")), 1);
	TestEqual(TEXT("Resumed expression result"), Manager->GetScriptVariable(TEXT("result")).Number, 16.0);
	TestTrue(TEXT("Resumed expression completed"), Manager->CallStack.IsEmpty());
	bool bTableOk = false;
	Manager->SetScriptVariable(TEXT("t"), FMT2QuestExpression::Evaluate(TEXT("{[7]={a={b=42}}}"), Context, bTableOk));
	TestTrue(TEXT("Table fixture"), bTableOk);
	if (!Run(TEXT("t[probe.tick()].a.b"))) { return false; }
	TestEqual(TEXT("Nested field indexing after a helper"), Manager->GetScriptVariable(TEXT("result")).Number, 42.0);
	TestEqual(TEXT("Index helper executes once"), Manager->GetQuestFlag(TEXT("calls")), 1);
	TArray<FString> AssignmentLines = Lines;
	AssignmentLines.Append({TEXT("local assigned = false and probe.tick()"), TEXT("return assigned")});
	const int32 BeforeDiagnostics = Result.StatementsUnconverted;
	const auto AssignmentNodes = TranslateBlock(AssignmentLines, Lines.Num(), AssignmentLines.Num(), State);
	TestEqual(TEXT("Actual assignment/return translates"), Result.StatementsUnconverted, BeforeDiagnostics);
	Manager->SetQuestFlag(TEXT("calls"), 0);
	Manager->PushFrame(AssignmentNodes);
	Manager->RunPendingFrames();
	TestEqual(TEXT("Actual assignment preserves short circuit"), Manager->GetQuestFlag(TEXT("calls")), 0);
	TestTrue(TEXT("Actual assignment result is false"), Manager->GetScriptVariable(TEXT("assigned")).bIsBoolean &&
		!Manager->GetScriptVariable(TEXT("assigned")).AsBool());
	TArray<FString> InlineLines = Lines;
	InlineLines.Append({TEXT("if false and probe.tick() then probe.tick() end"), TEXT("probe.echo(probe.tick())")});
	const int32 InlineDiagnostics = Result.StatementsUnconverted;
	const auto InlineNodes = TranslateBlock(InlineLines, Lines.Num(), InlineLines.Num(), State);
	TestEqual(TEXT("Inline if and nested standalone call translate"), Result.StatementsUnconverted, InlineDiagnostics);
	Manager->SetQuestFlag(TEXT("calls"), 0);
	Manager->PushFrame(InlineNodes);
	Manager->RunPendingFrames();
	TestEqual(TEXT("Inline branch skipped; standalone argument executes once"), Manager->GetQuestFlag(TEXT("calls")), 1);
	FString Unsupported(TEXT("probe.tick() + missing.native()"));
	TArray<TObjectPtr<UMT2QuestNode>> Rejected;
	const int32 BeforeNodes = Result.NodesCreated;
	const int32 BeforeTemporaries = State.GeneratedTemporaryIndex;
	TestFalse(TEXT("Unsupported subtree stays rejected"), ExtractRuntimeFunctionCalls(Lines, Unsupported, State, Rejected));
	TestTrue(TEXT("Rejected lowering emits no partial prelude"), Rejected.IsEmpty());
	TestEqual(TEXT("Rejected lowering rolls back node count"), Result.NodesCreated, BeforeNodes);
	TestEqual(TEXT("Rejected lowering rolls back temporaries"), State.GeneratedTemporaryIndex, BeforeTemporaries);
	return true;
}
#endif
