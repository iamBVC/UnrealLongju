/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Quests/MT2QuestExpression.h"

#include "Characters/MT2PlayerCharacter.h"
#include "Components/MT2HealthComponent.h"
#include "Components/MT2ManaComponent.h"
#include "Components/MT2StatusEffectComponent.h"
#include "Components/MT2StatusEffectDefinition.h"
#include "EngineUtils.h"
#include "Core/MT2VnumRegistrySubsystem.h"
#include "Game/MT2GameStateBase.h"
#include "Guild/MT2GuildSubsystem.h"
#include "Engine/GameInstance.h"
#include "Persistence/MT2PersistenceComponent.h"
#include "Items/MT2InventoryComponent.h"
#include "Items/MT2ItemTemplate.h"
#include "Mobs/MT2Mob.h"
#include "Mounts/MT2MountComponent.h"
#include "Npcs/MT2Npc.h"
#include "Party/MT2Party.h"
#include "Player/MT2PlayerState.h"
#include "Quests/MT2Quest.h"
#include "Quests/MT2QuestEntitySubsystem.h"
#include "Quests/MT2QuestManagerComponent.h"
#include "Quests/MT2QuestLuaPattern.h"
#include "Skills/MT2SkillComponent.h"
#include "World/MT2MapPresentationActor.h"
#include "Misc/PackageName.h"
#include <cstdio>

namespace
{
	bool SameLuaValue(const FMT2QuestValue& Left, const FMT2QuestValue& Right)
	{
		if (Left.bIsNil || Right.bIsNil) { return Left.bIsNil && Right.bIsNil; }
		if (Left.IsTableRef() || Right.IsTableRef())
		{
			return (Left.RuntimeTable && Left.RuntimeTable == Right.RuntimeTable) ||
				(Left.Table && Left.Table == Right.Table && Left.TableNodeIndex == Right.TableNodeIndex);
		}
		if (Left.bIsBoolean != Right.bIsBoolean || Left.bIsText != Right.bIsText) { return false; }
		return Left.bIsText ? Left.Text.Equals(Right.Text, ESearchCase::CaseSensitive) : Left.Number == Right.Number;
	}

	bool TryNumericValue(const FMT2QuestValue& Value, double& OutNumber)
	{
		if (Value.bIsNil || Value.bIsBoolean || Value.IsTableRef()) { return false; }
		if (!Value.bIsText) { OutNumber = Value.Number; return true; }
		const FString Text = Value.Text.TrimStartAndEnd();
		int32 Cursor = 0, Digits = 0;
		if (Text.IsEmpty()) { return false; }
		if (Text[Cursor] == TEXT('+') || Text[Cursor] == TEXT('-')) { ++Cursor; }
		while (Cursor < Text.Len() && FChar::IsDigit(Text[Cursor])) { ++Cursor; ++Digits; }
		if (Cursor < Text.Len() && Text[Cursor] == TEXT('.'))
		{
			++Cursor;
			while (Cursor < Text.Len() && FChar::IsDigit(Text[Cursor])) { ++Cursor; ++Digits; }
		}
		if (!Digits) { return false; }
		if (Cursor < Text.Len() && (Text[Cursor] == TEXT('e') || Text[Cursor] == TEXT('E')))
		{
			++Cursor;
			if (Cursor < Text.Len() && (Text[Cursor] == TEXT('+') || Text[Cursor] == TEXT('-'))) { ++Cursor; }
			const int32 ExponentStart = Cursor;
			while (Cursor < Text.Len() && FChar::IsDigit(Text[Cursor])) { ++Cursor; }
			if (Cursor == ExponentStart) { return false; }
		}
		if (Cursor != Text.Len()) { return false; }
		OutNumber = FCString::Atod(*Text);
		return true;
	}
	bool TryLuaString(const FMT2QuestValue& Value, FString& OutText)
	{
		if (Value.bIsNil || Value.bIsBoolean || Value.IsTableRef()) { return false; }
		OutText = Value.bIsText ? Value.Text : FString::Printf(TEXT("%.14g"), Value.Number);
		return true;
	}

	bool FormatLuaString(const TArray<FMT2QuestValue>& Arguments, FString& OutText)
	{
		FString Format;
		if (Arguments.IsEmpty() || !TryLuaString(Arguments[0], Format)) { return false; }
		const FTCHARToUTF8 FormatBytes(*Format, Format.Len());
		TArray<ANSICHAR> Output;
		int32 ArgumentIndex = 1;
		constexpr int32 OutputLimit = 1024 * 1024;
		auto Append = [&](const ANSICHAR* Bytes, int32 Length)
		{
			if (Length < 0 || Length > OutputLimit - Output.Num()) { return false; }
			Output.Append(Bytes, Length); return true;
		};
		for (int32 Index = 0; Index < FormatBytes.Length();)
		{
			const ANSICHAR* Source = FormatBytes.Get();
			if (Source[Index] != '%')
			{
				if (!Append(Source + Index++, 1)) { return false; }
				continue;
			}
			const int32 Start = Index++;
			if (Index < FormatBytes.Length() && Source[Index] == '%')
			{
				if (!Append(Source + Index++, 1)) { return false; }
				continue;
			}
			// Lua 5.0 permits flags, at most two width/precision digits, and no length modifiers.
			while (Index < FormatBytes.Length() && (Source[Index] == '-' || Source[Index] == '+' ||
				Source[Index] == ' ' || Source[Index] == '#' || Source[Index] == '0')) { ++Index; }
			auto ScanDigits = [&]()
			{
				int32 Count = 0;
				while (Index < FormatBytes.Length() && Source[Index] >= '0' && Source[Index] <= '9') { ++Index; ++Count; }
				return Count <= 2;
			};
			if (!ScanDigits()) { return false; }
			const bool bHasPrecision = Index < FormatBytes.Length() && Source[Index] == '.';
			if (bHasPrecision) { ++Index; if (!ScanDigits()) { return false; } }
			if (Index >= FormatBytes.Length() || Index - Start + 1 > 20 || !Arguments.IsValidIndex(ArgumentIndex)) { return false; }
			const ANSICHAR Conversion = Source[Index++];
			ANSICHAR Spec[21] = {};
			FMemory::Memcpy(Spec, Source + Start, Index - Start);
			const FMT2QuestValue& Value = Arguments[ArgumentIndex++];
			ANSICHAR Buffer[512] = {};
			int32 Length = -1;
			if (Conversion == 's' || Conversion == 'q')
			{
				FString Text;
				if (!TryLuaString(Value, Text)) { return false; }
				const FTCHARToUTF8 Bytes(*Text, Text.Len());
				if (Conversion == 'q')
				{
					if (!Append("\"", 1)) { return false; }
					for (int32 ByteIndex = 0; ByteIndex < Bytes.Length(); ++ByteIndex)
					{
						const ANSICHAR Byte = Bytes.Get()[ByteIndex];
						if (Byte == 0) { if (!Append("\\000", 4)) { return false; } continue; }
						if ((Byte == '"' || Byte == '\\' || Byte == '\n') && !Append("\\", 1)) { return false; }
						if (!Append(&Byte, 1)) { return false; }
					}
					if (!Append("\"", 1)) { return false; }
					continue;
				}
				// Preserve Lua 5.0's long-string shortcut, including its ignored width.
				if (!bHasPrecision && Bytes.Length() >= 100)
				{
					if (!Append(Bytes.Get(), Bytes.Length())) { return false; }
					continue;
				}
				TArray<ANSICHAR> CString;
				CString.Append(Bytes.Get(), Bytes.Length());
				CString.Add(0);
				Length = std::snprintf(Buffer, sizeof(Buffer), Spec, CString.GetData());
			}
			else
			{
				double Number = 0;
				if (!TryNumericValue(Value, Number)) { return false; }
				if (Conversion == 'c' || Conversion == 'd' || Conversion == 'i')
				{
					if (!FMath::IsFinite(Number)) { return false; }
					const double Integer = FMath::TruncToDouble(Number);
					if (Integer < MIN_int32 || Integer > MAX_int32) { return false; }
					Length = std::snprintf(Buffer, sizeof(Buffer), Spec, static_cast<int32>(Integer));
				}
				else if (Conversion == 'o' || Conversion == 'u' || Conversion == 'x' || Conversion == 'X')
				{
					if (!FMath::IsFinite(Number)) { return false; }
					const double Integer = FMath::TruncToDouble(Number);
					if (Integer < MIN_int32 || Integer > MAX_uint32) { return false; }
					const uint32 Unsigned = Integer < 0 ? static_cast<uint32>(static_cast<int32>(Integer)) : static_cast<uint32>(Integer);
					Length = std::snprintf(Buffer, sizeof(Buffer), Spec, Unsigned);
				}
				else if (Conversion == 'e' || Conversion == 'E' || Conversion == 'f' || Conversion == 'g' || Conversion == 'G')
				{
					Length = std::snprintf(Buffer, sizeof(Buffer), Spec, Number);
				}
				else { return false; }
			}
			if (Length < 0 || Length >= UE_ARRAY_COUNT(Buffer)) { return false; }
			// Lua 5.0 appends strlen(buff), so a formatted NUL character produces no bytes.
			if (!Append(Buffer, FCStringAnsi::Strlen(Buffer))) { return false; }
		}
		if (Output.IsEmpty()) { OutText.Empty(); return true; }
		const FUTF8ToTCHAR Converted(Output.GetData(), Output.Num());
		OutText = FString(Converted.Length(), Converted.Get());
		// FString cannot represent arbitrary Lua byte strings. Reject lossy conversions explicitly.
		const FTCHARToUTF8 RoundTrip(*OutText, OutText.Len());
		return RoundTrip.Length() == Output.Num() && FMemory::Memcmp(RoundTrip.Get(), Output.GetData(), Output.Num()) == 0;
	}
	// ---------------------------------------------------------------------------------------------
	// Tokenizer
	// ---------------------------------------------------------------------------------------------
	enum class ETokenKind : uint8 { End, Number, String, Identifier, Operator };

	struct FToken
	{
		ETokenKind Kind = ETokenKind::End;
		FString Text;
		double Number = 0.0;
	};

	// Multi-character operators must be tested before their single-character prefixes.
	const TCHAR* const MultiCharOperators[] = {
		TEXT("=="), TEXT("~="), TEXT("!="), TEXT(">="), TEXT("<="), TEXT("..")
	};

	TArray<FToken> Tokenize(const FString& Source)
	{
		TArray<FToken> Tokens;
		int32 Index = 0;
		while (Index < Source.Len())
		{
			const TCHAR Char = Source[Index];
			if (FChar::IsWhitespace(Char)) { ++Index; continue; }

			// Number
			if (FChar::IsDigit(Char) ||
				(Char == TEXT('.') && Index + 1 < Source.Len() && FChar::IsDigit(Source[Index + 1])))
			{
				const int32 Start = Index;
				bool bDecimal = false;
				while (Index < Source.Len())
				{
					if (FChar::IsDigit(Source[Index])) { ++Index; continue; }
					if (!bDecimal && Source[Index] == TEXT('.') &&
						(Index + 1 >= Source.Len() || Source[Index + 1] != TEXT('.')))
					{
						bDecimal = true;
						++Index;
						continue;
					}
					break;
				}
				FToken Token;
				Token.Kind = ETokenKind::Number;
				Token.Number = FCString::Atod(*Source.Mid(Start, Index - Start));
				Tokens.Add(Token);
				continue;
			}

			// String literal
			if (Char == TEXT('"') || Char == TEXT('\''))
			{
				const TCHAR Quote = Char;
				++Index;
				FString Text;
				while (Index < Source.Len() && Source[Index] != Quote)
				{
					if (Source[Index] == TEXT('\\') && Index + 1 < Source.Len())
					{
						++Index;
						const TCHAR Escape = Source[Index++];
						Text.AppendChar(Escape == TEXT('n') ? TEXT('\n') : Escape == TEXT('r') ? TEXT('\r') :
							Escape == TEXT('t') ? TEXT('\t') : Escape);
						continue;
					}
					Text.AppendChar(Source[Index++]);
				}
				++Index; // closing quote
				FToken Token;
				Token.Kind = ETokenKind::String;
				Token.Text = Text;
				Tokens.Add(Token);
				continue;
			}

			// Identifier (dotted names like pc.get_level stay one token)
			if (FChar::IsAlpha(Char) || Char == TEXT('_'))
			{
				const int32 Start = Index;
				while (Index < Source.Len() &&
					(FChar::IsAlnum(Source[Index]) || Source[Index] == TEXT('_') ||
					 (Source[Index] == TEXT('.') && Index + 1 < Source.Len() &&
					  (FChar::IsAlpha(Source[Index + 1]) || Source[Index + 1] == TEXT('_')))))
				{
					++Index;
				}
				FToken Token;
				Token.Kind = ETokenKind::Identifier;
				Token.Text = Source.Mid(Start, Index - Start);
				Tokens.Add(Token);
				continue;
			}

			// Operator
			FToken Token;
			Token.Kind = ETokenKind::Operator;
			bool bMatched = false;
			for (const TCHAR* Operator : MultiCharOperators)
			{
				if (Source.Mid(Index, 2) == Operator)
				{
					Token.Text = Operator;
					Index += 2;
					bMatched = true;
					break;
				}
			}
			if (!bMatched)
			{
				Token.Text = FString::Chr(Char);
				++Index;
			}
			Tokens.Add(Token);
		}
		Tokens.Add(FToken());
		return Tokens;
	}

	// ---------------------------------------------------------------------------------------------
	// Function binding
	// ---------------------------------------------------------------------------------------------

	// Zero-argument properties the scripts read without parentheses (pc.level, pc.name, ...).
	const TSet<FString>& GetPropertyNames()
	{
		static const TSet<FString> Names = {
			TEXT("pc.level"), TEXT("pc.name"), TEXT("pc.job"), TEXT("pc.gold"), TEXT("pc.money"),
			TEXT("pc.empire"), TEXT("pc.sex"), TEXT("pc.hp"), TEXT("pc.maxhp"), TEXT("pc.sp"),
			TEXT("pc.maxsp"), TEXT("pc.playtime"), TEXT("pc.vid"),
			// Read bare in the scripts, e.g. `npc.race == special.levelup_quest[lev][1]` in the kill
			// block of levelup.quest. They are also callable, so they appear in both sets.
			TEXT("npc.race"), TEXT("npc.vnum"), TEXT("npc.empire"), TEXT("npc.level"),
			TEXT("guild.level"), TEXT("guild.name"), TEXT("item.vnum"), TEXT("item.count")
		};
		return Names;
	}

	const TSet<FString>& GetFunctionNames()
	{
		static const TSet<FString> Names = {
			// player
			TEXT("pc.get_level"), TEXT("pc.count_item"), TEXT("pc.countitem"), TEXT("pc.getqf"),
			TEXT("pc.get_empire"), TEXT("pc.get_money"), TEXT("pc.get_gold"), TEXT("pc.get_job"),
			TEXT("pc.get_skill_group"), TEXT("pc.get_map_index"), TEXT("pc.getf"), TEXT("pc.get_name"),
			TEXT("pc.get_sex"), TEXT("pc.get_race"), TEXT("pc.get_hp"), TEXT("pc.get_max_hp"),
			TEXT("pc.gethp"), TEXT("pc.getmaxhp"), TEXT("pc.get_max_sp"), TEXT("pc.getmaxsp"),
			TEXT("pc.get_sp"), TEXT("pc.getsp"), TEXT("pc.get_exp"), TEXT("pc.get_next_exp"),
			TEXT("pc.get_align"), TEXT("pc.get_change_empire_count"),
			TEXT("pc.get_special_ride_vnum"), TEXT("pc.get_special_ride_remaining"),
			TEXT("pc.is_gm"), TEXT("pc.get_guild_id"), TEXT("pc.get_vid"), TEXT("pc.get_local_x"),
			TEXT("pc.get_local_y"), TEXT("pc.get_x"), TEXT("pc.get_y"), TEXT("pc.get_skill_level"), TEXT("pc.has_master_skill"),
			TEXT("pc.is_skill_book_no_delay"), TEXT("pc.remove_skill_book_no_delay"),
			TEXT("pc.learn_grand_master_skill"),
			TEXT("pc.get_real_alignment"), TEXT("pc.get_alignment"), TEXT("pc.change_alignment"), TEXT("pc.changealignment"),
			// game / world
			TEXT("game.get_event_flag"), TEXT("game.get_time"), TEXT("get_time"),
			TEXT("find_npc_by_vnum"), TEXT("mob_name"), TEXT("mob2_name"), TEXT("item_name"),
			TEXT("get_server_timer_arg"), TEXT("get_channel_id"),
			// math
			TEXT("math.random"), TEXT("math.floor"), TEXT("math.ceil"), TEXT("math.abs"),
			TEXT("math.min"), TEXT("math.max"), TEXT("number"),
			TEXT("time_min_to_sec"), TEXT("time_hour_to_sec"), TEXT("time_day_to_sec"),
			// horse
			TEXT("horse.get_level"), TEXT("horse.get_grade"), TEXT("horse.is_summon"),
			// string
			TEXT("string.format"), TEXT("string.len"), TEXT("string.lower"), TEXT("string.upper"), TEXT("string.gsub"), TEXT("string.find"), TEXT("tostring"),
			TEXT("get_quest_state"), TEXT("type"),
			// server/environment predicates the scripts branch on
			TEXT("is_test_server"), TEXT("get_global_time"), TEXT("pc_find_skill_teacher_vid"),
			TEXT("pc.is_clear_skill_group"), TEXT("pc.get_skill_group_name"), TEXT("pc.is_polymorphed"),
			TEXT("pc.is_engaged"), TEXT("pc.is_married"), TEXT("pc.has_guild"), TEXT("pc.is_riding"),
			TEXT("pc.count_item_by_vnum"), TEXT("pc.get_wear"), TEXT("pc.get_part"),
			// Spelling variants the scripts use interchangeably with the ones above, plus the reads
			// that had no binding at all (each one blocked every condition it appeared in).
			TEXT("pc.is_mount"), TEXT("pc.hasguild"), TEXT("pc.getempire"), TEXT("pc.get_guild"),
			TEXT("pc.isguildmaster"), TEXT("pc.is_guild_master"), TEXT("pc.get_gm_level"),
			TEXT("pc.in_dungeon"), TEXT("pc.is_dead"), TEXT("pc.get_player_id"),
			// questlib.lua helpers (lines 118, 147): thin wrappers over the reads above.
			TEXT("pc_is_novice"), TEXT("npc_is_same_empire"), TEXT("npc_is_same_job"),
			TEXT("table_is_in"), TEXT("next_time_is_now"),
			TEXT("count_item_range"), TEXT("remove_item_range"),
			TEXT("tonumber"), TEXT("math.mod"), TEXT("table.getn"), TEXT("affect.get_apply_on"),
			TEXT("horse.is_dead"), TEXT("horse.is_ride"), TEXT("npc.get_guild"),
			TEXT("item.get_socket"), TEXT("item.get_vnum"), TEXT("item.vnum"),
			TEXT("item.get_count"), TEXT("item.get_cell"), TEXT("item.get_value"),
			TEXT("item.get_type"), TEXT("item.get_sub_type"), TEXT("item.get_level"),
			TEXT("item.get_level_limit"), TEXT("item.get_refine_vnum"), TEXT("item.next_refine_vnum"),
			TEXT("party.is_party"), TEXT("party.is_leader"), TEXT("party.get_member_count"),
			TEXT("party.getf"), TEXT("party.setf"),
			// the NPC/mob that raised the event (Context.TargetActor)
			TEXT("npc.get_race"), TEXT("npc.race"), TEXT("npc.get_vnum"), TEXT("npc.vnum"),
			TEXT("npc.is_pc"), TEXT("npc.get_empire"), TEXT("npc.empire"), TEXT("npc.get_level"),
			TEXT("npc.level"),
			TEXT("npc.lock"), TEXT("npc.unlock"), TEXT("npc.is_near"),
			TEXT("npc.purge"),
			TEXT("npc.get_vid"), TEXT("npc.is_near_vid"), TEXT("find_pc_by_name"),
			TEXT("is_destination_village"),
			TEXT("guild.get_level"), TEXT("guild.is_guild_master"), TEXT("guild.level"),
			TEXT("guild.get_name"), TEXT("guild.name"), TEXT("guild.get_id"),
			TEXT("guild.get_member_count"), TEXT("guild.get_ladder_point"), TEXT("guild.get_rank")
		};
		return Names;
	}

	// Defined below, next to the rest of the table plumbing; the questlib helpers need it here.
	const UMT2QuestTableAsset* GetQuestTables();

	FMT2QuestValue CallFunctionValue(
		const FString& Name, const TArray<FMT2QuestValue>& Arguments,
		const FMT2QuestContext& Context, bool& bOutOk)
	{
		const AMT2PlayerState* State = Context.PlayerState;
		const AMT2PlayerCharacter* Player = Context.Player;
		const UMT2InventoryComponent* Inventory = Player ? Player->GetInventoryComponent() : nullptr;

		auto Argument = [&Arguments](int32 Index) -> FMT2QuestValue
		{
			return Arguments.IsValidIndex(Index) ? Arguments[Index] : FMT2QuestValue();
		};

		// ---- player ----
		if (Name == TEXT("pc.get_real_alignment"))
		{
			return FMT2QuestValue(State ? State->GetRawAlignment() / 10 : 0);
		}
		if (Name == TEXT("pc.get_alignment") || Name == TEXT("pc.get_align"))
		{
			return FMT2QuestValue(State ? State->GetDisplayedRawAlignment() / 10 : 0);
		}
		if (Name == TEXT("pc.change_alignment") || Name == TEXT("pc.changealignment"))
		{
			double Amount = 0;
			// lua_tonumber yields zero for nil, booleans, tables and nonnumeric strings.
			TryNumericValue(Argument(0), Amount);
			const double RawAmount = Amount * 10.0;
			if (!FMath::IsFinite(RawAmount) || RawAmount < MIN_int32 || RawAmount > MAX_int32)
			{
				bOutOk = false; return FMT2QuestValue();
			}
			if (Context.PlayerState)
			{
				if (!Context.PlayerState->HasAuthority()) { bOutOk = false; return FMT2QuestValue(); }
				Context.PlayerState->ChangeLegacyAlignment(static_cast<int32>(RawAmount));
			}
			return FMT2QuestValue();
		}
		if (Name == TEXT("pc.learn_grand_master_skill"))
		{
			double SkillVnum = 0;
			if (!TryNumericValue(Argument(0), SkillVnum)) { return FMT2QuestValue(); }
			if (!FMath::IsFinite(SkillVnum) || SkillVnum < MIN_int32 || SkillVnum > MAX_int32)
			{
				bOutOk = false; return FMT2QuestValue();
			}
			UMT2SkillComponent* Skills = Context.PlayerState ? Context.PlayerState->GetSkillComponent() : nullptr;
			return FMT2QuestValue::Boolean(Skills && Skills->TrainGrandMasterSkill(static_cast<int32>(SkillVnum)));
		}
		if (Name == TEXT("pc.is_skill_book_no_delay") || Name == TEXT("pc.remove_skill_book_no_delay"))
		{
			UMT2StatusEffectComponent* Effects = Context.Player ? Context.Player->GetStatusEffectComponent() : nullptr;
			if (Name == TEXT("pc.is_skill_book_no_delay"))
			{
				return FMT2QuestValue::Boolean(Effects && Effects->HasEffectType(MT2AffectId::SkillBookNoCooldown));
			}
			if (Effects && Context.Player->HasAuthority())
			{
				Effects->RemoveEffectsByType(MT2AffectId::SkillBookNoCooldown);
			}
			return FMT2QuestValue();
		}
		if (Name == TEXT("count_item_range") || Name == TEXT("remove_item_range"))
		{
			const bool bRemove = Name == TEXT("remove_item_range");
			int32 Values[3] = {};
			const int32 Required = bRemove ? 3 : 2;
			if (!Inventory || (bRemove && !Player->HasAuthority())) { bOutOk = false; return FMT2QuestValue(); }
			for (int32 Index = 0; Index < Required; ++Index)
			{
				double Number = 0;
				// Item identifiers/counts must be representable by the existing inventory model.
				if (!TryNumericValue(Argument(Index), Number) || !FMath::IsFinite(Number) ||
					Number < 0 || Number > MAX_int32 || FMath::FloorToDouble(Number) != Number)
				{
					bOutOk = false; return FMT2QuestValue();
				}
				Values[Index] = static_cast<int32>(Number);
			}
			if (!bRemove)
			{
				return FMT2QuestValue(static_cast<double>(Inventory->CountItemsInVnumRange(Values[0], Values[1])));
			}
			// GFquestlib returns true for a zero cost only if it encounters an owned item;
			// with no matching items the helper falls through, yielding nil rather than false.
			if (Values[0] == 0)
			{
				return Inventory->CountItemsInVnumRange(Values[1], Values[2]) > 0
					? FMT2QuestValue::Boolean(true) : FMT2QuestValue();
			}
			return FMT2QuestValue::Boolean(Context.Player->GetInventoryComponent()->RemoveItemsInVnumRange(
				Values[0], Values[1], Values[2]));
		}
		if (Name == TEXT("pc.get_level") || Name == TEXT("pc.level"))
		{
			return FMT2QuestValue(State ? State->GetCharacterLevel() : 0);
		}
		if (Name == TEXT("pc.count_item") || Name == TEXT("pc.countitem"))
		{
			return FMT2QuestValue(Inventory ? Inventory->CountItemByVnum(Argument(0).AsInt()) : 0);
		}
		if (Name == TEXT("pc.getqf"))
		{
			return FMT2QuestValue(Context.Manager
				? Context.Manager->GetQuestFlag(FName(*Argument(0).Text), Context.Quest) : 0);
		}
		if (Name == TEXT("pc.getf"))
		{
			// pc.getf("quest", "flag") - a fully qualified flag read.
			const FString Key = Arguments.Num() >= 2
				? Argument(0).Text + TEXT(".") + Argument(1).Text : Argument(0).Text;
			return FMT2QuestValue(Context.Manager
				? Context.Manager->GetQuestFlag(FName(*Key), Context.Quest) : 0);
		}
		if (Name == TEXT("pc.get_empire") || Name == TEXT("pc.empire") || Name == TEXT("pc.getempire"))
		{
			return FMT2QuestValue(State ? static_cast<int32>(State->GetEmpire()) : 0);
		}
		if (Name == TEXT("pc.get_money") || Name == TEXT("pc.get_gold") ||
			Name == TEXT("pc.gold") || Name == TEXT("pc.money"))
		{
			return FMT2QuestValue(static_cast<double>(State ? State->GetYang() : 0));
		}
		if (Name == TEXT("pc.get_job") || Name == TEXT("pc.job") ||
			Name == TEXT("pc.get_race") || Name == TEXT("pc.race"))
		{
			return FMT2QuestValue(State
				? static_cast<int32>(State->GetCharacterAppearance().Race) : 0);
		}
		if (Name == TEXT("pc.get_sex") || Name == TEXT("pc.sex"))
		{
			return FMT2QuestValue(State
				? static_cast<int32>(State->GetCharacterAppearance().Sex) : 0);
		}
		if (Name == TEXT("pc.get_name") || Name == TEXT("pc.name"))
		{
			return FMT2QuestValue(State ? State->GetCharacterName() : FString());
		}
		if (Name == TEXT("pc.get_skill_group"))
		{
			const UMT2SkillComponent* Skills = State ? State->GetSkillComponent() : nullptr;
			return FMT2QuestValue(Skills ? Skills->GetSkillGroup() : 0);
		}
		if (Name == TEXT("pc.get_skill_level"))
		{
			const UMT2SkillComponent* Skills = State ? State->GetSkillComponent() : nullptr;
			return FMT2QuestValue(Skills ? Skills->GetSkillLevel(Argument(0).AsInt()) : 0);
		}
		if (Name == TEXT("pc.has_master_skill"))
		{
			const UMT2SkillComponent* Skills = State ? State->GetSkillComponent() : nullptr;
			if (Skills)
			{
				// questlua_pc.cpp scans [0, SKILL_MAX_NUM=255) and excludes M1 (level 20).
				for (const FMT2SkillLevelEntry& Skill : Skills->GetSkillLevels())
				{
					if (Skill.SkillVnum >= 0 && Skill.SkillVnum < 255 && Skill.Level >= 21 &&
						MT2SkillMastery::FromLevel(Skill.Level) >= EMT2SkillMastery::Master)
					{
						return FMT2QuestValue::Boolean(true);
					}
				}
			}
			return FMT2QuestValue::Boolean(false);
		}
		if (Name == TEXT("pc.get_exp"))
		{
			return FMT2QuestValue(static_cast<double>(State ? State->GetExperience() : 0));
		}
		if (Name == TEXT("pc.get_next_exp"))
		{
			return FMT2QuestValue(
				static_cast<double>(State ? State->GetRequiredExperienceForNextLevel() : 0));
		}
		if (Name == TEXT("pc.is_gm"))
		{
			return FMT2QuestValue(State && State->IsAdmin() ? 1.0 : 0.0);
		}
		// questlua_pc.cpp pc_get_gm_level returns the character's GM level; only "player" and "admin"
		// exist here, so it collapses to the same test is_gm makes.
		if (Name == TEXT("pc.get_gm_level"))
		{
			return FMT2QuestValue(State && State->IsAdmin() ? 1.0 : 0.0);
		}
		// questlib.lua line 118: a novice is a character that has not picked a skill group yet.
		if (Name == TEXT("pc_is_novice"))
		{
			const UMT2SkillComponent* Skills = State ? State->GetSkillComponent() : nullptr;
			return FMT2QuestValue(Skills && Skills->GetSkillGroup() == 0 ? 1.0 : 0.0);
		}
		// questlib.lua line 209: is the event's NPC one of the two skill teachers for the player's job?
		// The vnum lists live at the bottom of questlib.lua and are converted with the other tables.
		if (Name == TEXT("npc_is_same_job") || Name == TEXT("table_is_in"))
		{
			const AMT2Mob* EventMob = Cast<AMT2Mob>(Context.TargetActor);
			auto ListContains = [](const TCHAR* ListName, int32 Vnum)
			{
				const UMT2QuestTableAsset* Tables = GetQuestTables();
				const FMT2QuestTable* List = Tables ? Tables->FindTable(FName(ListName)) : nullptr;
				const FMT2QuestTableNode* Root = List ? List->GetNode(0) : nullptr;
				if (!Root)
				{
					return false;
				}
				for (const int32 ChildIndex : Root->Children)
				{
					const FMT2QuestTableNode* Child = List->GetNode(ChildIndex);
					if (Child && Child->bIsNumber && FMath::RoundToInt(Child->Number) == Vnum)
					{
						return true;
					}
				}
				return false;
			};

			if (Name == TEXT("table_is_in"))
			{
				const FMT2QuestValue List = Argument(0);
				const FMT2QuestValue Wanted = Argument(1);
				if (List.IsTableRef())
				{
					const int32 Length = List.GetTableLength();
					for (int32 Index = 1; Index <= Length; ++Index)
					{
						const FMT2QuestValue Child = List.GetTableValue(FMT2QuestValue(Index));
						if (SameLuaValue(Child, Wanted))
						{
							return FMT2QuestValue(1.0);
						}
					}
				}
				return FMT2QuestValue(0.0);
			}

			const int32 NpcVnum = EventMob ? EventMob->GetMobVnum() : 0;
			const int32 Job = State ? static_cast<int32>(State->GetCharacterAppearance().Race) : -1;
			static const TCHAR* const JobLists[4][2] = {
				{TEXT("WARRIOR1_NPC_LIST"), TEXT("WARRIOR2_NPC_LIST")},
				{TEXT("ASSASSIN1_NPC_LIST"), TEXT("ASSASSIN2_NPC_LIST")},
				{TEXT("SURA1_NPC_LIST"), TEXT("SURA2_NPC_LIST")},
				{TEXT("SHAMAN1_NPC_LIST"), TEXT("SHAMAN2_NPC_LIST")},
			};
			if (NpcVnum <= 0 || Job < 0 || Job > 3)
			{
				return FMT2QuestValue(0.0);
			}
			return FMT2QuestValue(
				ListContains(JobLists[Job][0], NpcVnum) || ListContains(JobLists[Job][1], NpcVnum)
					? 1.0 : 0.0);
		}

		// questlib.lua line 147: pc.get_empire() == npc.empire, for the NPC that raised the event.
		if (Name == TEXT("npc_is_same_empire"))
		{
			const AMT2Mob* EventMob = Cast<AMT2Mob>(Context.TargetActor);
			const int32 PlayerEmpire = State ? static_cast<int32>(State->GetEmpire()) : 0;
			return FMT2QuestValue(
				EventMob && static_cast<int32>(EventMob->GetEmpire()) == PlayerEmpire ? 1.0 : 0.0);
		}
		if (Name == TEXT("pc.is_dead"))
		{
			const UMT2HealthComponent* Health = Player ? Player->GetHealthComponent() : nullptr;
			// questlua_pc.cpp also considers a missing current character dead.
			return FMT2QuestValue(!Health || Health->IsDead() ? 1.0 : 0.0);
		}
		// Dungeons are not implemented, so a character is never inside one. Scripts branch on this to
		// decide whether a warp is leaving an instance; "no" is the correct answer here, not a guess.
		if (Name == TEXT("pc.in_dungeon"))
		{
			return FMT2QuestValue(0.0);
		}
		if (Name == TEXT("pc.get_hp") || Name == TEXT("pc.gethp") || Name == TEXT("pc.hp") ||
			Name == TEXT("pc.get_max_hp") || Name == TEXT("pc.getmaxhp") || Name == TEXT("pc.maxhp"))
		{
			const UMT2HealthComponent* Health = Player ? Player->GetHealthComponent() : nullptr;
			const bool bMaximum = Name == TEXT("pc.get_max_hp") ||
				Name == TEXT("pc.getmaxhp") || Name == TEXT("pc.maxhp");
			return FMT2QuestValue(Health ? (bMaximum ? Health->GetMaxHealth() : Health->GetHealth()) : 0.0);
		}
		if (Name == TEXT("pc.get_sp") || Name == TEXT("pc.getsp") || Name == TEXT("pc.sp") ||
			Name == TEXT("pc.get_max_sp") || Name == TEXT("pc.getmaxsp") || Name == TEXT("pc.maxsp"))
		{
			const UMT2ManaComponent* Mana = Player ? Player->GetManaComponent() : nullptr;
			const bool bMaximum = Name == TEXT("pc.get_max_sp") ||
				Name == TEXT("pc.getmaxsp") || Name == TEXT("pc.maxsp");
			return FMT2QuestValue(Mana ? (bMaximum ? Mana->GetMaxMana() : Mana->GetMana()) : 0.0);
		}
		if (Name == TEXT("pc.get_x") || Name == TEXT("pc.get_local_x"))
		{
			return FMT2QuestValue(Player ? Player->GetActorLocation().X : 0.0);
		}
		if (Name == TEXT("pc.get_y") || Name == TEXT("pc.get_local_y"))
		{
			return FMT2QuestValue(Player ? Player->GetActorLocation().Y : 0.0);
		}
		if (Name == TEXT("pc.get_map_index"))
		{
			if (Player)
			{
				for (TActorIterator<AMT2MapPresentationActor> It(Player->GetWorld()); It; ++It)
				{
					if (It->MapIndex > 0)
					{
						return FMT2QuestValue(static_cast<double>(It->MapIndex));
					}
					break;
				}
			}
			const AMT2GameStateBase* GameState =
				Player && Player->GetWorld() ? Player->GetWorld()->GetGameState<AMT2GameStateBase>() : nullptr;
			FString MapId = GameState ? GameState->GetMapId() : FString();
			if (MapId.IsEmpty() && Player)
			{
				for (TActorIterator<AMT2MapPresentationActor> It(Player->GetWorld()); It; ++It)
				{
					MapId = It->MapId;
					break;
				}
			}
			MapId = FPackageName::GetShortName(MapId);
			const UMT2QuestTableAsset* Tables = GetQuestTables();
			const FMT2QuestMapDefinition* Map = Tables ? Tables->FindMapById(MapId) : nullptr;
			return FMT2QuestValue(Map ? static_cast<double>(Map->MapIndex) : 0.0);
		}
		if (Name == TEXT("pc.get_change_empire_count"))
		{
			// This counter is not persisted by UnrealLongju yet. New characters have never changed empire.
			return FMT2QuestValue(0.0);
		}
		if (Name == TEXT("pc.get_special_ride_vnum") ||
			Name == TEXT("pc.get_special_ride_remaining"))
		{
			const UMT2MountComponent* Mounts = Player ? Player->GetMountComponent() : nullptr;
			const FMT2QuestValue Remaining(Mounts ? Mounts->GetCalledSpecialMountRemainingSeconds() : 0);
			return Name == TEXT("pc.get_special_ride_vnum")
				? FMT2QuestValue::Results({FMT2QuestValue(Mounts ? Mounts->GetCalledSpecialMountVnum() : 0), Remaining}) : Remaining;
		}
		// The guild reads spelled on pc.* rather than guild.*; they answer from the same record.
		if (Name == TEXT("pc.get_guild_id") || Name == TEXT("pc.get_guild") ||
			Name == TEXT("pc.hasguild") || Name == TEXT("pc.has_guild") || Name == TEXT("pc.isguildmaster") ||
			Name == TEXT("pc.is_guild_master"))
		{
			const int32 GuildId = State ? State->GetGuildId() : 0;
			if (Name == TEXT("pc.hasguild") || Name == TEXT("pc.has_guild"))
			{
				return FMT2QuestValue(GuildId != 0 ? 1.0 : 0.0);
			}
			if (Name == TEXT("pc.isguildmaster") || Name == TEXT("pc.is_guild_master"))
			{
				const UGameInstance* GameInstance = State ? State->GetGameInstance() : nullptr;
				UMT2GuildSubsystem* Guilds =
					GameInstance ? GameInstance->GetSubsystem<UMT2GuildSubsystem>() : nullptr;
				const UMT2PersistenceComponent* Persistence =
					State ? State->GetPersistenceComponent() : nullptr;
				const FString CharacterId = Persistence ? Persistence->GetEntityId() : FString();
				return FMT2QuestValue(
					Guilds && Guilds->IsGuildMaster(GuildId, CharacterId) ? 1.0 : 0.0);
			}
			return FMT2QuestValue(GuildId);
		}
		if (Name == TEXT("pc.get_vid") || Name == TEXT("pc.vid"))
		{
			UMT2QuestEntitySubsystem* Entities = Player && Player->GetWorld()
				? Player->GetWorld()->GetSubsystem<UMT2QuestEntitySubsystem>() : nullptr;
			return FMT2QuestValue(Entities ? Entities->GetEntityId(Context.Player) : 0);
		}
		if (Name == TEXT("pc.get_align") || Name == TEXT("pc.playtime") ||
			Name == TEXT("pc.get_player_id"))
		{
			return FMT2QuestValue(0.0);
		}

		// ---- game / world ----
		if (Name == TEXT("game.get_event_flag"))
		{
			// Event flags are server-global; stored alongside quest flags under a reserved scope.
			return FMT2QuestValue(Context.Manager
				? Context.Manager->GetQuestFlag(FName(*(FString(TEXT("game.")) + Argument(0).Text)), nullptr) : 0);
		}
		if (Name == TEXT("game.get_time"))
		{
			return FMT2QuestValue(Player && Player->GetWorld()
				? Player->GetWorld()->GetTimeSeconds() : 0.0);
		}
		if (Name == TEXT("find_npc_by_vnum"))
		{
			const int32 Wanted = Argument(0).AsInt();
			if (Player && Player->GetWorld() && Wanted > 0)
			{
				for (TActorIterator<AMT2Mob> It(Player->GetWorld()); It; ++It)
				{
					if (It->GetMobVnum() == Wanted && IsValid(*It) && !It->IsActorBeingDestroyed())
					{
						return FMT2QuestValue(Player->GetWorld()->GetSubsystem<UMT2QuestEntitySubsystem>()->GetEntityId(*It));
					}
				}
			}
			return FMT2QuestValue(0.0);
		}
		if (Name == TEXT("find_pc_by_name"))
		{
			if (Player && Player->GetWorld() && Argument(0).bIsText)
			{
				for (TActorIterator<AMT2PlayerCharacter> It(Player->GetWorld()); It; ++It)
				{
					const AMT2PlayerState* Candidate = It->GetPlayerState<AMT2PlayerState>();
					if (IsValid(Candidate) && Candidate->GetCharacterName().Equals(Argument(0).Text, ESearchCase::CaseSensitive))
					{
						return FMT2QuestValue(Player->GetWorld()->GetSubsystem<UMT2QuestEntitySubsystem>()->GetEntityId(*It));
					}
				}
			}
			return FMT2QuestValue(0.0);
		}
		if (Name == TEXT("mob_name") || Name == TEXT("mob2_name") || Name == TEXT("item_name"))
		{
			// Resolve the display name from the vnum registry so quest text like "Kill %s!" names the
			// actual monster instead of coming out blank.
			const int32 Vnum = Argument(0).AsInt();
			const UGameInstance* GameInstance = State ? State->GetGameInstance() : nullptr;
			UMT2VnumRegistrySubsystem* Registry =
				GameInstance ? GameInstance->GetSubsystem<UMT2VnumRegistrySubsystem>() : nullptr;
			if (!Registry || Vnum <= 0)
			{
				return FMT2QuestValue(FString());
			}
			if (Name == TEXT("item_name"))
			{
				const UMT2ItemTemplate* Item = Registry->ResolveItemTemplateClass(Vnum).GetDefaultObject();
				return FMT2QuestValue(Item ? Item->DisplayName.ToString() : FString());
			}
			const AMT2Mob* Mob = Registry->ResolveMobClass(Vnum).GetDefaultObject();
			return FMT2QuestValue(Mob ? Mob->GetMobDisplayName() : FString());
		}
		if (Name == TEXT("get_server_timer_arg"))
		{
			// The value handed to server_timer(name, seconds, arg), readable inside its handler.
			return Context.Manager ? Context.Manager->GetActiveTimerArgument() : FMT2QuestValue();
		}

		if (Name == TEXT("type"))
		{
			// Lua distinguishes an absent argument (error) from an explicit nil value.
			if (Arguments.IsEmpty()) { bOutOk = false; return FMT2QuestValue(); }
			const FMT2QuestValue& Value = Arguments[0];
			return FMT2QuestValue(FString(Value.bIsNil ? TEXT("nil") : Value.IsTableRef() ? TEXT("table") :
				Value.bIsBoolean ? TEXT("boolean") : Value.bIsText ? TEXT("string") : TEXT("number")));
		}

		// ---- math ----
		if (Name == TEXT("math.random") || Name == TEXT("number"))
		{
			if (Arguments.Num() >= 2)
			{
				return FMT2QuestValue(FMath::RandRange(Argument(0).AsInt(), Argument(1).AsInt()));
			}
			if (Arguments.Num() == 1)
			{
				return FMT2QuestValue(FMath::RandRange(1, FMath::Max(Argument(0).AsInt(), 1)));
			}
			return FMT2QuestValue(FMath::FRand());
		}
		if (Name == TEXT("math.floor")) { return FMT2QuestValue(FMath::FloorToDouble(Argument(0).Number)); }
		if (Name == TEXT("math.ceil"))  { return FMT2QuestValue(FMath::CeilToDouble(Argument(0).Number)); }
		if (Name == TEXT("math.abs"))   { return FMT2QuestValue(FMath::Abs(Argument(0).Number)); }
		if (Name == TEXT("math.min"))   { return FMT2QuestValue(FMath::Min(Argument(0).Number, Argument(1).Number)); }
		if (Name == TEXT("math.max"))   { return FMT2QuestValue(FMath::Max(Argument(0).Number, Argument(1).Number)); }
		if (Name == TEXT("time_min_to_sec"))  { return FMT2QuestValue(Argument(0).Number * 60.0); }
		if (Name == TEXT("time_hour_to_sec")) { return FMT2QuestValue(Argument(0).Number * 3600.0); }
		if (Name == TEXT("time_day_to_sec"))  { return FMT2QuestValue(Argument(0).Number * 86400.0); }

		// ---- horse ----
		if (Name == TEXT("horse.get_level") || Name == TEXT("horse.get_grade") ||
			Name == TEXT("horse.is_summon") || Name == TEXT("horse.is_dead") ||
			Name == TEXT("horse.is_ride"))
		{
			return FMT2QuestValue(0.0);
		}

		if (Name == TEXT("math.mod"))
		{
			const double Divisor = Argument(1).Number;
			return FMT2QuestValue(Divisor != 0.0 ? FMath::Fmod(Argument(0).Number, Divisor) : 0.0);
		}
		if (Name == TEXT("tonumber"))
		{
			const FMT2QuestValue Value = Argument(0);
			double Number = 0.0;
			return TryNumericValue(Value, Number) ? FMT2QuestValue(Number) : FMT2QuestValue();
		}
		// table.getn on a converted Lua table: how many entries it holds.
		if (Name == TEXT("table.getn"))
		{
			const FMT2QuestValue Value = Argument(0);
			return FMT2QuestValue(Value.GetTableLength());
		}
		if (Name == TEXT("affect.get_apply_on"))
		{
			// Imported effects use the normalized apply channel directly, so no legacy
			// EApplyTypes-to-EPointTypes translation is required at runtime.
			return Argument(0);
		}

		// ---- string ----
		if (Name == TEXT("string.find"))
		{
			FString Source, Pattern;
			if (!TryLuaString(Argument(0), Source) || !TryLuaString(Argument(1), Pattern)) { bOutOk = false; return FMT2QuestValue(); }
			int32 Initial = 1;
			if (!Argument(2).bIsNil)
			{
				double Number = 0;
				if (!TryNumericValue(Argument(2), Number) || !FMath::IsFinite(Number)) { bOutOk = false; return FMT2QuestValue(); }
				Number = FMath::TruncToDouble(Number);
				if (Number < MIN_int32 || Number > MAX_int32) { bOutOk = false; return FMT2QuestValue(); }
				Initial = static_cast<int32>(Number);
			}
			bool bFound = false; int32 Start = 0, End = 0; FString Error;
			TArray<MT2QuestLuaPattern::FCaptureValue> Captures;
			if (!MT2QuestLuaPattern::Find(Source, Pattern, Initial, Argument(3).AsBool(), bFound, Start, End, Captures, Error))
			{
				bOutOk = false; return FMT2QuestValue();
			}
			if (!bFound) { return FMT2QuestValue(); }
			TArray<FMT2QuestValue> Values = {FMT2QuestValue(Start), FMT2QuestValue(End)};
			for (const auto& Capture : Captures) { Values.Add(Capture.bPosition ? FMT2QuestValue(Capture.Position) : FMT2QuestValue(Capture.Text)); }
			return FMT2QuestValue::Results(MoveTemp(Values));
		}
		if (Name == TEXT("string.gsub"))
		{
			FString Source, Pattern, Replacement;
			if (!TryLuaString(Argument(0), Source) || !TryLuaString(Argument(1), Pattern) ||
				!TryLuaString(Argument(2), Replacement)) { bOutOk = false; return FMT2QuestValue(); }
			const int32 ByteLength = FTCHARToUTF8(*Source, Source.Len()).Length();
			int32 Maximum = ByteLength == MAX_int32 ? MAX_int32 : ByteLength + 1;
			if (!Argument(3).bIsNil)
			{
				double Value = 0;
				if (!TryNumericValue(Argument(3), Value) || !FMath::IsFinite(Value) ||
					Value < MIN_int32 || Value > MAX_int32) { bOutOk = false; return FMT2QuestValue(); }
				Maximum = static_cast<int32>(Value);
			}
			FString Text, Error; int32 Count = 0;
			if (!MT2QuestLuaPattern::Substitute(Source, Pattern, Replacement, Maximum, Text, Count, Error))
			{
				bOutOk = false; return FMT2QuestValue();
			}
			return FMT2QuestValue::Results({FMT2QuestValue(Text), FMT2QuestValue(Count)});
		}
		if (Name == TEXT("string.format"))
		{
			FString Text;
			if (!FormatLuaString(Arguments, Text)) { bOutOk = false; return FMT2QuestValue(); }
			return FMT2QuestValue(Text);
		}
		if (Name == TEXT("get_quest_state"))
		{
			const FString Key = Argument(0).Text + TEXT(".__status");
			return FMT2QuestValue(Context.Manager
				? Context.Manager->GetQuestFlag(FName(*Key), nullptr) : 0);
		}
		if (Name == TEXT("string.len") || Name == TEXT("string.lower") || Name == TEXT("string.upper"))
		{
			FString Text;
			if (!TryLuaString(Argument(0), Text)) { bOutOk = false; return FMT2QuestValue(); }
			if (Name == TEXT("string.len")) { return FMT2QuestValue(FTCHARToUTF8(*Text, Text.Len()).Length()); }
			// Legacy Lua uses byte-wise C-locale case conversion, not Unicode case folding.
			const bool bLower = Name == TEXT("string.lower");
			for (TCHAR& Character : Text)
			{
				if (bLower && Character >= TEXT('A') && Character <= TEXT('Z')) { Character += TEXT('a') - TEXT('A'); }
				else if (!bLower && Character >= TEXT('a') && Character <= TEXT('z')) { Character -= TEXT('a') - TEXT('A'); }
			}
			return FMT2QuestValue(Text);
		}
		if (Name == TEXT("tostring"))
		{
			const FMT2QuestValue Value = Argument(0);
			if (Value.bIsNil) { return FMT2QuestValue(TEXT("nil")); }
			if (Value.bIsBoolean) { return FMT2QuestValue(Value.AsBool() ? TEXT("true") : TEXT("false")); }
			return FMT2QuestValue(Value.bIsText ? Value.Text : FString::Printf(TEXT("%.14g"), Value.Number));
		}

		// ---- environment predicates ----
		if (Name == TEXT("is_test_server"))
		{
			return FMT2QuestValue(0.0); // live behaviour: the scripts' test-only branches stay off
		}
		if (Name == TEXT("get_global_time") || Name == TEXT("get_time"))
		{
			// Both names bind to _get_global_time in the legacy server. Quest flags persist across
			// map travel/restarts, so world uptime cannot be used for their expiry timestamps.
			return FMT2QuestValue(static_cast<double>(FDateTime::UtcNow().ToUnixTimestamp()));
		}
		if (Name == TEXT("next_time_is_now"))
		{
			if (!Context.Manager)
			{
				bOutOk = false;
				return FMT2QuestValue();
			}
			const int32 NextTime = Context.Manager->GetQuestFlag(TEXT("__NEXT_TIME__"), Context.Quest);
			return FMT2QuestValue(FDateTime::UtcNow().ToUnixTimestamp() >= NextTime ? 1.0 : 0.0);
		}
		if (Name == TEXT("get_channel_id"))
		{
			const AMT2GameStateBase* GameState =
				Player && Player->GetWorld() ? Player->GetWorld()->GetGameState<AMT2GameStateBase>() : nullptr;
			return FMT2QuestValue(GameState ? GameState->GetServerChannel() : 1);
		}
		if (Name == TEXT("pc_find_skill_teacher_vid"))
		{
			return FMT2QuestValue(0.0);
		}
		if (Name == TEXT("pc.is_clear_skill_group"))
		{
			const UMT2SkillComponent* Skills = State ? State->GetSkillComponent() : nullptr;
			return FMT2QuestValue(Skills && Skills->GetSkillGroup() == 0 ? 1.0 : 0.0);
		}
		if (Name == TEXT("pc.get_skill_group_name"))
		{
			return FMT2QuestValue(FString());
		}
		if (Name == TEXT("pc.count_item_by_vnum"))
		{
			return FMT2QuestValue(Inventory ? Inventory->CountItemByVnum(Argument(0).AsInt()) : 0);
		}
		// Features that do not exist yet (mount/marriage/guild/party/item-socket reads). They return a
		// neutral 0/false so the surrounding quest still runs its normal path instead of stalling.
		if (Name == TEXT("pc.is_riding") || Name == TEXT("pc.is_mount"))
		{
			const UMT2MountComponent* Mounts = Player ? Player->GetMountComponent() : nullptr;
			return FMT2QuestValue(Mounts && Mounts->IsMounted() ? 1.0 : 0.0);
		}
		if (Name == TEXT("party.is_party"))
		{
			return FMT2QuestValue(State && State->GetParty() ? 1.0 : 0.0);
		}
		if (Name == TEXT("party.is_leader"))
		{
			const AMT2Party* Party = State ? State->GetParty() : nullptr;
			return FMT2QuestValue(Party && Party->IsLeader(State) ? 1.0 : 0.0);
		}
		if (Name == TEXT("party.get_member_count"))
		{
			const AMT2Party* Party = State ? State->GetParty() : nullptr;
			return FMT2QuestValue(Party ? Party->GetMemberCount() : 0);
		}
		if (Name == TEXT("party.getf") || Name == TEXT("party.setf"))
		{
			const bool bWrite = Name == TEXT("party.setf");
			AMT2Party* Party = State ? State->GetParty() : nullptr;
			const FMT2QuestValue Key = Argument(0);
			// lua_isstring also accepts numbers; booleans, nil and tables are not flag names.
			if (!Party || Key.bIsNil || Key.bIsBoolean || Key.IsTableRef())
			{
				return bWrite ? FMT2QuestValue() : FMT2QuestValue(0.0);
			}
			const FString FlagName = Key.bIsText ? Key.Text : FString::Printf(TEXT("%.14g"), Key.Number);
			if (!bWrite) { return FMT2QuestValue(Party->GetScriptFlag(FlagName)); }
			if (!State->HasAuthority() || !Party->HasAuthority() || State->GetWorld() != Party->GetWorld())
			{
				bOutOk = false; return FMT2QuestValue();
			}
			double Value = 0;
			if (!TryNumericValue(Argument(1), Value)) { return FMT2QuestValue(); }
			if (!FMath::IsFinite(Value) || Value < MIN_int32 || Value > MAX_int32)
			{
				bOutOk = false; return FMT2QuestValue();
			}
			Party->SetScriptFlag(FlagName, static_cast<int32>(Value));
			return FMT2QuestValue();
		}
		if (Name == TEXT("pc.is_polymorphed") || Name == TEXT("pc.is_engaged") ||
			Name == TEXT("pc.is_married") ||
			Name == TEXT("pc.get_wear") || Name == TEXT("pc.get_part"))
		{
			return FMT2QuestValue(0.0);
		}

		// ---- the NPC/mob this event came from ----
		if (Name.StartsWith(TEXT("npc.")))
		{
			if (Name == TEXT("npc.get_vid"))
			{
				AMT2CharacterBase* Character = Cast<AMT2CharacterBase>(Context.TargetActor);
				UMT2QuestEntitySubsystem* Entities = Player && Player->GetWorld()
					? Player->GetWorld()->GetSubsystem<UMT2QuestEntitySubsystem>() : nullptr;
				return FMT2QuestValue(Entities ? Entities->GetEntityId(Character) : 0);
			}
			if (Name == TEXT("npc.is_near") || Name == TEXT("npc.is_near_vid"))
			{
				const AMT2CharacterBase* Npc = Cast<AMT2CharacterBase>(Context.TargetActor);
				const AMT2CharacterBase* Other = Player;
				int32 RangeIndex = 0;
				if (Name == TEXT("npc.is_near_vid"))
				{
					RangeIndex = 1;
					double Id = 0;
					if (!TryNumericValue(Argument(0), Id) || !FMath::IsFinite(Id) || Id < 1 || Id > MAX_int32)
					{
						return FMT2QuestValue::Boolean(false);
					}
					UMT2QuestEntitySubsystem* Entities = Player && Player->GetWorld()
						? Player->GetWorld()->GetSubsystem<UMT2QuestEntitySubsystem>() : nullptr;
					Other = Entities ? Entities->FindEntity(static_cast<int32>(Id)) : nullptr;
				}
				double Range = 10;
				double Supplied = 0;
				if (TryNumericValue(Argument(RangeIndex), Supplied)) { Range = Supplied; }
				if (!IsValid(Npc) || !IsValid(Other) || Npc->IsActorBeingDestroyed() || Other->IsActorBeingDestroyed() ||
					Npc->GetWorld() != Other->GetWorld() || !FMath::IsFinite(Range)) { return FMT2QuestValue::Boolean(false); }
				// Legacy DISTANCE_APPROX uses integer XY differences, with strict '<', and ignores Z.
				const FVector A = Npc->GetActorLocation(), B = Other->GetActorLocation();
				const double DX = FMath::Abs(FMath::TruncToDouble(A.X) - FMath::TruncToDouble(B.X));
				const double DY = FMath::Abs(FMath::TruncToDouble(A.Y) - FMath::TruncToDouble(B.Y));
				const double Distance = FMath::FloorToDouble((246 * FMath::Max(DX, DY) + 102 * FMath::Min(DX, DY)) / 256);
				return FMT2QuestValue::Boolean(Distance < Range * 100);
			}
			if (Name == TEXT("npc.purge"))
			{
				bOutOk = Context.Manager && Context.Manager->PurgeQuestNpc(Context);
				return FMT2QuestValue();
			}
			// Conversation ownership is shared by all players on this NPC, not a per-player flag.
			if (Name == TEXT("npc.lock") || Name == TEXT("npc.unlock"))
			{
				if (!Context.Manager) { bOutOk = false; return FMT2QuestValue(); }
				if (Name == TEXT("npc.lock")) { return FMT2QuestValue::Boolean(Context.Manager->TryLockQuestNpc(Context)); }
				bOutOk = Context.Manager->UnlockQuestNpc(Context);
				return FMT2QuestValue();
			}
			const AMT2Mob* Mob = Cast<AMT2Mob>(Context.TargetActor);
			// npc.is_pc() asks whether the "other side" is a player, which matters for duel/PvP scripts.
			if (Name == TEXT("npc.is_pc"))
			{
				return FMT2QuestValue(Cast<AMT2PlayerCharacter>(Context.TargetActor) ? 1.0 : 0.0);
			}
			if (!Mob)
			{
				return FMT2QuestValue(0.0);
			}
			if (Name == TEXT("npc.get_vnum") || Name == TEXT("npc.vnum") ||
				Name == TEXT("npc.get_race") || Name == TEXT("npc.race"))
			{
				// The old scripts use "race" for a mob's vnum.
				return FMT2QuestValue(Mob->GetMobVnum());
			}
			if (Name == TEXT("npc.get_level") || Name == TEXT("npc.level"))
			{
				return FMT2QuestValue(Mob->GetMobLevel());
			}
			if (Name == TEXT("npc.get_guild"))
			{
				// Mobs and NPCs are never in a guild; only guild-war scripts read this.
				return FMT2QuestValue(0.0);
			}
			if (Name == TEXT("npc.get_empire") || Name == TEXT("npc.empire"))
			{
				return FMT2QuestValue(static_cast<int32>(Mob->GetEmpire()));
			}
			return FMT2QuestValue(0.0);
		}
		if (Name == TEXT("is_destination_village"))
		{
			// GFquestlib.lua returns an empty string for a matching map, otherwise nil. It
			// does not compare empire ids, and the empty string is true in Lua.
			const int32 Map = CallFunctionValue(TEXT("pc.get_map_index"), {}, Context, bOutOk).AsInt();
			const int32 Group = Argument(0).AsInt();
			const bool bFirst = Map == 1 || Map == 21 || Map == 41;
			const bool bSecond = Map == 3 || Map == 23 || Map == 43;
			const bool bMatch = Group == 1 && bFirst || Group == 2 && bSecond ||
				Group == 3 && (bFirst || bSecond) || Group == 65 && Map == 65;
			return bMatch ? FMT2QuestValue(FString()) : FMT2QuestValue();
		}

		// next_refine_vnum is a proto lookup, not a read of the current event item.
		if (Name == TEXT("item.next_refine_vnum"))
		{
			double Number = 0;
			const int32 Vnum = TryNumericValue(Argument(0), Number) && FMath::IsFinite(Number) &&
				Number > 0 && Number <= MAX_int32 ? FMath::TruncToInt(Number) : 0;
			UGameInstance* GameInstance = Context.PlayerState ? Context.PlayerState->GetGameInstance() : nullptr;
			UMT2VnumRegistrySubsystem* Registry = GameInstance ? GameInstance->GetSubsystem<UMT2VnumRegistrySubsystem>() : nullptr;
			const UMT2ItemTemplate* Item = Registry ? Registry->ResolveItemTemplateClass(Vnum).GetDefaultObject() : nullptr;
			if (!Item)
			{
				UE_LOG(LogTemp, Error, TEXT("Quest item.next_refine_vnum cannot find item proto %d."), Vnum);
			}
			return FMT2QuestValue(Item ? Item->RefinedVnum : 0);
		}

		// ---- the item that raised this event (item.* reads) ----
		if (Name.StartsWith(TEXT("item.")))
		{
			const TArray<FMT2ItemSlot>* Slots = Inventory ? &Inventory->GetSlots() : nullptr;
			const FMT2ItemSlot* Slot = Slots && Slots->IsValidIndex(Context.EventItemSlot)
				? &(*Slots)[Context.EventItemSlot] : nullptr;
			if (!Slot || Slot->IsEmpty())
			{
				if (Name == TEXT("item.get_level_limit")) { return FMT2QuestValue(); }
				return FMT2QuestValue(0.0); // no item in context (e.g. read outside an item event)
			}
			if (Name == TEXT("item.get_vnum") || Name == TEXT("item.vnum"))
			{
				return FMT2QuestValue(Slot->Vnum);
			}
			if (Name == TEXT("item.get_count") || Name == TEXT("item.count"))
			{
				return FMT2QuestValue(Slot->Count);
			}
			if (Name == TEXT("item.get_cell"))
			{
				return FMT2QuestValue(Context.EventItemSlot);
			}
			const UMT2ItemTemplate* Template = Inventory
				? Inventory->GetTemplateAtSlot(Context.EventItemSlot) : nullptr;
			if (Name == TEXT("item.get_level_limit"))
			{
				// The legacy binding ignores its arguments and returns nil for non-weapon/armor items.
				if (!Template || (!Template->IsA<UMT2ItemWeaponTemplate>() && !Template->IsA<UMT2ItemArmorTemplate>()))
				{
					return FMT2QuestValue();
				}
				const FMT2ItemLimit* Limit = Template->Limits.FindByPredicate(
					[](const FMT2ItemLimit& Entry) { return Entry.Type == EMT2ItemLimitType::Level; });
				return FMT2QuestValue(Limit ? Limit->Value : 0);
			}
			if (Name == TEXT("item.get_refine_vnum"))
			{
				return FMT2QuestValue(Template ? Template->RefinedVnum : 0);
			}
			if (Name == TEXT("item.get_value"))
			{
				return FMT2QuestValue(Template ? Template->GetLegacyQuestValue(Argument(0).AsInt()) : 0);
			}
			if (Name == TEXT("item.get_level"))
			{
				return FMT2QuestValue(Template ? Template->RefinementLevel : 0);
			}
			if (Name == TEXT("item.get_type"))
			{
				int32 Type = 0;
				if (Template)
				{
					if (Template->IsA<UMT2ItemWeaponTemplate>()) Type = 1;
					else if (Template->IsA<UMT2ItemArmorTemplate>()) Type = 2;
					else if (Template->IsA<UMT2ItemUseTemplate>()) Type = 3;
					else if (Template->IsA<UMT2ItemAutoUseTemplate>()) Type = 4;
					else if (Template->IsA<UMT2ItemMaterialTemplate>()) Type = 5;
					else if (Template->IsA<UMT2ItemMetinStoneTemplate>()) Type = 10;
					else if (Template->IsA<UMT2ItemRodTemplate>()) Type = 13;
					else if (Template->IsA<UMT2ItemPickTemplate>()) Type = 24;
					else if (Template->IsA<UMT2ItemCostumeTemplate>()) Type = 28;
				}
				return FMT2QuestValue(Type);
			}
			if (Name == TEXT("item.get_sub_type"))
			{
				int32 SubType = 0;
				if (const UMT2ItemWeaponTemplate* Weapon = Cast<UMT2ItemWeaponTemplate>(Template)) SubType = Weapon->WeaponSubType;
				else if (const UMT2ItemArmorTemplate* Armor = Cast<UMT2ItemArmorTemplate>(Template)) SubType = Armor->ArmorSubType;
				else if (const UMT2ItemUseTemplate* Use = Cast<UMT2ItemUseTemplate>(Template)) SubType = Use->UseSubType;
				else if (const UMT2ItemAutoUseTemplate* AutoUse = Cast<UMT2ItemAutoUseTemplate>(Template)) SubType = AutoUse->AutoUseSubType;
				else if (const UMT2ItemMaterialTemplate* Material = Cast<UMT2ItemMaterialTemplate>(Template)) SubType = Material->MaterialSubType;
				else if (const UMT2ItemMetinStoneTemplate* Stone = Cast<UMT2ItemMetinStoneTemplate>(Template)) SubType = Stone->MetinSubType;
				else if (const UMT2ItemCostumeTemplate* Costume = Cast<UMT2ItemCostumeTemplate>(Template)) SubType = Costume->CostumeSubType;
				return FMT2QuestValue(SubType);
			}
			if (Name == TEXT("item.get_socket"))
			{
				// Old sockets are numeric; ours hold a metin-stone template, so report the stone's vnum
				// (0 for an open socket) which is what the scripts compare against.
				const int32 SocketIndex = Argument(0).AsInt();
				if (!Slot->MetinSockets.IsValidIndex(SocketIndex))
				{
					return FMT2QuestValue(0.0);
				}
				const FMT2MetinSocket& Socket = Slot->MetinSockets[SocketIndex];
				const UMT2ItemMetinStoneTemplate* Stone = Socket.Stone.GetDefaultObject();
				// A mounted stone reports its vnum; otherwise the socket's scripted numeric value.
				return FMT2QuestValue(Stone ? Stone->Vnum : Socket.Value);
			}
			return FMT2QuestValue(0.0);
		}

		// ---- guild ----
		if (Name.StartsWith(TEXT("guild.")))
		{
			const UGameInstance* GameInstance = State ? State->GetGameInstance() : nullptr;
			UMT2GuildSubsystem* Guilds =
				GameInstance ? GameInstance->GetSubsystem<UMT2GuildSubsystem>() : nullptr;
			const int32 GuildId = State ? State->GetGuildId() : 0;
			// Membership is keyed by persistent character id, so a guild survives its members logging out.
			const UMT2PersistenceComponent* Persistence =
				State ? State->GetPersistenceComponent() : nullptr;
			const FString CharacterId = Persistence ? Persistence->GetEntityId() : FString();

			if (Name == TEXT("guild.get_id"))
			{
				return FMT2QuestValue(GuildId);
			}
			if (Name == TEXT("guild.get_level") || Name == TEXT("guild.level"))
			{
				return FMT2QuestValue(Guilds ? Guilds->GetGuildLevel(GuildId) : 0);
			}
			if (Name == TEXT("guild.get_name") || Name == TEXT("guild.name"))
			{
				// Fall back to the name mirrored on the PlayerState when the record isn't local.
				const FString GuildName = Guilds ? Guilds->GetGuildName(GuildId) : FString();
				return FMT2QuestValue(GuildName.IsEmpty() && State ? State->GetGuildName() : GuildName);
			}
			if (Name == TEXT("guild.is_guild_master"))
			{
				return FMT2QuestValue(
					Guilds && Guilds->IsGuildMaster(GuildId, CharacterId) ? 1.0 : 0.0);
			}
			if (Name == TEXT("guild.get_rank"))
			{
				return FMT2QuestValue(Guilds
					? static_cast<int32>(Guilds->GetMemberRank(GuildId, CharacterId)) : 0);
			}
			if (Name == TEXT("guild.get_member_count") || Name == TEXT("guild.get_ladder_point"))
			{
				FMT2Guild Guild;
				if (Guilds && Guilds->GetGuild(GuildId, Guild))
				{
					return FMT2QuestValue(Name == TEXT("guild.get_member_count")
						? Guild.Members.Num() : Guild.LadderPoints);
				}
				return FMT2QuestValue(0.0);
			}
			return FMT2QuestValue(0.0);
		}

		bOutOk = false;
		return FMT2QuestValue();
	}

	FMT2QuestValue CallFunction(const FString& Name, const TArray<FMT2QuestValue>& Arguments,
		const FMT2QuestContext& Context, bool& bOutOk)
	{
		FMT2QuestValue Value = CallFunctionValue(Name, Arguments, Context, bOutOk);
		if (bOutOk && (Name == TEXT("pc.change_alignment") || Name == TEXT("pc.changealignment") ||
			Name == TEXT("pc.remove_skill_book_no_delay"))) { return FMT2QuestValue::Results({}); }
		// Verified lua_pushboolean bindings and boolean-returning questlib helpers. Numeric
		// getters (including GM level and guild id) must remain numbers even when they return zero.
		static const TSet<FString> BooleanFunctions = {
			TEXT("pc.is_gm"), TEXT("pc_is_novice"), TEXT("npc_is_same_job"), TEXT("table_is_in"),
			TEXT("npc_is_same_empire"), TEXT("pc.is_dead"), TEXT("pc.in_dungeon"),
			TEXT("pc.hasguild"), TEXT("pc.has_guild"), TEXT("pc.isguildmaster"), TEXT("pc.is_guild_master"),
			TEXT("is_test_server"), TEXT("next_time_is_now"), TEXT("pc.is_clear_skill_group"),
			TEXT("pc.is_riding"), TEXT("pc.is_mount"), TEXT("party.is_party"), TEXT("party.is_leader"),
			TEXT("pc.is_polymorphed"), TEXT("pc.is_engaged"), TEXT("pc.is_married"),
			TEXT("npc.lock"), TEXT("npc.is_pc"), TEXT("npc.is_near"), TEXT("guild.is_guild_master"),
			TEXT("horse.is_summon"), TEXT("horse.is_dead")
		};
		return bOutOk && BooleanFunctions.Contains(Name) ? FMT2QuestValue::Boolean(Value.Number != 0.0) : Value;
	}

	// The converted Lua tables, loaded once. Read-only shared data, so a single cached pointer serves
	// every player and expression.
	const UMT2QuestTableAsset* GetQuestTables()
	{
		static TWeakObjectPtr<const UMT2QuestTableAsset> CachedAsset;
		static bool bTriedLoad = false;
		if (CachedAsset.IsValid())
		{
			return CachedAsset.Get();
		}
		if (bTriedLoad && !CachedAsset.IsValid())
		{
			// Retry after a GC/level change; the load is cheap and only happens when a table is used.
			bTriedLoad = false;
		}
		bTriedLoad = true;
		const UMT2QuestTableAsset* Asset = LoadObject<UMT2QuestTableAsset>(
			nullptr, UMT2QuestTableAsset::GetAssetPath());
		CachedAsset = Asset;
		return Asset;
	}

	// Converts a table node into the value the scripts expect: leaves become their number/string, and a
	// nested table stays a reference so it can be indexed again.
	FMT2QuestValue MakeTableValue(const FMT2QuestTable* Table, int32 NodeIndex,
		TSharedPtr<const FMT2QuestTable> Owner = nullptr)
	{
		const FMT2QuestTableNode* Node = Table ? Table->GetNode(NodeIndex) : nullptr;
		if (!Node)
		{
			return FMT2QuestValue();
		}
		if (Node->bIsTable)
		{
			FMT2QuestValue Value(Table, NodeIndex);
			Value.OwnedTable = MoveTemp(Owner);
			return Value;
		}
		if (Node->bIsNil) { return FMT2QuestValue(); }
		if (Node->bIsBoolean) { return FMT2QuestValue::Boolean(Node->Number != 0.0); }
		return Node->bIsNumber ? FMT2QuestValue(Node->Number) : FMT2QuestValue(Node->Text);
	}

	// ---------------------------------------------------------------------------------------------
	// Recursive-descent parser/evaluator
	// ---------------------------------------------------------------------------------------------
	struct FParser
	{
		const TArray<FToken>& Tokens;
		const FMT2QuestContext& Context;
		int32 Index = 0;
		bool bOk = true;
		bool bValidateOnly = false;
		int32 ConstructorDepth = 0;

		const FToken& Peek() const { return Tokens[FMath::Min(Index, Tokens.Num() - 1)]; }
		bool IsOperator(const TCHAR* Text) const
		{
			return Peek().Kind == ETokenKind::Operator && Peek().Text == Text;
		}
		bool IsKeyword(const TCHAR* Text) const
		{
			return Peek().Kind == ETokenKind::Identifier && Peek().Text == Text;
		}

		FMT2QuestValue ParseOr()
		{
			FMT2QuestValue Left = ParseAnd();
			while (IsKeyword(TEXT("or")))
			{
				++Index;
				const bool bSkip = !bValidateOnly && Left.AsBool();
				FMT2QuestValue Right;
				{
					TGuardValue<bool> SkipGuard(bValidateOnly, bValidateOnly || bSkip);
					Right = ParseAnd();
				}
				if (!bSkip) { Left = Right; }
				Left = Left.Scalar();
			}
			return Left;
		}

		FMT2QuestValue ParseAnd()
		{
			FMT2QuestValue Left = ParseComparison();
			while (IsKeyword(TEXT("and")))
			{
				++Index;
				const bool bSkip = !bValidateOnly && !Left.AsBool();
				FMT2QuestValue Right;
				{
					TGuardValue<bool> SkipGuard(bValidateOnly, bValidateOnly || bSkip);
					Right = ParseComparison();
				}
				if (!bSkip) { Left = Right; }
				Left = Left.Scalar();
			}
			return Left;
		}

		FMT2QuestValue ParseComparison()
		{
			FMT2QuestValue Left = ParseConcatenation();
			while (Peek().Kind == ETokenKind::Operator)
			{
				const FString Operator = Peek().Text;
				if (Operator != TEXT("==") && Operator != TEXT("~=") && Operator != TEXT("!=") &&
					Operator != TEXT("<") && Operator != TEXT("<=") &&
					Operator != TEXT(">") && Operator != TEXT(">="))
				{
					break;
				}
				++Index;
				const FMT2QuestValue Right = ParseConcatenation();
				if (bValidateOnly) { Left = FMT2QuestValue(0.0); continue; }
				bool Result = false;
				if (Left.bIsNil || Right.bIsNil || Left.IsTableRef() || Right.IsTableRef())
				{
					const bool bEqual = Left.bIsNil && Right.bIsNil ||
						(Left.RuntimeTable && Left.RuntimeTable == Right.RuntimeTable) ||
						(Left.Table && Left.Table == Right.Table && Left.TableNodeIndex == Right.TableNodeIndex);
					if (Operator == TEXT("==")) { Result = bEqual; }
					else if (Operator == TEXT("~=") || Operator == TEXT("!=")) { Result = !bEqual; }
					else { bOk = false; }
				}
				else if (Left.bIsBoolean || Right.bIsBoolean || Left.bIsText != Right.bIsText)
				{
					const bool bEqual = Left.bIsBoolean && Right.bIsBoolean && Left.Number == Right.Number;
					if (Operator == TEXT("==")) { Result = bEqual; }
					else if (Operator == TEXT("~=") || Operator == TEXT("!=")) { Result = !bEqual; }
					else { bOk = false; }
				}
				else if (Left.bIsText && Right.bIsText)
				{
					const int32 Order = Left.Text.Compare(Right.Text, ESearchCase::CaseSensitive);
					if (Operator == TEXT("==")) { Result = Order == 0; }
					else if (Operator == TEXT("~=") || Operator == TEXT("!=")) { Result = Order != 0; }
					else if (Operator == TEXT("<")) { Result = Order < 0; }
					else if (Operator == TEXT("<=")) { Result = Order <= 0; }
					else if (Operator == TEXT(">")) { Result = Order > 0; }
					else if (Operator == TEXT(">=")) { Result = Order >= 0; }
				}
				else if (Operator == TEXT("=="))      { Result = Left.Number == Right.Number; }
				else if (Operator == TEXT("~=") || Operator == TEXT("!=")) { Result = Left.Number != Right.Number; }
				else if (Operator == TEXT("<"))       { Result = Left.Number < Right.Number; }
				else if (Operator == TEXT("<="))      { Result = Left.Number <= Right.Number; }
				else if (Operator == TEXT(">"))       { Result = Left.Number > Right.Number; }
				else if (Operator == TEXT(">="))      { Result = Left.Number >= Right.Number; }
				Left = FMT2QuestValue::Boolean(Result);
			}
			return Left;
		}

		FMT2QuestValue ParseConcatenation()
		{
			FMT2QuestValue Left = ParseAdditive();
			while (IsOperator(TEXT("..")))
			{
				++Index;
				const FMT2QuestValue Right = ParseAdditive();
				if (bValidateOnly) { Left = FMT2QuestValue(); continue; }
				if (Left.bIsNil || Right.bIsNil || Left.bIsBoolean || Right.bIsBoolean || Left.IsTableRef() || Right.IsTableRef())
				{
					bOk = false;
					return FMT2QuestValue();
				}
				const FString LeftText = Left.bIsText ? Left.Text : FString::Printf(TEXT("%.14g"), Left.Number);
				const FString RightText = Right.bIsText ? Right.Text : FString::Printf(TEXT("%.14g"), Right.Number);
				Left = FMT2QuestValue(LeftText + RightText);
			}
			return Left;
		}

		FMT2QuestValue ParseAdditive()
		{
			FMT2QuestValue Left = ParseMultiplicative();
			while (IsOperator(TEXT("+")) || IsOperator(TEXT("-")))
			{
				const FString Operator = Peek().Text;
				++Index;
				const FMT2QuestValue Right = ParseMultiplicative();
				if (bValidateOnly) { Left = FMT2QuestValue(); continue; }
				double LeftNumber = 0.0, RightNumber = 0.0;
				if (!TryNumericValue(Left, LeftNumber) || !TryNumericValue(Right, RightNumber))
				{
					bOk = false;
					return FMT2QuestValue();
				}
				Left = FMT2QuestValue(Operator == TEXT("+") ? LeftNumber + RightNumber : LeftNumber - RightNumber);
			}
			return Left;
		}

		FMT2QuestValue ParseMultiplicative()
		{
			FMT2QuestValue Left = ParseUnary();
			while (IsOperator(TEXT("*")) || IsOperator(TEXT("/")) || IsOperator(TEXT("%")))
			{
				const FString Operator = Peek().Text;
				++Index;
				const FMT2QuestValue Right = ParseUnary();
				if (bValidateOnly) { Left = FMT2QuestValue(); continue; }
				double LeftNumber = 0.0, RightNumber = 0.0;
				if (!TryNumericValue(Left, LeftNumber) || !TryNumericValue(Right, RightNumber))
				{
					bOk = false;
					return FMT2QuestValue();
				}
				if (Operator == TEXT("*")) { Left = FMT2QuestValue(LeftNumber * RightNumber); }
				else if (RightNumber == 0.0) { bOk = false; return FMT2QuestValue(); }
				else if (Operator == TEXT("/")) { Left = FMT2QuestValue(LeftNumber / RightNumber); }
				else { Left = FMT2QuestValue(LeftNumber - FMath::FloorToDouble(LeftNumber / RightNumber) * RightNumber); }
			}
			return Left;
		}

		// Applies any trailing [index] operators, so t[a][b] walks two levels down.
		FMT2QuestValue ParseIndexing(FMT2QuestValue Value)
		{
			while (IsOperator(TEXT("[")) || IsOperator(TEXT(".")))
			{
				const bool bNamed = IsOperator(TEXT("."));
				++Index;
				FMT2QuestValue IndexValue;
				if (bNamed)
				{
					if (Peek().Kind != ETokenKind::Identifier) { bOk = false; return FMT2QuestValue(); }
					IndexValue = FMT2QuestValue(Peek().Text);
					++Index;
				}
				else
				{
					IndexValue = ParseOr();
					if (IsOperator(TEXT("]"))) { ++Index; } else { bOk = false; }
				}
				if (bValidateOnly) { continue; }
				if (!Value.IsTableRef())
				{
					// Indexing something that is not a table: the script relies on data we do not have.
					bOk = false;
					return FMT2QuestValue();
				}
				if (bNamed)
				{
					TArray<FString> Fields;
					IndexValue.Text.ParseIntoArray(Fields, TEXT("."), true);
					for (const FString& Field : Fields)
					{
						if (!Value.IsTableRef()) { bOk = false; return FMT2QuestValue(); }
						Value = Value.GetTableValue(FMT2QuestValue(Field));
					}
				}
				else { Value = Value.GetTableValue(IndexValue); }
			}
			return Value;
		}

		FMT2QuestValue ParseUnary()
		{
			if (IsOperator(TEXT("-")))
			{
				++Index;
				const FMT2QuestValue Value = ParseUnary();
				if (bValidateOnly) { return FMT2QuestValue(); }
				double Number = 0.0;
				if (!TryNumericValue(Value, Number)) { bOk = false; return FMT2QuestValue(); }
				return FMT2QuestValue(-Number);
			}
			if (IsKeyword(TEXT("not")))
			{
				++Index;
				return FMT2QuestValue::Boolean(!ParseUnary().AsBool());
			}
			return ParseIndexing(ParsePrimary());
		}

		FMT2QuestValue ParsePrimary()
		{
			if (IsOperator(TEXT("{")))
			{
				if (ConstructorDepth >= 64) { bOk = false; return FMT2QuestValue(); }
				TGuardValue<int32> DepthGuard(ConstructorDepth, ConstructorDepth + 1);
				++Index;
				FMT2QuestValue Table = FMT2QuestValue::NewTable();
				int32 SequenceIndex = 1;
				while (bOk && !IsOperator(TEXT("}")) && Peek().Kind != ETokenKind::End)
				{
					FMT2QuestValue Key(SequenceIndex);
					bool bSequence = false;
					if (IsOperator(TEXT("[")))
					{
						++Index;
						Key = ParseOr();
						if (!IsOperator(TEXT("]"))) { bOk = false; break; }
						++Index;
						if (!IsOperator(TEXT("="))) { bOk = false; break; }
						++Index;
					}
					else if (Peek().Kind == ETokenKind::Identifier && Tokens.IsValidIndex(Index + 1) &&
						Tokens[Index + 1].Text == TEXT("="))
					{
						Key = FMT2QuestValue(Peek().Text);
						Index += 2;
					}
					else { ++SequenceIndex; bSequence = true; }
					const FMT2QuestValue Value = ParseOr();
					if (!bValidateOnly && !Table.SetTableValue(Key, Value)) { bOk = false; break; }
					const bool bLast = IsOperator(TEXT("}")) ||
						((IsOperator(TEXT(",")) || IsOperator(TEXT(";"))) && Tokens.IsValidIndex(Index + 1) && Tokens[Index + 1].Text == TEXT("}"));
					if (!bValidateOnly && bSequence && bLast && Value.CallResults)
					{
						for (int32 ResultIndex = 1; ResultIndex < Value.CallResults->Num(); ++ResultIndex)
						{
							if (!Table.SetTableValue(FMT2QuestValue(SequenceIndex++), (*Value.CallResults)[ResultIndex])) { bOk = false; break; }
						}
					}
					if (IsOperator(TEXT(",")) || IsOperator(TEXT(";"))) { ++Index; }
					else if (!IsOperator(TEXT("}"))) { bOk = false; }
				}
				if (IsOperator(TEXT("}"))) { ++Index; } else { bOk = false; }
				return Table;
			}
			const FToken& Token = Peek();
			if (Token.Kind == ETokenKind::Number) { ++Index; return FMT2QuestValue(Token.Number); }
			if (Token.Kind == ETokenKind::String) { ++Index; return FMT2QuestValue(Token.Text); }

			if (IsOperator(TEXT("(")))
			{
				++Index;
				FMT2QuestValue Value = ParseOr();
				if (IsOperator(TEXT(")"))) { ++Index; } else { bOk = false; }
				return Value.Scalar();
			}

			if (Token.Kind == ETokenKind::Identifier)
			{
				const FString Name = Token.Text;
				++Index;
				if (Name == TEXT("true"))  { return FMT2QuestValue::Boolean(true); }
				if (Name == TEXT("false")) { return FMT2QuestValue::Boolean(false); }
				if (Name == TEXT("nil")) { return FMT2QuestValue(); }

				// Call
				if (IsOperator(TEXT("(")))
				{
					++Index;
					TArray<FMT2QuestValue> Arguments;
					if (!IsOperator(TEXT(")")))
					{
						while (true)
						{
							const FMT2QuestValue Argument = ParseOr();
							if (IsOperator(TEXT(")")) && Argument.CallResults) { Arguments.Append(*Argument.CallResults); }
							else { Arguments.Add(Argument.Scalar()); }
							if (IsOperator(TEXT(","))) { ++Index; continue; }
							break;
						}
					}
					if (IsOperator(TEXT(")"))) { ++Index; } else { bOk = false; }
					if (bValidateOnly) { return FMT2QuestValue(); }
					return CallFunction(Name, Arguments, Context, bOk);
				}
				if (bValidateOnly) { return FMT2QuestValue(); }

				// Zero-argument property (pc.level) or a script variable.
				if (GetPropertyNames().Contains(Name))
				{
					return CallFunction(Name, {}, Context, bOk);
				}
				// A converted Lua table (special.levelup_quest, ...) resolves to its root node so the
				// [] operator below can walk into it.
				if (const UMT2QuestTableAsset* Tables = GetQuestTables())
				{
					if (const FMT2QuestTable* Table = Tables->FindTable(FName(*Name)))
					{
						return MakeTableValue(Table, 0);
					}
				}

				// Lua uses dot syntax for named table fields. Dotted API/property names were handled
				// above; for everything else, resolve the first segment as a script variable and walk
				// the remaining named children.
				TArray<FString> Segments;
				Name.ParseIntoArray(Segments, TEXT("."), true);
				FMT2QuestValue Value = Context.Manager && !Segments.IsEmpty()
					? Context.Manager->GetScriptVariable(FName(*Segments[0])) : FMT2QuestValue();
				for (int32 SegmentIndex = 1; SegmentIndex < Segments.Num(); ++SegmentIndex)
				{
					if (!Value.IsTableRef())
					{
						bOk = false;
						return FMT2QuestValue();
					}
					Value = Value.GetTableValue(FMT2QuestValue(Segments[SegmentIndex]));
				}
				return Value;
			}

			bOk = false;
			++Index;
			return FMT2QuestValue();
		}
	};
}

FMT2QuestValue FMT2QuestValue::NewTable()
{
	FMT2QuestValue Value;
	Value.bIsNil = false;
	Value.RuntimeTable = MakeShared<FMT2QuestRuntimeTable>();
	return Value;
}

FMT2QuestValue FMT2QuestValue::Results(TArray<FMT2QuestValue> Values)
{
	for (FMT2QuestValue& Value : Values) { Value = Value.Scalar(); }
	FMT2QuestValue Result = Values.IsEmpty() ? FMT2QuestValue() : Values[0];
	Result.CallResults = MakeShared<const TArray<FMT2QuestValue>>(MoveTemp(Values));
	return Result;
}

FMT2QuestValue FMT2QuestValue::GetTableValue(const FMT2QuestValue& Key) const
{
	if (Key.bIsNil || Key.IsTableRef() || (!Key.bIsText && !FMath::IsFinite(Key.Number))) { return FMT2QuestValue(); }
	if (RuntimeTable)
	{
		const FMT2QuestValue* Value = Key.bIsBoolean ? RuntimeTable->Booleans.Find(Key.AsBool()) :
			(Key.bIsText ? RuntimeTable->Names.Find(Key.Text) : RuntimeTable->Numbers.Find(Key.Number));
		return Value ? *Value : FMT2QuestValue();
	}
	if (!Table) { return FMT2QuestValue(); }
	if (Key.bIsBoolean) { return FMT2QuestValue(); }
	if (!Key.bIsText && (!FMath::IsFinite(Key.Number) || Key.Number < 1 || Key.Number > MAX_int32 ||
		Key.Number != FMath::FloorToDouble(Key.Number))) { return FMT2QuestValue(); }
	const int32 Child = Key.bIsText ? Table->GetNamedChildIndex(TableNodeIndex, FName(*Key.Text))
		: Table->GetChildIndex(TableNodeIndex, static_cast<int32>(Key.Number));
	return MakeTableValue(Table, Child, OwnedTable);
}

int32 FMT2QuestValue::GetTableLength() const
{
	if (RuntimeTable)
	{
		int32 Length = 0;
		while (Length < 10000 && RuntimeTable->Numbers.Contains(Length + 1)) { ++Length; }
		return Length;
	}
	const FMT2QuestTableNode* Root = Table ? Table->GetNode(TableNodeIndex) : nullptr;
	return Root && Root->bIsTable ? Root->Children.Num() : 0;
}

TArray<FMT2QuestValue> FMT2QuestValue::GetTableKeys() const
{
	TArray<FMT2QuestValue> Keys;
	if (RuntimeTable)
	{
		TArray<double> Numbers;
		RuntimeTable->Numbers.GetKeys(Numbers);
		Numbers.Sort();
		for (double Key : Numbers) { Keys.Add(FMT2QuestValue(Key)); }
		TArray<FString> Names;
		RuntimeTable->Names.GetKeys(Names);
		Names.Sort();
		for (const FString& Key : Names) { Keys.Add(FMT2QuestValue(Key)); }
		for (bool Key : {false, true})
		{
			if (RuntimeTable->Booleans.Contains(Key)) { Keys.Add(FMT2QuestValue::Boolean(Key)); }
		}
	}
	else if (const FMT2QuestTableNode* Root = Table ? Table->GetNode(TableNodeIndex) : nullptr)
	{
		for (int32 Index = 1; Index <= Root->Children.Num(); ++Index)
		{
			if (!GetTableValue(FMT2QuestValue(Index)).bIsNil) { Keys.Add(FMT2QuestValue(Index)); }
		}
		TArray<FName> Names;
		Root->NamedChildren.GetKeys(Names);
		Names.Sort(FNameLexicalLess());
		for (FName Name : Names) { Keys.Add(FMT2QuestValue(Name.ToString())); }
	}
	return Keys;
}

FMT2QuestValue FMT2QuestValue::ToRuntimeTable() const
{
	if (RuntimeTable || !IsTableRef()) { return Scalar(); }
	FMT2QuestValue Result = NewTable();
	const FMT2QuestTableNode* Root = Table->GetNode(TableNodeIndex);
	if (!Root || !Root->bIsTable) { return FMT2QuestValue(); }
	for (int32 Index = 0; Index < Root->Children.Num(); ++Index)
	{
		const FMT2QuestValue Value = GetTableValue(FMT2QuestValue(Index + 1));
		if (!Value.bIsNil) { Result.RuntimeTable->Numbers.Add(Index + 1, Value.ToRuntimeTable()); }
	}
	for (const auto& Field : Root->NamedChildren)
	{
		const FMT2QuestValue Value = MakeTableValue(Table, Field.Value, OwnedTable);
		if (!Value.bIsNil) { Result.RuntimeTable->Names.Add(Field.Key.ToString(), Value.ToRuntimeTable()); }
	}
	return Result;
}

bool FMT2QuestValue::SetTableValue(const FMT2QuestValue& Key, const FMT2QuestValue& Value, bool bCommit) const
{
	if (!RuntimeTable || Key.bIsNil || Key.IsTableRef() || (!Key.bIsText && !FMath::IsFinite(Key.Number))) { return false; }
	const bool bExisting = Key.bIsBoolean ? RuntimeTable->Booleans.Contains(Key.AsBool()) :
		(Key.bIsText ? RuntimeTable->Names.Contains(Key.Text) : RuntimeTable->Numbers.Contains(Key.Number));
	if (!Value.bIsNil && !bExisting && RuntimeTable->Names.Num() + RuntimeTable->Numbers.Num() + RuntimeTable->Booleans.Num() >= 10000) { return false; }
	// Shared references preserve table aliases. Reject reference cycles instead of leaking a server
	// conversation through shared-pointer cycles; cyclic Lua table graphs remain unsupported.
	TSet<const FMT2QuestRuntimeTable*> Visited;
	TArray<TSharedPtr<FMT2QuestRuntimeTable>> Pending;
	if (Value.RuntimeTable) { Pending.Add(Value.RuntimeTable); }
	while (!Pending.IsEmpty())
	{
		const TSharedPtr<FMT2QuestRuntimeTable> Next = Pending.Pop();
		if (Next == RuntimeTable) { return false; }
		if (Visited.Contains(Next.Get())) { continue; }
		Visited.Add(Next.Get());
		for (const auto& Entry : Next->Numbers) { if (Entry.Value.RuntimeTable) { Pending.Add(Entry.Value.RuntimeTable); } }
		for (const auto& Entry : Next->Names) { if (Entry.Value.RuntimeTable) { Pending.Add(Entry.Value.RuntimeTable); } }
		for (const auto& Entry : Next->Booleans) { if (Entry.Value.RuntimeTable) { Pending.Add(Entry.Value.RuntimeTable); } }
	}
	if (!bCommit) { return true; }
	if (Key.bIsBoolean)
	{
		if (Value.bIsNil) { RuntimeTable->Booleans.Remove(Key.AsBool()); }
		else { RuntimeTable->Booleans.Add(Key.AsBool(), Value.Scalar()); }
	}
	else if (Key.bIsText)
	{
		if (Value.bIsNil) { RuntimeTable->Names.Remove(Key.Text); }
		else { RuntimeTable->Names.Add(Key.Text, Value.Scalar()); }
	}
	else
	{
		if (Value.bIsNil) { RuntimeTable->Numbers.Remove(Key.Number); }
		else { RuntimeTable->Numbers.Add(Key.Number, Value.Scalar()); }
	}
	return true;
}

FMT2QuestValue FMT2QuestExpression::Evaluate(
	const FString& Expression, const FMT2QuestContext& Context, bool& bOutOk)
{
	const TArray<FToken> Tokens = Tokenize(Expression);
	// Validate the complete expression before calls can mutate gameplay state.
	FParser SyntaxParser{Tokens, Context};
	SyntaxParser.bValidateOnly = true;
	SyntaxParser.ParseOr();
	if (!SyntaxParser.bOk || SyntaxParser.Peek().Kind != ETokenKind::End)
	{
		bOutOk = false;
		return FMT2QuestValue();
	}
	FParser Parser{Tokens, Context};
	FMT2QuestValue Value = Parser.ParseOr();
	// Trailing tokens mean the expression wasn't fully understood.
	bOutOk = Parser.bOk && Parser.Peek().Kind == ETokenKind::End;
	return Value;
}

TArray<FMT2QuestValue> FMT2QuestExpression::EvaluateList(
	const TArray<FString>& Expressions, const FMT2QuestContext& Context, bool& bOutOk)
{
	// Parse every RHS before execution; malformed later expressions must not partially mutate state.
	for (const FString& Expression : Expressions)
	{
		const TArray<FToken> Tokens = Tokenize(Expression);
		FParser Parser{Tokens, Context}; Parser.bValidateOnly = true; Parser.ParseOr();
		if (!Parser.bOk || Parser.Peek().Kind != ETokenKind::End) { bOutOk = false; return {}; }
	}
	TArray<FMT2QuestValue> Results;
	bOutOk = true;
	for (int32 Index = 0; Index < Expressions.Num(); ++Index)
	{
		const FMT2QuestValue Value = Evaluate(Expressions[Index], Context, bOutOk);
		if (!bOutOk) { return {}; }
		if (Index == Expressions.Num() - 1 && Value.CallResults) { Results.Append(*Value.CallResults); }
		else { Results.Add(Value.Scalar()); }
	}
	return Results;
}

bool FMT2QuestExpression::EvaluateBool(const FString& Expression, const FMT2QuestContext& Context)
{
	bool bOk = true;
	const FMT2QuestValue Value = Evaluate(Expression, Context, bOk);
	return bOk && Value.AsBool();
}

bool FMT2QuestExpression::IsSupportedFunction(const FString& FunctionName)
{
	return GetFunctionNames().Contains(FunctionName) || GetPropertyNames().Contains(FunctionName);
}

bool FMT2QuestExpression::IsKnownTable(const FString& TableName)
{
	const UMT2QuestTableAsset* Tables = GetQuestTables();
	return Tables && Tables->FindTable(FName(*TableName)) != nullptr;
}

bool FMT2QuestExpression::IsSupported(const FString& Expression, FString& OutUnsupported)
{
	const TArray<FToken> Tokens = Tokenize(Expression);
	for (int32 Index = 0; Index < Tokens.Num(); ++Index)
	{
		if (Tokens[Index].Kind != ETokenKind::Identifier)
		{
			continue;
		}
		const FString& Name = Tokens[Index].Text;
		if (Name == TEXT("and") || Name == TEXT("or") || Name == TEXT("not") ||
			Name == TEXT("true") || Name == TEXT("false") || Name == TEXT("nil"))
		{
			continue;
		}
		const bool bIsCall = Tokens.IsValidIndex(Index + 1) &&
			Tokens[Index + 1].Kind == ETokenKind::Operator && Tokens[Index + 1].Text == TEXT("(");
		if (bIsCall)
		{
			if (!GetFunctionNames().Contains(Name))
			{
				OutUnsupported = Name;
				return false;
			}
			continue;
		}
		// A bare dotted name that isn't a known property is a table read we can't resolve - unless it
		// names one of the converted Lua tables.
		// Unknown dotted non-call identifiers may be named fields on a local Lua table. The
		// importer has the local-variable scope and validates the root there.
	}
	const FMT2QuestContext EmptyContext;
	FParser Parser{Tokens, EmptyContext};
	Parser.bValidateOnly = true;
	Parser.ParseOr();
	if (!Parser.bOk || Parser.Peek().Kind != ETokenKind::End)
	{
		OutUnsupported = TEXT("expression syntax");
		return false;
	}
	return true;
}
