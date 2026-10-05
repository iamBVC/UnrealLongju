/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Persistence/MT2PersistenceBackend.h"

#include "FramePro/FramePro.h"

#include "Authentication/MT2AuthenticationUtils.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Algo/Reverse.h"
#include "SQLiteDatabase.h"
#include "SQLitePreparedStatement.h"
#include "Stats/MT2PlayerStatFormula.h"

DEFINE_LOG_CATEGORY_STATIC(LogMT2SQLiteBackend, Log, All);

namespace
{
	FString DatabaseError(const FSQLiteDatabase& Database, const FString& Context)
	{
		const FString Detail = Database.GetLastError();
		return Detail.IsEmpty() ? Context : FString::Printf(TEXT("%s: %s"), *Context, *Detail);
	}

	double NumberField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, double Default = 0.0)
	{
		double Value = Default;
		if (Object.IsValid()) Object->TryGetNumberField(Name, Value);
		return Value;
	}

	int32 IntegerField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, int32 Default = 0)
	{
		return FMath::RoundToInt(NumberField(Object, Name, Default));
	}

	bool BooleanField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, bool bDefault = false)
	{
		if (!Object.IsValid()) return bDefault;
		bool bValue = bDefault;
		if (Object->TryGetBoolField(Name, bValue)) return bValue;
		double Number = bDefault ? 1.0 : 0.0;
		return Object->TryGetNumberField(Name, Number) ? !FMath::IsNearlyZero(Number) : bDefault;
	}

	int64 Int64Field(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, int64 Default = 0)
	{
		if (!Object.IsValid()) return Default;
		FString Text;
		if (Object->TryGetStringField(Name, Text)) return FCString::Atoi64(*Text);
		double Number = static_cast<double>(Default);
		return Object->TryGetNumberField(Name, Number) ? static_cast<int64>(Number) : Default;
	}

	FString StringField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, const FString& Default = FString())
	{
		FString Value = Default;
		if (Object.IsValid()) Object->TryGetStringField(Name, Value);
		return Value;
	}

	TSharedPtr<FJsonObject> ObjectField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name)
	{
		const TSharedPtr<FJsonObject>* Child = nullptr;
		return Object.IsValid() && Object->TryGetObjectField(Name, Child) && Child ? *Child : nullptr;
	}

	bool ParseJson(const FString& Json, TSharedPtr<FJsonObject>& OutObject)
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
	}

	FString WriteJson(const TSharedRef<FJsonObject>& Object)
	{
		FString Json;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
		FJsonSerializer::Serialize(Object, Writer);
		return Json;
	}

	// Skills, quickslots and affects are stored as one CSV column each on the players row:
	// entries separated by commas, an entry's numeric fields by colons, in the fixed order given by
	// FieldNames (e.g. skills "vnum:level,vnum:level"). The payload JSON stays the interface; these
	// two only translate between its object arrays and the column text.
	FString CsvFromObjectArray(
		const TArray<TSharedPtr<FJsonValue>>& Values, std::initializer_list<const TCHAR*> FieldNames)
	{
		TArray<FString> Entries;
		Entries.Reserve(Values.Num());
		for (const TSharedPtr<FJsonValue>& Value : Values)
		{
			const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!Object.IsValid())
			{
				continue;
			}
			TArray<FString> Fields;
			for (const TCHAR* FieldName : FieldNames)
			{
				// 17 significant digits preserve exact Unix timestamps and integer counters through
				// the JSON-number/double adapter.
				Fields.Add(FString::Printf(TEXT("%.17g"), NumberField(Object, FieldName)));
			}
			Entries.Add(FString::Join(Fields, TEXT(":")));
		}
		return FString::Join(Entries, TEXT(","));
	}

	TArray<TSharedPtr<FJsonValue>> ObjectArrayFromCsv(
		const FString& Csv, std::initializer_list<const TCHAR*> FieldNames)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		if (Csv.IsEmpty())
		{
			return Values;
		}
		TArray<FString> Entries;
		Csv.ParseIntoArray(Entries, TEXT(","));
		for (const FString& Entry : Entries)
		{
			TArray<FString> Fields;
			Entry.ParseIntoArray(Fields, TEXT(":"), /*CullEmpty*/ false);
			TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			int32 FieldIndex = 0;
			for (const TCHAR* FieldName : FieldNames)
			{
				Object->SetNumberField(FieldName,
					Fields.IsValidIndex(FieldIndex) ? FCString::Atod(*Fields[FieldIndex]) : 0.0);
				++FieldIndex;
			}
			Values.Add(MakeShared<FJsonValueObject>(Object));
		}
		return Values;
	}

	// The per-section field orders. Column name -> JSON field names, shared by save and load so the
	// two sides can never drift apart.
	constexpr std::initializer_list<const TCHAR*> SkillCsvFields =
		{ TEXT("vnum"), TEXT("level"), TEXT("read_count"), TEXT("cooldown_end"),
			TEXT("grandmaster_read_count") };
	constexpr std::initializer_list<const TCHAR*> QuickSlotCsvFields = { TEXT("type"), TEXT("vnum") };
	constexpr std::initializer_list<const TCHAR*> AffectCsvFields =
		{ TEXT("type"), TEXT("apply_on"), TEXT("value"), TEXT("flag"), TEXT("duration"), TEXT("sp_cost"),
		  TEXT("kind"), TEXT("source_item"), TEXT("remove_on_death") };

	bool ReadCharacterSummaryRow(const FSQLitePreparedStatement& Row, FMT2CharacterSummary& OutCharacter)
	{
		int32 Race = 0;
		int32 Sex = 0;
		int32 Style = 0;
		int32 Empire = 0;
		if (!Row.GetColumnValueByIndex(0, OutCharacter.CharacterId) ||
			!Row.GetColumnValueByIndex(1, OutCharacter.CharacterName) ||
			!Row.GetColumnValueByIndex(2, OutCharacter.Level) ||
			!Row.GetColumnValueByIndex(3, Race) || !Row.GetColumnValueByIndex(4, Sex) ||
			!Row.GetColumnValueByIndex(5, Style) || !Row.GetColumnValueByIndex(6, Empire) ||
			!Row.GetColumnValueByIndex(7, OutCharacter.MapId) ||
			!Row.GetColumnValueByIndex(8, OutCharacter.Channel) ||
			!Row.GetColumnValueByIndex(9, OutCharacter.EquippedArmorVnum) ||
			!Row.GetColumnValueByIndex(10, OutCharacter.EquippedWeaponVnum) ||
			!Row.GetColumnValueByIndex(11, OutCharacter.EquippedHairVnum))
		{
			return false;
		}
		OutCharacter.Appearance.Race = static_cast<EMT2CharacterRace>(Race);
		OutCharacter.Appearance.Sex = static_cast<EMT2CharacterSex>(Sex);
		OutCharacter.Appearance.Style = static_cast<EMT2CharacterStyle>(Style);
		OutCharacter.Empire = static_cast<EMT2Empire>(Empire);
		return true;
	}
}

struct FMT2PersistenceBackend::FImpl
{
	FSQLiteDatabase Database;
	FCriticalSection Mutex;
	FString DatabasePath;

	bool Execute(const FString& Sql, FString& OutError)
	{
		if (Database.Execute(*Sql)) return true;
		OutError = DatabaseError(Database, TEXT("SQLite command failed"));
		return false;
	}

	bool ConfigureConnection(const FMT2PersistenceBackendConfig& Config, FString& OutError)
	{
		return Execute(TEXT("PRAGMA journal_mode=WAL;"), OutError) &&
			Execute(FString::Printf(TEXT("PRAGMA busy_timeout=%d;"), Config.BusyTimeoutMilliseconds), OutError) &&
			Execute(TEXT("PRAGMA foreign_keys=ON;"), OutError) &&
			Execute(TEXT("PRAGMA trusted_schema=OFF;"), OutError) &&
			Execute(TEXT("PRAGMA temp_store=MEMORY;"), OutError) &&
			Execute(FString::Printf(TEXT("PRAGMA synchronous=%s;"), *Config.SynchronousMode), OutError) &&
			Execute(TEXT("PRAGMA wal_autocheckpoint=1000;"), OutError) &&
			Execute(TEXT("PRAGMA journal_size_limit=67108864;"), OutError);
	}

	TArray<FString> SchemaStatements() const
	{
		return {
			TEXT("CREATE TABLE IF NOT EXISTS accounts (account_id TEXT PRIMARY KEY,username_normalized TEXT NOT NULL UNIQUE COLLATE NOCASE,display_name TEXT NOT NULL,password_algorithm TEXT NOT NULL,password_salt TEXT NOT NULL,password_hash TEXT NOT NULL,status INTEGER NOT NULL DEFAULT 0,failed_attempts INTEGER NOT NULL DEFAULT 0,locked_until INTEGER NOT NULL DEFAULT 0);"),
			TEXT("CREATE TABLE IF NOT EXISTS players (player_id TEXT PRIMARY KEY,account_id TEXT NOT NULL,character_name TEXT NOT NULL UNIQUE COLLATE NOCASE,character_index INTEGER NOT NULL,level INTEGER NOT NULL DEFAULT 1,experience INTEGER NOT NULL DEFAULT 0,experience_milestone_step INTEGER NOT NULL DEFAULT 0,unspent_stat_points INTEGER NOT NULL DEFAULT 0,unspent_skill_points INTEGER NOT NULL DEFAULT 0,race INTEGER NOT NULL DEFAULT 0,sex INTEGER NOT NULL DEFAULT 0,style INTEGER NOT NULL DEFAULT 0,empire INTEGER NOT NULL DEFAULT 0,guild_id INTEGER NOT NULL DEFAULT 0,guild_name TEXT NOT NULL DEFAULT '',karma INTEGER NOT NULL DEFAULT 0,yang INTEGER NOT NULL DEFAULT 0,st INTEGER NOT NULL DEFAULT 0,dx INTEGER NOT NULL DEFAULT 0,ht INTEGER NOT NULL DEFAULT 0,iq INTEGER NOT NULL DEFAULT 0,health REAL NOT NULL DEFAULT 100,max_health REAL NOT NULL DEFAULT 100,mana REAL NOT NULL DEFAULT 100,max_mana REAL NOT NULL DEFAULT 100,stamina REAL NOT NULL DEFAULT 100,max_stamina REAL NOT NULL DEFAULT 100,movement_speed REAL NOT NULL DEFAULT 100,skill_group INTEGER NOT NULL DEFAULT 0,map_id TEXT NOT NULL,channel INTEGER NOT NULL DEFAULT 1,position_valid INTEGER NOT NULL DEFAULT 0,pos_x REAL NOT NULL DEFAULT 0,pos_y REAL NOT NULL DEFAULT 0,pos_z REAL NOT NULL DEFAULT 0,rotation_pitch REAL NOT NULL DEFAULT 0,rotation_yaw REAL NOT NULL DEFAULT 0,rotation_roll REAL NOT NULL DEFAULT 0,skills TEXT NOT NULL DEFAULT '',quickslots TEXT NOT NULL DEFAULT '',affects TEXT NOT NULL DEFAULT '',startup_items_granted INTEGER NOT NULL DEFAULT 0,UNIQUE(account_id,character_index),FOREIGN KEY(account_id) REFERENCES accounts(account_id) ON DELETE CASCADE);"),
			TEXT("CREATE TABLE IF NOT EXISTS items (item_instance_id TEXT PRIMARY KEY,owner_player_id TEXT NOT NULL,storage_type INTEGER NOT NULL,slot_index INTEGER NOT NULL,vnum INTEGER NOT NULL,item_count INTEGER NOT NULL DEFAULT 1,bonus_type_0 INTEGER NOT NULL DEFAULT 0,bonus_value_0 INTEGER NOT NULL DEFAULT 0,bonus_type_1 INTEGER NOT NULL DEFAULT 0,bonus_value_1 INTEGER NOT NULL DEFAULT 0,bonus_type_2 INTEGER NOT NULL DEFAULT 0,bonus_value_2 INTEGER NOT NULL DEFAULT 0,bonus_type_3 INTEGER NOT NULL DEFAULT 0,bonus_value_3 INTEGER NOT NULL DEFAULT 0,bonus_type_4 INTEGER NOT NULL DEFAULT 0,bonus_value_4 INTEGER NOT NULL DEFAULT 0,rare_bonus_type_0 INTEGER NOT NULL DEFAULT 0,rare_bonus_value_0 INTEGER NOT NULL DEFAULT 0,rare_bonus_type_1 INTEGER NOT NULL DEFAULT 0,rare_bonus_value_1 INTEGER NOT NULL DEFAULT 0,socket_0 INTEGER NOT NULL DEFAULT 0,socket_1 INTEGER NOT NULL DEFAULT 0,socket_2 INTEGER NOT NULL DEFAULT 0,UNIQUE(owner_player_id,storage_type,slot_index),FOREIGN KEY(owner_player_id) REFERENCES players(player_id) ON DELETE CASCADE);"),
			TEXT("CREATE TABLE IF NOT EXISTS player_quests (player_id TEXT NOT NULL,quest_id TEXT NOT NULL,state TEXT NOT NULL DEFAULT '',title TEXT NOT NULL DEFAULT '',summary TEXT NOT NULL DEFAULT '',counter INTEGER NOT NULL DEFAULT -1,PRIMARY KEY(player_id,quest_id),FOREIGN KEY(player_id) REFERENCES players(player_id) ON DELETE CASCADE);"),
			TEXT("CREATE TABLE IF NOT EXISTS player_quest_flags (player_id TEXT NOT NULL,flag_name TEXT NOT NULL,value INTEGER NOT NULL DEFAULT 0,PRIMARY KEY(player_id,flag_name),FOREIGN KEY(player_id) REFERENCES players(player_id) ON DELETE CASCADE);"),
			TEXT("CREATE TABLE IF NOT EXISTS admins (character_name_normalized TEXT PRIMARY KEY COLLATE NOCASE,authority TEXT NOT NULL DEFAULT 'implementor',enabled INTEGER NOT NULL DEFAULT 1);"),
			TEXT("CREATE TABLE IF NOT EXISTS guilds (guild_id INTEGER PRIMARY KEY AUTOINCREMENT,name TEXT NOT NULL UNIQUE COLLATE NOCASE,leader_player_id TEXT NOT NULL UNIQUE,level INTEGER NOT NULL DEFAULT 1,experience INTEGER NOT NULL DEFAULT 0,yang INTEGER NOT NULL DEFAULT 0,FOREIGN KEY(leader_player_id) REFERENCES players(player_id));"),
			TEXT("CREATE TABLE IF NOT EXISTS guild_members (guild_id INTEGER NOT NULL,player_id TEXT NOT NULL UNIQUE,rank INTEGER NOT NULL DEFAULT 15,contributed_experience INTEGER NOT NULL DEFAULT 0,PRIMARY KEY(guild_id,player_id),FOREIGN KEY(guild_id) REFERENCES guilds(guild_id) ON DELETE CASCADE,FOREIGN KEY(player_id) REFERENCES players(player_id) ON DELETE CASCADE);"),
			TEXT("CREATE TABLE IF NOT EXISTS guild_ranks (guild_id INTEGER NOT NULL,rank INTEGER NOT NULL,name TEXT NOT NULL,permissions INTEGER NOT NULL DEFAULT 0,PRIMARY KEY(guild_id,rank),FOREIGN KEY(guild_id) REFERENCES guilds(guild_id) ON DELETE CASCADE);"),
			// Messenger. One row per direction, like the old messenger_list(account, companion): the
			// pair is written twice so a lookup for either side is a single indexed read.
			TEXT("CREATE TABLE IF NOT EXISTS messenger_friends (character_id TEXT NOT NULL,companion_id TEXT NOT NULL,added_unix INTEGER NOT NULL DEFAULT 0,PRIMARY KEY(character_id,companion_id),FOREIGN KEY(character_id) REFERENCES players(player_id) ON DELETE CASCADE,FOREIGN KEY(companion_id) REFERENCES players(player_id) ON DELETE CASCADE);"),
			// Private messages outlive a session, so a message sent to an offline friend is waiting on
			// their next login from any server. delivered_unix is set when it first reaches a client.
			TEXT("CREATE TABLE IF NOT EXISTS messenger_messages (message_id INTEGER PRIMARY KEY AUTOINCREMENT,sender_id TEXT NOT NULL,recipient_id TEXT NOT NULL,body TEXT NOT NULL,sent_unix INTEGER NOT NULL,read_unix INTEGER NOT NULL DEFAULT 0,delivered_unix INTEGER NOT NULL DEFAULT 0);"),
			TEXT("CREATE INDEX IF NOT EXISTS messenger_messages_recipient ON messenger_messages(recipient_id,read_unix);"),
			TEXT("CREATE INDEX IF NOT EXISTS messenger_messages_pair ON messenger_messages(sender_id,recipient_id,sent_unix);")
		};
	}

	bool CreateSchema(FString& OutError)
	{
		if (!Execute(TEXT("BEGIN IMMEDIATE;"), OutError)) return false;
		for (const FString& Sql : SchemaStatements())
		{
			if (!Execute(Sql, OutError))
			{
				Database.Execute(TEXT("ROLLBACK;"));
				return false;
			}
		}
		if (!Execute(TEXT("COMMIT;"), OutError))
		{
			Database.Execute(TEXT("ROLLBACK;"));
			if (OutError.IsEmpty()) OutError = DatabaseError(Database, TEXT("Could not finalize schema"));
			return false;
		}
		return true;
	}

	FMT2PersistenceLoadResult LoadPlayer(const FString& PlayerId)
	{
		FMT2PersistenceLoadResult Result;
		Result.Record.EntityType = TEXT("player");
		Result.Record.EntityId = PlayerId;
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		FSQLitePreparedStatement Statement = Database.PrepareStatement(
			TEXT("SELECT account_id,character_name,level,experience,experience_milestone_step,unspent_skill_points,unspent_stat_points,race,sex,style,empire,guild_id,guild_name,karma,yang,st,dx,ht,iq,health,max_health,mana,max_mana,stamina,max_stamina,movement_speed,skill_group,map_id,position_valid,pos_x,pos_y,pos_z,rotation_pitch,rotation_yaw,rotation_roll,skills,quickslots,affects,startup_items_granted FROM players WHERE player_id=? LIMIT 1;"));
		if (!Statement.IsValid() || !Statement.SetBindingValueByIndex(1, PlayerId))
		{
			Result.Error = DatabaseError(Database, TEXT("Could not prepare player load"));
			return Result;
		}
		const int64 Rows = Statement.Execute([&](const FSQLitePreparedStatement& Row)
		{
			FString Name, GuildName, MapId;
			int32 Level=1, Milestone=0, SkillPoints=0, StatPoints=0, Race=0, Sex=0, Style=0, Empire=0;
			int32 GuildId=0, ST=0, DX=0, HT=0, IQ=0, SkillGroup=0, PositionValid=0, StartupItemsGranted=0;
			double Karma=0;
			int64 Experience=0, Yang=0;
			double Health=100, MaxHealth=100, Mana=100, MaxMana=100, Stamina=100, MaxStamina=100, MoveSpeed=100;
			double X=0,Y=0,Z=0,Pitch=0,Yaw=0,Roll=0;
			FString SkillsCsv, QuickSlotsCsv, AffectsCsv;
			if (!Row.GetColumnValueByIndex(0, Result.Record.OwnerId) || !Row.GetColumnValueByIndex(1, Name) ||
				!Row.GetColumnValueByIndex(2, Level) || !Row.GetColumnValueByIndex(3, Experience) ||
				!Row.GetColumnValueByIndex(4, Milestone) || !Row.GetColumnValueByIndex(5, SkillPoints) ||
				!Row.GetColumnValueByIndex(6, StatPoints) || !Row.GetColumnValueByIndex(7, Race) ||
				!Row.GetColumnValueByIndex(8, Sex) || !Row.GetColumnValueByIndex(9, Style) ||
				!Row.GetColumnValueByIndex(10, Empire) || !Row.GetColumnValueByIndex(11, GuildId) ||
				!Row.GetColumnValueByIndex(12, GuildName) || !Row.GetColumnValueByIndex(13, Karma) ||
				!Row.GetColumnValueByIndex(14, Yang) || !Row.GetColumnValueByIndex(15, ST) ||
				!Row.GetColumnValueByIndex(16, DX) || !Row.GetColumnValueByIndex(17, HT) ||
				!Row.GetColumnValueByIndex(18, IQ) || !Row.GetColumnValueByIndex(19, Health) ||
				!Row.GetColumnValueByIndex(20, MaxHealth) || !Row.GetColumnValueByIndex(21, Mana) ||
				!Row.GetColumnValueByIndex(22, MaxMana) || !Row.GetColumnValueByIndex(23, Stamina) ||
				!Row.GetColumnValueByIndex(24, MaxStamina) || !Row.GetColumnValueByIndex(25, MoveSpeed) ||
				!Row.GetColumnValueByIndex(26, SkillGroup) || !Row.GetColumnValueByIndex(27, MapId) ||
				!Row.GetColumnValueByIndex(28, PositionValid) || !Row.GetColumnValueByIndex(29, X) ||
				!Row.GetColumnValueByIndex(30, Y) || !Row.GetColumnValueByIndex(31, Z) ||
				!Row.GetColumnValueByIndex(32, Pitch) || !Row.GetColumnValueByIndex(33, Yaw) ||
				!Row.GetColumnValueByIndex(34, Roll) || !Row.GetColumnValueByIndex(35, SkillsCsv) ||
				!Row.GetColumnValueByIndex(36, QuickSlotsCsv) || !Row.GetColumnValueByIndex(37, AffectsCsv) ||
				!Row.GetColumnValueByIndex(38, StartupItemsGranted))
			{
				return ESQLitePreparedStatementExecuteRowResult::Error;
			}

			Root->SetStringField(TEXT("character_name"), Name);
			Root->SetNumberField(TEXT("level"), Level);
			Root->SetStringField(TEXT("experience"), LexToString(Experience));
			Root->SetNumberField(TEXT("experience_milestone_step"), Milestone);
			Root->SetNumberField(TEXT("unspent_skill_points"), SkillPoints);
			Root->SetNumberField(TEXT("unspent_stat_points"), StatPoints);
			Root->SetNumberField(TEXT("empire"), Empire);
			Root->SetNumberField(TEXT("guild_id"), GuildId);
			Root->SetStringField(TEXT("guild_name"), GuildName);
			Root->SetNumberField(TEXT("karma"), Karma);
			Root->SetStringField(TEXT("yang"), LexToString(Yang));
			Root->SetBoolField(TEXT("startup_items_granted"), StartupItemsGranted != 0);
			Root->SetNumberField(TEXT("skill_group"), SkillGroup);
			TSharedRef<FJsonObject> Appearance = MakeShared<FJsonObject>();
			Appearance->SetNumberField(TEXT("race"), Race); Appearance->SetNumberField(TEXT("sex"), Sex);
			Appearance->SetNumberField(TEXT("style"), Style); Root->SetObjectField(TEXT("appearance"), Appearance);
			TSharedRef<FJsonObject> Stats = MakeShared<FJsonObject>();
			Stats->SetNumberField(TEXT("st"), ST); Stats->SetNumberField(TEXT("dx"), DX);
			Stats->SetNumberField(TEXT("ht"), HT); Stats->SetNumberField(TEXT("iq"), IQ);
			Root->SetObjectField(TEXT("primary_stats"), Stats);
			TSharedRef<FJsonObject> Attributes = MakeShared<FJsonObject>();
			Attributes->SetNumberField(TEXT("health"), Health); Attributes->SetNumberField(TEXT("max_health"), MaxHealth);
			Attributes->SetNumberField(TEXT("mana"), Mana); Attributes->SetNumberField(TEXT("max_mana"), MaxMana);
			Attributes->SetNumberField(TEXT("stamina"), Stamina); Attributes->SetNumberField(TEXT("max_stamina"), MaxStamina);
			Attributes->SetNumberField(TEXT("movement_speed"), MoveSpeed); Root->SetObjectField(TEXT("attributes"), Attributes);
			if (PositionValid != 0)
			{
				TSharedRef<FJsonObject> World = MakeShared<FJsonObject>();
				World->SetStringField(TEXT("map"), MapId); World->SetNumberField(TEXT("x"), X);
				World->SetNumberField(TEXT("y"), Y); World->SetNumberField(TEXT("z"), Z);
				World->SetNumberField(TEXT("pitch"), Pitch); World->SetNumberField(TEXT("yaw"), Yaw);
				World->SetNumberField(TEXT("roll"), Roll); Root->SetObjectField(TEXT("world"), World);
			}
			Root->SetArrayField(TEXT("skills"), ObjectArrayFromCsv(SkillsCsv, SkillCsvFields));
			Root->SetArrayField(TEXT("quick_slots"), ObjectArrayFromCsv(QuickSlotsCsv, QuickSlotCsvFields));
			Root->SetArrayField(TEXT("affects"), ObjectArrayFromCsv(AffectsCsv, AffectCsvFields));
			Result.Record.bHasWorldPosition = PositionValid != 0;
			Result.bFound = true;
			return ESQLitePreparedStatementExecuteRowResult::Stop;
		});
		if (Rows == INDEX_NONE)
		{
			Result.Error = DatabaseError(Database, TEXT("Player load failed"));
			return Result;
		}
		if (!Result.bFound)
		{
			Result.bSucceeded = true;
			return Result;
		}

		auto ReadObjectArray = [this, &PlayerId](const TCHAR* Sql, TFunctionRef<TSharedRef<FJsonObject>(const FSQLitePreparedStatement&)> Convert,
			TArray<TSharedPtr<FJsonValue>>& OutValues) -> bool
		{
			FSQLitePreparedStatement Child = Database.PrepareStatement(Sql);
			if (!Child.IsValid() || !Child.SetBindingValueByIndex(1, PlayerId)) return false;
			return Child.Execute([&](const FSQLitePreparedStatement& Row)
			{
				OutValues.Add(MakeShared<FJsonValueObject>(Convert(Row)));
				return ESQLitePreparedStatementExecuteRowResult::Continue;
			}) != INDEX_NONE;
		};

		TArray<TSharedPtr<FJsonValue>> Items;
		if (!ReadObjectArray(TEXT("SELECT storage_type,slot_index,vnum,item_count,bonus_type_0,bonus_value_0,bonus_type_1,bonus_value_1,bonus_type_2,bonus_value_2,bonus_type_3,bonus_value_3,bonus_type_4,bonus_value_4,rare_bonus_type_0,rare_bonus_value_0,rare_bonus_type_1,rare_bonus_value_1,socket_0,socket_1,socket_2 FROM items WHERE owner_player_id=? ORDER BY storage_type,slot_index;"),
			[](const FSQLitePreparedStatement& Row)
			{
				int32 Storage=0,Slot=0,Vnum=0,Count=0; Row.GetColumnValueByIndex(0,Storage); Row.GetColumnValueByIndex(1,Slot); Row.GetColumnValueByIndex(2,Vnum); Row.GetColumnValueByIndex(3,Count);
				TSharedRef<FJsonObject> O=MakeShared<FJsonObject>(); O->SetNumberField(TEXT("container"),Storage); O->SetNumberField(TEXT("slot"),Slot); O->SetNumberField(TEXT("vnum"),Vnum); O->SetNumberField(TEXT("count"),Count);
				for (int32 Index=0;Index<5;++Index)
				{
					int32 Type=0,Value=0; Row.GetColumnValueByIndex(4+Index*2,Type); Row.GetColumnValueByIndex(5+Index*2,Value);
					O->SetNumberField(FString::Printf(TEXT("bonus_type_%d"),Index),Type);
					O->SetNumberField(FString::Printf(TEXT("bonus_value_%d"),Index),Value);
				}
				for (int32 Index=0;Index<2;++Index)
				{
					int32 Type=0,Value=0; Row.GetColumnValueByIndex(14+Index*2,Type); Row.GetColumnValueByIndex(15+Index*2,Value);
					O->SetNumberField(FString::Printf(TEXT("rare_bonus_type_%d"),Index),Type);
					O->SetNumberField(FString::Printf(TEXT("rare_bonus_value_%d"),Index),Value);
				}
				for (int32 Index=0;Index<3;++Index)
				{
					int32 Socket=0; Row.GetColumnValueByIndex(18+Index,Socket);
					O->SetNumberField(FString::Printf(TEXT("socket_%d"),Index),Socket);
				}
				return O;
			}, Items))
		{
			Result.Error = DatabaseError(Database, TEXT("Could not load player items")); return Result;
		}
		Root->SetArrayField(TEXT("items"), Items);

		TSharedRef<FJsonObject> QuestStates = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> QuestJournal;
		FSQLitePreparedStatement Quests = Database.PrepareStatement(
			TEXT("SELECT quest_id,state,title,summary,counter FROM player_quests WHERE player_id=? ORDER BY quest_id;"));
		if (!Quests.IsValid() || !Quests.SetBindingValueByIndex(1, PlayerId) ||
			Quests.Execute([&](const FSQLitePreparedStatement& Row)
			{
				FString QuestId, State, Title, Summary;
				int32 Counter = -1;
				if (!Row.GetColumnValueByIndex(0, QuestId) || !Row.GetColumnValueByIndex(1, State) ||
					!Row.GetColumnValueByIndex(2, Title) || !Row.GetColumnValueByIndex(3, Summary) ||
					!Row.GetColumnValueByIndex(4, Counter))
				{
					return ESQLitePreparedStatementExecuteRowResult::Error;
				}
				QuestStates->SetStringField(QuestId, State);
				TSharedRef<FJsonObject> JournalEntry = MakeShared<FJsonObject>();
				JournalEntry->SetStringField(TEXT("quest"), QuestId);
				JournalEntry->SetStringField(TEXT("title"), Title);
				JournalEntry->SetStringField(TEXT("summary"), Summary);
				JournalEntry->SetNumberField(TEXT("counter"), Counter);
				QuestJournal.Add(MakeShared<FJsonValueObject>(JournalEntry));
				return ESQLitePreparedStatementExecuteRowResult::Continue;
			}) == INDEX_NONE)
		{
			Result.Error = DatabaseError(Database, TEXT("Could not load player quests"));
			return Result;
		}
		Root->SetObjectField(TEXT("quest_states"), QuestStates);
		Root->SetArrayField(TEXT("quest_journal"), QuestJournal);

		TSharedRef<FJsonObject> QuestFlags = MakeShared<FJsonObject>();
		FSQLitePreparedStatement Flags = Database.PrepareStatement(
			TEXT("SELECT flag_name,value FROM player_quest_flags WHERE player_id=? ORDER BY flag_name;"));
		if (!Flags.IsValid() || !Flags.SetBindingValueByIndex(1, PlayerId) ||
			Flags.Execute([&](const FSQLitePreparedStatement& Row)
			{
				FString FlagName;
				int32 FlagValue = 0;
				if (!Row.GetColumnValueByIndex(0, FlagName) || !Row.GetColumnValueByIndex(1, FlagValue))
				{
					return ESQLitePreparedStatementExecuteRowResult::Error;
				}
				QuestFlags->SetNumberField(FlagName, FlagValue);
				return ESQLitePreparedStatementExecuteRowResult::Continue;
			}) == INDEX_NONE)
		{
			Result.Error = DatabaseError(Database, TEXT("Could not load player quest flags"));
			return Result;
		}
		Root->SetObjectField(TEXT("quest_flags"), QuestFlags);

		Result.Record.PayloadJson = WriteJson(Root);
		Result.bSucceeded = true;
		return Result;
	}

	bool BindAndExecutePlayerCore(const FMT2PersistentRecord& Record, const TSharedPtr<FJsonObject>& Root, FString& OutError)
	{
		const TSharedPtr<FJsonObject> Appearance = ObjectField(Root, TEXT("appearance"));
		FSQLitePreparedStatement Statement = Database.PrepareStatement(
			TEXT("UPDATE players SET account_id=?,character_name=?,level=?,experience=?,experience_milestone_step=?,unspent_stat_points=?,unspent_skill_points=?,race=?,sex=?,style=?,empire=?,guild_id=?,guild_name=?,karma=?,yang=?,startup_items_granted=? WHERE player_id=?;"));
		const bool bBound = Statement.IsValid() &&
			Statement.SetBindingValueByIndex(1, Record.OwnerId) &&
			Statement.SetBindingValueByIndex(2, StringField(Root,TEXT("character_name"))) &&
			Statement.SetBindingValueByIndex(3, IntegerField(Root,TEXT("level"),1)) &&
			Statement.SetBindingValueByIndex(4, Int64Field(Root,TEXT("experience"))) &&
			Statement.SetBindingValueByIndex(5, IntegerField(Root,TEXT("experience_milestone_step"))) &&
			Statement.SetBindingValueByIndex(6, IntegerField(Root,TEXT("unspent_stat_points"))) &&
			Statement.SetBindingValueByIndex(7, IntegerField(Root,TEXT("unspent_skill_points"))) &&
			Statement.SetBindingValueByIndex(8, IntegerField(Appearance,TEXT("race"))) &&
			Statement.SetBindingValueByIndex(9, IntegerField(Appearance,TEXT("sex"))) &&
			Statement.SetBindingValueByIndex(10, IntegerField(Appearance,TEXT("style"))) &&
			Statement.SetBindingValueByIndex(11, IntegerField(Root,TEXT("empire"))) &&
			Statement.SetBindingValueByIndex(12, IntegerField(Root,TEXT("guild_id"))) &&
			Statement.SetBindingValueByIndex(13, StringField(Root,TEXT("guild_name"))) &&
			Statement.SetBindingValueByIndex(14, NumberField(Root,TEXT("karma"))) &&
			Statement.SetBindingValueByIndex(15, Int64Field(Root,TEXT("yang"))) &&
			Statement.SetBindingValueByIndex(16, BooleanField(Root,TEXT("startup_items_granted")) ? 1 : 0) &&
			Statement.SetBindingValueByIndex(17, Record.EntityId);
		if (bBound && Statement.Execute()) return true;
		OutError = DatabaseError(Database, TEXT("Could not save player core fields"));
		return false;
	}

	bool SavePlayerChildren(const FString& PlayerId, const TSharedPtr<FJsonObject>& Root, FString& OutError)
	{
		const TSharedPtr<FJsonObject> Stats = ObjectField(Root,TEXT("primary_stats"));
		const TSharedPtr<FJsonObject> Attr = ObjectField(Root,TEXT("attributes"));
		FSQLitePreparedStatement Values = Database.PrepareStatement(
			TEXT("UPDATE players SET st=?,dx=?,ht=?,iq=?,health=?,max_health=?,mana=?,max_mana=?,stamina=?,max_stamina=?,movement_speed=?,skill_group=? WHERE player_id=?;"));
		if (!Values.IsValid() || !Values.SetBindingValueByIndex(1,IntegerField(Stats,TEXT("st"))) ||
			!Values.SetBindingValueByIndex(2,IntegerField(Stats,TEXT("dx"))) ||
			!Values.SetBindingValueByIndex(3,IntegerField(Stats,TEXT("ht"))) ||
			!Values.SetBindingValueByIndex(4,IntegerField(Stats,TEXT("iq"))) ||
			!Values.SetBindingValueByIndex(5,NumberField(Attr,TEXT("health"),100)) ||
			!Values.SetBindingValueByIndex(6,NumberField(Attr,TEXT("max_health"),100)) ||
			!Values.SetBindingValueByIndex(7,NumberField(Attr,TEXT("mana"),100)) ||
			!Values.SetBindingValueByIndex(8,NumberField(Attr,TEXT("max_mana"),100)) ||
			!Values.SetBindingValueByIndex(9,NumberField(Attr,TEXT("stamina"),100)) ||
			!Values.SetBindingValueByIndex(10,NumberField(Attr,TEXT("max_stamina"),100)) ||
			!Values.SetBindingValueByIndex(11,NumberField(Attr,TEXT("movement_speed"),100)) ||
			!Values.SetBindingValueByIndex(12,IntegerField(Root,TEXT("skill_group"))) ||
			!Values.SetBindingValueByIndex(13,PlayerId) || !Values.Execute())
		{
			OutError = DatabaseError(Database,TEXT("Could not save player stats and resources")); return false;
		}

		const TSharedPtr<FJsonObject> World = ObjectField(Root,TEXT("world"));
		if (World.IsValid())
		{
			FSQLitePreparedStatement Position = Database.PrepareStatement(TEXT("UPDATE players SET position_valid=1,pos_x=?,pos_y=?,pos_z=?,rotation_pitch=?,rotation_yaw=?,rotation_roll=? WHERE player_id=?;"));
			if (!Position.IsValid() || !Position.SetBindingValueByIndex(1,NumberField(World,TEXT("x"))) ||
				!Position.SetBindingValueByIndex(2,NumberField(World,TEXT("y"))) || !Position.SetBindingValueByIndex(3,NumberField(World,TEXT("z"))) ||
				!Position.SetBindingValueByIndex(4,NumberField(World,TEXT("pitch"))) || !Position.SetBindingValueByIndex(5,NumberField(World,TEXT("yaw"))) ||
				!Position.SetBindingValueByIndex(6,NumberField(World,TEXT("roll"))) || !Position.SetBindingValueByIndex(7,PlayerId) || !Position.Execute())
			{
				OutError=DatabaseError(Database,TEXT("Could not save player position")); return false;
			}
		}

		// Item rows are only rewritten when the payload actually carries an "items" field. A payload
		// without one means the capture could not reach the inventory (the pawn is destroyed before
		// the PlayerState during logout/shutdown), and the unconditional DELETE that used to sit
		// here wiped every stored item on that final save. Present-but-empty still clears, which is
		// correct for a readable inventory that is genuinely empty.
		const TArray<TSharedPtr<FJsonValue>>* Items=nullptr;
		if (Root->TryGetArrayField(TEXT("items"),Items) && Items)
		{
			FSQLitePreparedStatement DeleteItems=Database.PrepareStatement(TEXT("DELETE FROM items WHERE owner_player_id=?;"));
			if (!DeleteItems.IsValid() || !DeleteItems.SetBindingValueByIndex(1,PlayerId) || !DeleteItems.Execute())
			{
				OutError=DatabaseError(Database,TEXT("Could not clear player items")); return false;
			}
			for (const TSharedPtr<FJsonValue>& Value:*Items)
			{
				const TSharedPtr<FJsonObject> Item=Value.IsValid()?Value->AsObject():nullptr;
				if (!Item.IsValid()) continue;
				const int32 Storage=IntegerField(Item,TEXT("container")); const int32 Slot=IntegerField(Item,TEXT("slot"));
				const FString InstanceId=FString::Printf(TEXT("%s:%d:%d"),*PlayerId,Storage,Slot);
				FSQLitePreparedStatement Insert=Database.PrepareStatement(TEXT("INSERT INTO items(item_instance_id,owner_player_id,storage_type,slot_index,vnum,item_count,bonus_type_0,bonus_value_0,bonus_type_1,bonus_value_1,bonus_type_2,bonus_value_2,bonus_type_3,bonus_value_3,bonus_type_4,bonus_value_4,rare_bonus_type_0,rare_bonus_value_0,rare_bonus_type_1,rare_bonus_value_1,socket_0,socket_1,socket_2) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);"));
				bool bBound=Insert.IsValid() && Insert.SetBindingValueByIndex(1,InstanceId) && Insert.SetBindingValueByIndex(2,PlayerId) &&
					Insert.SetBindingValueByIndex(3,Storage) && Insert.SetBindingValueByIndex(4,Slot) &&
					Insert.SetBindingValueByIndex(5,IntegerField(Item,TEXT("vnum"))) &&
					Insert.SetBindingValueByIndex(6,IntegerField(Item,TEXT("count"),1));
				for (int32 Index=0;bBound&&Index<5;++Index)
				{
					bBound=Insert.SetBindingValueByIndex(7+Index*2,IntegerField(Item,*FString::Printf(TEXT("bonus_type_%d"),Index))) &&
						Insert.SetBindingValueByIndex(8+Index*2,IntegerField(Item,*FString::Printf(TEXT("bonus_value_%d"),Index)));
				}
				for (int32 Index=0;bBound&&Index<2;++Index)
				{
					bBound=Insert.SetBindingValueByIndex(17+Index*2,IntegerField(Item,*FString::Printf(TEXT("rare_bonus_type_%d"),Index))) &&
						Insert.SetBindingValueByIndex(18+Index*2,IntegerField(Item,*FString::Printf(TEXT("rare_bonus_value_%d"),Index)));
				}
				for (int32 Index=0;bBound&&Index<3;++Index)
				{
					bBound=Insert.SetBindingValueByIndex(
						21+Index, IntegerField(Item,*FString::Printf(TEXT("socket_%d"),Index)));
				}
				if (!bBound || !Insert.Execute())
				{
					OutError=DatabaseError(Database,TEXT("Could not insert player item")); return false;
				}
			}
		}

		// Skills, quickslots and affects go into one CSV column each on the players row. Skills and
		// quick_slots are captured from the PlayerState so they're always in the payload; affects
		// come from the pawn and follow the same contract as items: an absent field preserves the
		// stored column instead of clearing it.
		auto UpdateCsvColumn = [&](const TCHAR* JsonField, const TCHAR* Sql,
			std::initializer_list<const TCHAR*> FieldNames) -> bool
		{
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			if (!Root->TryGetArrayField(JsonField, Values) || !Values)
			{
				return true;
			}
			FSQLitePreparedStatement Update = Database.PrepareStatement(Sql);
			return Update.IsValid() &&
				Update.SetBindingValueByIndex(1, CsvFromObjectArray(*Values, FieldNames)) &&
				Update.SetBindingValueByIndex(2, PlayerId) && Update.Execute();
		};
		if (!UpdateCsvColumn(TEXT("skills"), TEXT("UPDATE players SET skills=? WHERE player_id=?;"), SkillCsvFields) ||
			!UpdateCsvColumn(TEXT("quick_slots"), TEXT("UPDATE players SET quickslots=? WHERE player_id=?;"), QuickSlotCsvFields) ||
			!UpdateCsvColumn(TEXT("affects"), TEXT("UPDATE players SET affects=? WHERE player_id=?;"), AffectCsvFields))
		{
			OutError = DatabaseError(Database, TEXT("Could not save player skills/quickslots/affects"));
			return false;
		}

		const TSharedPtr<FJsonObject>* QuestStatesObject = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* QuestJournal = nullptr;
		const bool bHasQuestStates = Root->TryGetObjectField(TEXT("quest_states"), QuestStatesObject) &&
			QuestStatesObject && QuestStatesObject->IsValid();
		const bool bHasQuestJournal = Root->TryGetArrayField(TEXT("quest_journal"), QuestJournal) && QuestJournal;
		if (bHasQuestStates || bHasQuestJournal)
		{
			struct FQuestRow
			{
				FString State;
				FString Title;
				FString Summary;
				int32 Counter = -1;
			};
			TMap<FString, FQuestRow> QuestRows;
			if (bHasQuestStates)
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*QuestStatesObject)->Values)
				{
					if (Pair.Value.IsValid() && Pair.Value->Type == EJson::String)
					{
						QuestRows.FindOrAdd(Pair.Key).State = Pair.Value->AsString();
					}
				}
			}
			if (bHasQuestJournal)
			{
				for (const TSharedPtr<FJsonValue>& Value : *QuestJournal)
				{
					const TSharedPtr<FJsonObject> Entry = Value.IsValid() ? Value->AsObject() : nullptr;
					const FString QuestId = StringField(Entry, TEXT("quest"));
					if (QuestId.IsEmpty())
					{
						continue;
					}
					FQuestRow& Row = QuestRows.FindOrAdd(QuestId);
					Row.Title = StringField(Entry, TEXT("title"));
					Row.Summary = StringField(Entry, TEXT("summary"));
					Row.Counter = IntegerField(Entry, TEXT("counter"), -1);
				}
			}

			FSQLitePreparedStatement DeleteQuests = Database.PrepareStatement(
				TEXT("DELETE FROM player_quests WHERE player_id=?;"));
			if (!DeleteQuests.IsValid() || !DeleteQuests.SetBindingValueByIndex(1, PlayerId) ||
				!DeleteQuests.Execute())
			{
				OutError = DatabaseError(Database, TEXT("Could not clear player quests"));
				return false;
			}
			for (const TPair<FString, FQuestRow>& Pair : QuestRows)
			{
				FSQLitePreparedStatement InsertQuest = Database.PrepareStatement(
					TEXT("INSERT INTO player_quests(player_id,quest_id,state,title,summary,counter) VALUES (?,?,?,?,?,?);"));
				if (!InsertQuest.IsValid() || !InsertQuest.SetBindingValueByIndex(1, PlayerId) ||
					!InsertQuest.SetBindingValueByIndex(2, Pair.Key) ||
					!InsertQuest.SetBindingValueByIndex(3, Pair.Value.State) ||
					!InsertQuest.SetBindingValueByIndex(4, Pair.Value.Title) ||
					!InsertQuest.SetBindingValueByIndex(5, Pair.Value.Summary) ||
					!InsertQuest.SetBindingValueByIndex(6, Pair.Value.Counter) || !InsertQuest.Execute())
				{
					OutError = DatabaseError(Database, TEXT("Could not insert player quest"));
					return false;
				}
			}
		}

		const TSharedPtr<FJsonObject>* QuestFlagsObject = nullptr;
		if (Root->TryGetObjectField(TEXT("quest_flags"), QuestFlagsObject) &&
			QuestFlagsObject && QuestFlagsObject->IsValid())
		{
			FSQLitePreparedStatement DeleteFlags = Database.PrepareStatement(
				TEXT("DELETE FROM player_quest_flags WHERE player_id=?;"));
			if (!DeleteFlags.IsValid() || !DeleteFlags.SetBindingValueByIndex(1, PlayerId) ||
				!DeleteFlags.Execute())
			{
				OutError = DatabaseError(Database, TEXT("Could not clear player quest flags"));
				return false;
			}
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*QuestFlagsObject)->Values)
			{
				double FlagValue = 0.0;
				if (!Pair.Value.IsValid() || !Pair.Value->TryGetNumber(FlagValue))
				{
					continue;
				}
				FSQLitePreparedStatement InsertFlag = Database.PrepareStatement(
					TEXT("INSERT INTO player_quest_flags(player_id,flag_name,value) VALUES (?,?,?);"));
				if (!InsertFlag.IsValid() || !InsertFlag.SetBindingValueByIndex(1, PlayerId) ||
					!InsertFlag.SetBindingValueByIndex(2, Pair.Key) ||
					!InsertFlag.SetBindingValueByIndex(3, FMath::RoundToInt(FlagValue)) || !InsertFlag.Execute())
				{
					OutError = DatabaseError(Database, TEXT("Could not insert player quest flag"));
					return false;
				}
			}
		}
		return true;
	}

	FMT2PersistenceSaveResult SavePlayer(const FMT2PersistentRecord& Record)
	{
		FMT2PersistenceSaveResult Result;
		TSharedPtr<FJsonObject> Root;
		if (!ParseJson(Record.PayloadJson,Root))
		{
			Result.Error=TEXT("Player persistence message is invalid."); return Result;
		}
		FString Error;
		if (!Execute(TEXT("BEGIN IMMEDIATE;"),Error)) {Result.Error=Error; return Result;}
		if (!BindAndExecutePlayerCore(Record,Root,Error) || !SavePlayerChildren(Record.EntityId,Root,Error))
		{
			Database.Execute(TEXT("ROLLBACK;")); Result.Error=Error; return Result;
		}
		if (!Execute(TEXT("COMMIT;"),Error))
		{
			Database.Execute(TEXT("ROLLBACK;")); Result.Error=Error; return Result;
		}
		Result.bSucceeded = true;
		return Result;
	}

	FMT2PersistenceLoadResult LoadAdmin(const FString& Name)
	{
		FMT2PersistenceLoadResult Result; Result.Record.EntityType=TEXT("admin"); Result.Record.EntityId=Name;
		FString Authority;
		FSQLitePreparedStatement S=Database.PrepareStatement(TEXT("SELECT authority FROM admins WHERE character_name_normalized=? AND enabled=1 LIMIT 1;"));
		if (!S.IsValid()||!S.SetBindingValueByIndex(1,Name)) {Result.Error=DatabaseError(Database,TEXT("Could not prepare admin load")); return Result;}
		const int64 Rows=S.Execute([&](const FSQLitePreparedStatement& Row){if(!Row.GetColumnValueByIndex(0,Authority))return ESQLitePreparedStatementExecuteRowResult::Error; Result.bFound=true; return ESQLitePreparedStatementExecuteRowResult::Stop;});
		if(Rows==INDEX_NONE){Result.Error=DatabaseError(Database,TEXT("Admin load failed"));return Result;}
		if(Result.bFound){TSharedRef<FJsonObject> O=MakeShared<FJsonObject>();O->SetStringField(TEXT("authority"),Authority);Result.Record.PayloadJson=WriteJson(O);}
		Result.bSucceeded=true; return Result;
	}

	FMT2PersistenceSaveResult SaveAdmin(const FMT2PersistentRecord& Record)
	{
		FMT2PersistenceSaveResult Result; FString Authority=TEXT("implementor"); TSharedPtr<FJsonObject> Root;
		if(ParseJson(Record.PayloadJson,Root)) Root->TryGetStringField(TEXT("authority"),Authority);
		FSQLitePreparedStatement S=Database.PrepareStatement(TEXT("INSERT INTO admins(character_name_normalized,authority,enabled) VALUES (?,?,1) ON CONFLICT(character_name_normalized) DO UPDATE SET authority=excluded.authority,enabled=1;"));
		Result.bSucceeded=S.IsValid()&&S.SetBindingValueByIndex(1,Record.EntityId.ToLower())&&S.SetBindingValueByIndex(2,Authority)&&S.Execute();
		Result.Revision=Result.bSucceeded?1:0; if(!Result.bSucceeded)Result.Error=DatabaseError(Database,TEXT("Admin save failed")); return Result;
	}

	bool ReadCharacters(const FString& AccountId,TArray<FMT2CharacterSummary>& Out,FString& OutError)
	{
		FSQLitePreparedStatement S=Database.PrepareStatement(TEXT(
			"SELECT p.player_id,p.character_name,p.level,p.race,p.sex,p.style,p.empire,p.map_id,p.channel,"
			"COALESCE((SELECT i.vnum FROM items i WHERE i.owner_player_id=p.player_id AND i.storage_type=1 AND i.slot_index=0 LIMIT 1),0),"
			"COALESCE((SELECT i.vnum FROM items i WHERE i.owner_player_id=p.player_id AND i.storage_type=1 AND i.slot_index=4 LIMIT 1),0),"
			"COALESCE((SELECT i.vnum FROM items i WHERE i.owner_player_id=p.player_id AND i.storage_type=1 AND i.slot_index=20 LIMIT 1),"
			"(SELECT i.vnum FROM items i WHERE i.owner_player_id=p.player_id AND i.storage_type=1 AND i.slot_index=1 LIMIT 1),0) "
			"FROM players p WHERE p.account_id=? ORDER BY p.character_index LIMIT 4;"));
		if(!S.IsValid()||!S.SetBindingValueByIndex(1,AccountId)){OutError=DatabaseError(Database,TEXT("Could not prepare character list"));return false;}
		const int64 Rows=S.Execute([&](const FSQLitePreparedStatement& Row){FMT2CharacterSummary& C=Out.AddDefaulted_GetRef();return ReadCharacterSummaryRow(Row,C)?ESQLitePreparedStatementExecuteRowResult::Continue:ESQLitePreparedStatementExecuteRowResult::Error;});
		if(Rows==INDEX_NONE){Out.Reset();OutError=DatabaseError(Database,TEXT("Could not load character list"));return false;} return true;
	}

};

FMT2PersistenceBackend::FMT2PersistenceBackend(FMT2PersistenceBackendConfig InConfig)
	: Impl(MakeUnique<FImpl>()), Config(MoveTemp(InConfig))
{
}

FMT2PersistenceBackend::~FMT2PersistenceBackend()
{
	if(Impl && Impl->Database.IsValid())
	{
		FScopeLock Lock(&Impl->Mutex);
		Impl->Database.Execute(TEXT("PRAGMA optimize;"));
		Impl->Database.Execute(TEXT("PRAGMA wal_checkpoint(TRUNCATE);"));
		Impl->Database.Close();
	}
}

bool FMT2PersistenceBackend::Initialize(FString& OutError)
{
	if(!IFileManager::Get().DirectoryExists(*Config.RootDirectory) && !IFileManager::Get().MakeDirectory(*Config.RootDirectory,true))
	{OutError=FString::Printf(TEXT("Could not create SQLite directory: %s"),*Config.RootDirectory);return false;}
	Impl->DatabasePath=FPaths::Combine(Config.RootDirectory,Config.Filename);
	if(!Impl->Database.Open(*Impl->DatabasePath,ESQLiteDatabaseOpenMode::ReadWriteCreate))
	{OutError=DatabaseError(Impl->Database,FString::Printf(TEXT("Could not open database %s"),*Impl->DatabasePath));return false;}
	if(!Impl->ConfigureConnection(Config,OutError))return false;
	if(Config.bCheckIntegrity && !Impl->Database.PerformQuickIntegrityCheck())
	{OutError=DatabaseError(Impl->Database,TEXT("Database integrity check failed"));return false;}
	if(!Impl->CreateSchema(OutError))return false;
	UE_LOG(LogMT2SQLiteBackend,Display,TEXT("Unified SQLite database ready: %s"),*Impl->DatabasePath);
	return true;
}

bool FMT2PersistenceBackend::Flush(FString& OutError)
{
	FScopeLock Lock(&Impl->Mutex);
	return Impl->Execute(TEXT("PRAGMA wal_checkpoint(FULL);"), OutError);
}

FMT2PersistenceLoadResult FMT2PersistenceBackend::Load(FName EntityType,const FString& EntityId)
{
	FRAMEPRO_NAMED_SCOPE("MT2.Persistence.Load");
	TRACE_CPUPROFILER_EVENT_SCOPE(MT2_Persistence_Load);
	FScopeLock Lock(&Impl->Mutex);
	const FString Type=EntityType.ToString().ToLower();
	if(Type==TEXT("player"))return Impl->LoadPlayer(EntityId);
	if(Type==TEXT("admin"))return Impl->LoadAdmin(EntityId.ToLower());
	FMT2PersistenceLoadResult Result;Result.Error=FString::Printf(TEXT("No relational persistence adapter for entity type '%s'."),*Type);return Result;
}

FMT2PersistenceSaveResult FMT2PersistenceBackend::Save(const FMT2PersistentRecord& Record)
{
	FRAMEPRO_NAMED_SCOPE("MT2.Persistence.Save");
	TRACE_CPUPROFILER_EVENT_SCOPE(MT2_Persistence_Save);
	FScopeLock Lock(&Impl->Mutex);
	const FString Type=Record.EntityType.ToString().ToLower();
	if(Type==TEXT("player"))return Impl->SavePlayer(Record);
	if(Type==TEXT("admin"))return Impl->SaveAdmin(Record);
	FMT2PersistenceSaveResult Result;Result.Error=FString::Printf(TEXT("No relational persistence adapter for entity type '%s'."),*Type);return Result;
}

FMT2AccountRegistrationResult FMT2PersistenceBackend::RegisterAccount(const FString& Username,const FString& Salt,const FString& Hash)
{
	FRAMEPRO_NAMED_SCOPE("MT2.Persistence.RegisterAccount");
	FScopeLock Lock(&Impl->Mutex);FMT2AccountRegistrationResult Result;Result.AccountId=MT2Authentication::GenerateSecureToken(16);
	FSQLitePreparedStatement S=Impl->Database.PrepareStatement(TEXT("INSERT INTO accounts(account_id,username_normalized,display_name,password_algorithm,password_salt,password_hash) VALUES (?,?,?,'pbkdf2-sha256-210000',?,?);"));
	if(Result.AccountId.IsEmpty()||!S.IsValid()||!S.SetBindingValueByIndex(1,Result.AccountId)||!S.SetBindingValueByIndex(2,MT2Authentication::NormalizeUsername(Username))||!S.SetBindingValueByIndex(3,Username.TrimStartAndEnd())||!S.SetBindingValueByIndex(4,Salt)||!S.SetBindingValueByIndex(5,Hash)||!S.Execute())
	{Result.AccountId.Reset();Result.Error=TEXT("Username is unavailable or account creation failed.");return Result;}Result.bSucceeded=true;return Result;
}

FMT2AccountLoginResult FMT2PersistenceBackend::AuthenticateAccount(const FString& Username,const FString& Password)
{
	FRAMEPRO_NAMED_SCOPE("MT2.Persistence.AuthenticateAccount");
	FScopeLock Lock(&Impl->Mutex);FMT2AccountLoginResult Result;FString Salt,Hash,Algorithm;int32 Status=0;int64 LockedUntil=0;
	FSQLitePreparedStatement S=Impl->Database.PrepareStatement(TEXT("SELECT account_id,password_algorithm,password_salt,password_hash,status,locked_until FROM accounts WHERE username_normalized=? LIMIT 1;"));
	if(!S.IsValid()||!S.SetBindingValueByIndex(1,MT2Authentication::NormalizeUsername(Username))){Result.Error=DatabaseError(Impl->Database,TEXT("Could not prepare login"));return Result;}
	const int64 Rows=S.Execute([&](const FSQLitePreparedStatement& Row){return Row.GetColumnValueByIndex(0,Result.AccountId)&&Row.GetColumnValueByIndex(1,Algorithm)&&Row.GetColumnValueByIndex(2,Salt)&&Row.GetColumnValueByIndex(3,Hash)&&Row.GetColumnValueByIndex(4,Status)&&Row.GetColumnValueByIndex(5,LockedUntil)?ESQLitePreparedStatementExecuteRowResult::Continue:ESQLitePreparedStatementExecuteRowResult::Error;});
	const int64 Now=FDateTime::UtcNow().ToUnixTimestamp();const bool bValid=Rows==1&&Status==0&&LockedUntil<=Now&&Algorithm==TEXT("pbkdf2-sha256-210000")&&MT2Authentication::VerifyPassword(Password,Salt,Hash);
	if(!bValid){if(Rows==1&&LockedUntil<=Now){FSQLitePreparedStatement F=Impl->Database.PrepareStatement(TEXT("UPDATE accounts SET failed_attempts=failed_attempts+1,locked_until=CASE WHEN failed_attempts+1>=5 THEN unixepoch()+30 ELSE locked_until END WHERE account_id=?;"));if(F.IsValid()&&F.SetBindingValueByIndex(1,Result.AccountId))F.Execute();}Result.AccountId.Reset();Result.Error=LockedUntil>Now?TEXT("Account is temporarily locked."):TEXT("Invalid username or password.");return Result;}
	FSQLitePreparedStatement Success=Impl->Database.PrepareStatement(TEXT("UPDATE accounts SET failed_attempts=0,locked_until=0 WHERE account_id=?;"));if(Success.IsValid()&&Success.SetBindingValueByIndex(1,Result.AccountId))Success.Execute();
	if(!Impl->ReadCharacters(Result.AccountId,Result.Characters,Result.Error)){Result.AccountId.Reset();return Result;}Result.bSucceeded=true;return Result;
}

FMT2CharacterCreateResult FMT2PersistenceBackend::CreateCharacter(const FString& AccountId,const FString& CharacterName,const FMT2CharacterAppearance& Appearance,EMT2Empire Empire,const FString& InitialMapId)
{
	FScopeLock Lock(&Impl->Mutex);FMT2CharacterCreateResult Result;FString CleanName=CharacterName;CleanName.TrimStartAndEndInline();
	if(CleanName.Len()<2||CleanName.Len()>MT2PlayerLimits::MaxCharacterNameLength||InitialMapId.IsEmpty()){Result.Error=TEXT("Character name or initial map is invalid.");return Result;}
	int64 Count=0;FSQLitePreparedStatement C=Impl->Database.PrepareStatement(TEXT("SELECT COUNT(*) FROM players WHERE account_id=?;"));
	if(!C.IsValid()||!C.SetBindingValueByIndex(1,AccountId)||C.Execute([&](const FSQLitePreparedStatement& Row){return Row.GetColumnValueByIndex(0,Count)?ESQLitePreparedStatementExecuteRowResult::Continue:ESQLitePreparedStatementExecuteRowResult::Error;})!=1||Count>=4){Result.Error=Count>=4?TEXT("Account already has the maximum number of characters."):TEXT("Could not validate character ownership.");return Result;}
	Result.Character.CharacterId=MT2Authentication::GenerateSecureToken(16);Result.Character.CharacterName=CleanName;Result.Character.Appearance=Appearance;Result.Character.Empire=Empire;Result.Character.MapId=InitialMapId;Result.Character.Channel=1;
	const FMT2PrimaryStats Stats=MT2PlayerStatFormula::GetInitialPrimaryStats(Appearance.Race);
	FSQLitePreparedStatement S=Impl->Database.PrepareStatement(TEXT("INSERT INTO players(player_id,account_id,character_name,character_index,level,race,sex,style,empire,map_id,channel,st,dx,ht,iq) VALUES (?,?,?,?,1,?,?,?,?,?,1,?,?,?,?);"));
	if(Result.Character.CharacterId.IsEmpty()||!S.IsValid()||!S.SetBindingValueByIndex(1,Result.Character.CharacterId)||!S.SetBindingValueByIndex(2,AccountId)||!S.SetBindingValueByIndex(3,CleanName)||!S.SetBindingValueByIndex(4,static_cast<int32>(Count))||!S.SetBindingValueByIndex(5,static_cast<int32>(Appearance.Race))||!S.SetBindingValueByIndex(6,static_cast<int32>(Appearance.Sex))||!S.SetBindingValueByIndex(7,static_cast<int32>(Appearance.Style))||!S.SetBindingValueByIndex(8,static_cast<int32>(Empire))||!S.SetBindingValueByIndex(9,InitialMapId)||!S.SetBindingValueByIndex(10,Stats.Strength)||!S.SetBindingValueByIndex(11,Stats.Dexterity)||!S.SetBindingValueByIndex(12,Stats.Constitution)||!S.SetBindingValueByIndex(13,Stats.Intelligence)||!S.Execute()){Result.Character={};Result.Error=TEXT("Character name is unavailable or creation failed.");return Result;}Result.bSucceeded=true;return Result;
}

FMT2CharacterCreateResult FMT2PersistenceBackend::FindCharacter(const FString& AccountId,const FString& CharacterId)
{
	FScopeLock Lock(&Impl->Mutex);FMT2CharacterCreateResult Result;FSQLitePreparedStatement S=Impl->Database.PrepareStatement(TEXT(
		"SELECT p.player_id,p.character_name,p.level,p.race,p.sex,p.style,p.empire,p.map_id,p.channel,"
		"COALESCE((SELECT i.vnum FROM items i WHERE i.owner_player_id=p.player_id AND i.storage_type=1 AND i.slot_index=0 LIMIT 1),0),"
		"COALESCE((SELECT i.vnum FROM items i WHERE i.owner_player_id=p.player_id AND i.storage_type=1 AND i.slot_index=4 LIMIT 1),0),"
		"COALESCE((SELECT i.vnum FROM items i WHERE i.owner_player_id=p.player_id AND i.storage_type=1 AND i.slot_index=20 LIMIT 1),"
		"(SELECT i.vnum FROM items i WHERE i.owner_player_id=p.player_id AND i.storage_type=1 AND i.slot_index=1 LIMIT 1),0) "
		"FROM players p WHERE p.account_id=? AND p.player_id=? LIMIT 1;"));
	if(!S.IsValid()||!S.SetBindingValueByIndex(1,AccountId)||!S.SetBindingValueByIndex(2,CharacterId)){Result.Error=DatabaseError(Impl->Database,TEXT("Could not prepare character lookup"));return Result;}
	const int64 Rows=S.Execute([&](const FSQLitePreparedStatement& Row){return ReadCharacterSummaryRow(Row,Result.Character)?ESQLitePreparedStatementExecuteRowResult::Continue:ESQLitePreparedStatementExecuteRowResult::Error;});Result.bSucceeded=Rows==1;if(!Result.bSucceeded)Result.Error=TEXT("Character does not belong to this account.");return Result;
}

bool FMT2PersistenceBackend::UpdateCharacterLocation(const FString& CharacterId,const FString& MapId,int32 Channel,FString& OutError)
{
	FScopeLock Lock(&Impl->Mutex);FSQLitePreparedStatement S=Impl->Database.PrepareStatement(TEXT("UPDATE players SET map_id=?,channel=? WHERE player_id=? RETURNING player_id;"));
	if(!S.IsValid()||!S.SetBindingValueByIndex(1,MapId)||!S.SetBindingValueByIndex(2,FMath::Max(Channel,1))||!S.SetBindingValueByIndex(3,CharacterId)){OutError=DatabaseError(Impl->Database,TEXT("Could not prepare character location update"));return false;}
	const int64 Rows=S.Execute([](const FSQLitePreparedStatement&){return ESQLitePreparedStatementExecuteRowResult::Continue;});if(Rows!=1){OutError=Rows==0?TEXT("Character location update found no active character."):DatabaseError(Impl->Database,TEXT("Character location update failed"));return false;}return true;
}

// -------------------------------------------------------------------------------------------------
// Messenger
// -------------------------------------------------------------------------------------------------
namespace
{
	int64 MessengerNowUnix()
	{
		return FDateTime::UtcNow().ToUnixTimestamp();
	}
}

TArray<FMT2FriendEntry> FMT2PersistenceBackend::LoadFriends(const FString& CharacterId)
{
	TArray<FMT2FriendEntry> Friends;
	FScopeLock Lock(&Impl->Mutex);
	FSQLitePreparedStatement Statement = Impl->Database.PrepareStatement(
		TEXT("SELECT p.player_id,p.character_name,p.level,p.map_id,p.channel,")
		TEXT("(SELECT COUNT(*) FROM messenger_messages m WHERE m.sender_id=p.player_id ")
		TEXT("AND m.recipient_id=? AND m.read_unix=0) ")
		TEXT("FROM messenger_friends f JOIN players p ON p.player_id=f.companion_id ")
		TEXT("WHERE f.character_id=? ORDER BY p.character_name COLLATE NOCASE;"));
	if (!Statement.IsValid() || !Statement.SetBindingValueByIndex(1, CharacterId) ||
		!Statement.SetBindingValueByIndex(2, CharacterId))
	{
		return Friends;
	}
	Statement.Execute([&Friends](const FSQLitePreparedStatement& Row)
	{
		FMT2FriendEntry Entry;
		Row.GetColumnValueByIndex(0, Entry.CharacterId);
		Row.GetColumnValueByIndex(1, Entry.CharacterName);
		Row.GetColumnValueByIndex(2, Entry.Level);
		Row.GetColumnValueByIndex(3, Entry.MapId);
		Row.GetColumnValueByIndex(4, Entry.Channel);
		Row.GetColumnValueByIndex(5, Entry.UnreadCount);
		// Presence is session state the coordinator owns; the stored map is only a last-known value.
		Entry.bOnline = false;
		Friends.Add(MoveTemp(Entry));
		return ESQLitePreparedStatementExecuteRowResult::Continue;
	});
	return Friends;
}

bool FMT2PersistenceBackend::AddFriendPair(
	const FString& CharacterId, const FString& CompanionId, FString& OutError)
{
	FScopeLock Lock(&Impl->Mutex);
	const int64 Now = MessengerNowUnix();
	// Both directions, so either side reading its own list sees the friendship.
	for (int32 Direction = 0; Direction < 2; ++Direction)
	{
		FSQLitePreparedStatement Statement = Impl->Database.PrepareStatement(
			TEXT("INSERT OR IGNORE INTO messenger_friends (character_id,companion_id,added_unix) VALUES (?,?,?);"));
		const FString& Left = Direction == 0 ? CharacterId : CompanionId;
		const FString& Right = Direction == 0 ? CompanionId : CharacterId;
		if (!Statement.IsValid() || !Statement.SetBindingValueByIndex(1, Left) ||
			!Statement.SetBindingValueByIndex(2, Right) || !Statement.SetBindingValueByIndex(3, Now) ||
			!Statement.Execute())
		{
			OutError = DatabaseError(Impl->Database, TEXT("Could not add friend"));
			return false;
		}
	}
	return true;
}

bool FMT2PersistenceBackend::RemoveFriendPair(
	const FString& CharacterId, const FString& CompanionId, FString& OutError)
{
	FScopeLock Lock(&Impl->Mutex);
	FSQLitePreparedStatement Statement = Impl->Database.PrepareStatement(
		TEXT("DELETE FROM messenger_friends WHERE (character_id=? AND companion_id=?) ")
		TEXT("OR (character_id=? AND companion_id=?);"));
	if (!Statement.IsValid() || !Statement.SetBindingValueByIndex(1, CharacterId) ||
		!Statement.SetBindingValueByIndex(2, CompanionId) ||
		!Statement.SetBindingValueByIndex(3, CompanionId) ||
		!Statement.SetBindingValueByIndex(4, CharacterId) || !Statement.Execute())
	{
		OutError = DatabaseError(Impl->Database, TEXT("Could not remove friend"));
		return false;
	}
	return true;
}

bool FMT2PersistenceBackend::AreFriends(const FString& CharacterId, const FString& CompanionId)
{
	FScopeLock Lock(&Impl->Mutex);
	FSQLitePreparedStatement Statement = Impl->Database.PrepareStatement(
		TEXT("SELECT 1 FROM messenger_friends WHERE character_id=? AND companion_id=? LIMIT 1;"));
	if (!Statement.IsValid() || !Statement.SetBindingValueByIndex(1, CharacterId) ||
		!Statement.SetBindingValueByIndex(2, CompanionId))
	{
		return false;
	}
	bool bFound = false;
	Statement.Execute([&bFound](const FSQLitePreparedStatement&)
	{
		bFound = true;
		return ESQLitePreparedStatementExecuteRowResult::Stop;
	});
	return bFound;
}

bool FMT2PersistenceBackend::FindCharacterIdByName(
	const FString& CharacterName, FString& OutCharacterId, FString& OutName)
{
	FScopeLock Lock(&Impl->Mutex);
	FSQLitePreparedStatement Statement = Impl->Database.PrepareStatement(
		TEXT("SELECT player_id,character_name FROM players WHERE character_name=? COLLATE NOCASE LIMIT 1;"));
	if (!Statement.IsValid() || !Statement.SetBindingValueByIndex(1, CharacterName))
	{
		return false;
	}
	bool bFound = false;
	Statement.Execute([&OutCharacterId, &OutName, &bFound](const FSQLitePreparedStatement& Row)
	{
		Row.GetColumnValueByIndex(0, OutCharacterId);
		Row.GetColumnValueByIndex(1, OutName);
		bFound = true;
		return ESQLitePreparedStatementExecuteRowResult::Stop;
	});
	return bFound;
}

int64 FMT2PersistenceBackend::StorePrivateMessage(
	const FMT2PrivateMessage& PrivateMessage, FString& OutError)
{
	FScopeLock Lock(&Impl->Mutex);
	FSQLitePreparedStatement Statement = Impl->Database.PrepareStatement(
		TEXT("INSERT INTO messenger_messages (sender_id,recipient_id,body,sent_unix) VALUES (?,?,?,?) ")
		TEXT("RETURNING message_id;"));
	if (!Statement.IsValid() ||
		!Statement.SetBindingValueByIndex(1, PrivateMessage.SenderCharacterId) ||
		!Statement.SetBindingValueByIndex(2, PrivateMessage.RecipientCharacterId) ||
		!Statement.SetBindingValueByIndex(3, PrivateMessage.Body) ||
		!Statement.SetBindingValueByIndex(4, PrivateMessage.SentUnixTime))
	{
		OutError = DatabaseError(Impl->Database, TEXT("Could not store private message"));
		return 0;
	}
	int64 MessageId = 0;
	Statement.Execute([&MessageId](const FSQLitePreparedStatement& Row)
	{
		Row.GetColumnValueByIndex(0, MessageId);
		return ESQLitePreparedStatementExecuteRowResult::Stop;
	});
	if (MessageId == 0)
	{
		OutError = DatabaseError(Impl->Database, TEXT("Private message insert returned no id"));
	}
	return MessageId;
}

TArray<FMT2PrivateMessage> FMT2PersistenceBackend::LoadUndeliveredMessages(const FString& CharacterId)
{
	TArray<FMT2PrivateMessage> Messages;
	FScopeLock Lock(&Impl->Mutex);
	FSQLitePreparedStatement Statement = Impl->Database.PrepareStatement(
		TEXT("SELECT m.message_id,m.sender_id,p.character_name,m.body,m.sent_unix,m.delivered_unix ")
		TEXT("FROM messenger_messages m LEFT JOIN players p ON p.player_id=m.sender_id ")
		TEXT("WHERE m.recipient_id=? AND m.read_unix=0 ORDER BY m.sent_unix ASC;"));
	if (!Statement.IsValid() || !Statement.SetBindingValueByIndex(1, CharacterId))
	{
		return Messages;
	}
	Statement.Execute([&Messages, &CharacterId](const FSQLitePreparedStatement& Row)
	{
		FMT2PrivateMessage Message;
		Row.GetColumnValueByIndex(0, Message.MessageId);
		Row.GetColumnValueByIndex(1, Message.SenderCharacterId);
		Row.GetColumnValueByIndex(2, Message.SenderName);
		Row.GetColumnValueByIndex(3, Message.Body);
		Row.GetColumnValueByIndex(4, Message.SentUnixTime);
		int64 DeliveredUnix = 0;
		Row.GetColumnValueByIndex(5, DeliveredUnix);
		// Never handed to a client before, so this one waited for the player to come back.
		Message.bWasOffline = DeliveredUnix == 0;
		Message.RecipientCharacterId = CharacterId;
		Messages.Add(MoveTemp(Message));
		return ESQLitePreparedStatementExecuteRowResult::Continue;
	});
	return Messages;
}

TArray<FMT2PrivateMessage> FMT2PersistenceBackend::LoadConversation(
	const FString& CharacterId, const FString& CompanionId, int32 MaxMessages)
{
	TArray<FMT2PrivateMessage> Messages;
	FScopeLock Lock(&Impl->Mutex);
	// Newest first so the limit keeps the most recent exchange; reversed below for display order.
	FSQLitePreparedStatement Statement = Impl->Database.PrepareStatement(
		TEXT("SELECT m.message_id,m.sender_id,p.character_name,m.recipient_id,m.body,m.sent_unix,m.read_unix ")
		TEXT("FROM messenger_messages m LEFT JOIN players p ON p.player_id=m.sender_id ")
		TEXT("WHERE (m.sender_id=? AND m.recipient_id=?) OR (m.sender_id=? AND m.recipient_id=?) ")
		TEXT("ORDER BY m.sent_unix DESC LIMIT ?;"));
	if (!Statement.IsValid() || !Statement.SetBindingValueByIndex(1, CharacterId) ||
		!Statement.SetBindingValueByIndex(2, CompanionId) ||
		!Statement.SetBindingValueByIndex(3, CompanionId) ||
		!Statement.SetBindingValueByIndex(4, CharacterId) ||
		!Statement.SetBindingValueByIndex(5, FMath::Max(MaxMessages, 1)))
	{
		return Messages;
	}
	Statement.Execute([&Messages](const FSQLitePreparedStatement& Row)
	{
		FMT2PrivateMessage Message;
		Row.GetColumnValueByIndex(0, Message.MessageId);
		Row.GetColumnValueByIndex(1, Message.SenderCharacterId);
		Row.GetColumnValueByIndex(2, Message.SenderName);
		Row.GetColumnValueByIndex(3, Message.RecipientCharacterId);
		Row.GetColumnValueByIndex(4, Message.Body);
		Row.GetColumnValueByIndex(5, Message.SentUnixTime);
		int64 ReadUnix = 0;
		Row.GetColumnValueByIndex(6, ReadUnix);
		Message.bRead = ReadUnix != 0;
		Messages.Add(MoveTemp(Message));
		return ESQLitePreparedStatementExecuteRowResult::Continue;
	});
	Algo::Reverse(Messages);
	return Messages;
}

void FMT2PersistenceBackend::MarkMessagesRead(const FString& CharacterId, const FString& CompanionId)
{
	FScopeLock Lock(&Impl->Mutex);
	FSQLitePreparedStatement Statement = Impl->Database.PrepareStatement(
		TEXT("UPDATE messenger_messages SET read_unix=? WHERE recipient_id=? AND sender_id=? AND read_unix=0;"));
	if (Statement.IsValid() && Statement.SetBindingValueByIndex(1, MessengerNowUnix()) &&
		Statement.SetBindingValueByIndex(2, CharacterId) &&
		Statement.SetBindingValueByIndex(3, CompanionId))
	{
		Statement.Execute();
	}
}

void FMT2PersistenceBackend::MarkMessagesDelivered(const TArray<int64>& MessageIds)
{
	if (MessageIds.IsEmpty())
	{
		return;
	}
	FScopeLock Lock(&Impl->Mutex);
	FSQLitePreparedStatement Statement = Impl->Database.PrepareStatement(
		TEXT("UPDATE messenger_messages SET delivered_unix=? WHERE message_id=? AND delivered_unix=0;"));
	if (!Statement.IsValid())
	{
		return;
	}
	const int64 Now = MessengerNowUnix();
	for (const int64 MessageId : MessageIds)
	{
		Statement.Reset();
		if (Statement.SetBindingValueByIndex(1, Now) && Statement.SetBindingValueByIndex(2, MessageId))
		{
			Statement.Execute();
		}
	}
}

// -------------------------------------------------------------------------------------------------
// Guilds
// -------------------------------------------------------------------------------------------------

bool FMT2PersistenceBackend::LoadGuildForCharacter(
	const FString& CharacterId, FMT2GuildSnapshot& OutGuild)
{
	int32 GuildId = 0;
	{
		FScopeLock Lock(&Impl->Mutex);
		FSQLitePreparedStatement Statement = Impl->Database.PrepareStatement(
			TEXT("SELECT guild_id FROM guild_members WHERE player_id=? LIMIT 1;"));
		if (!Statement.IsValid() || !Statement.SetBindingValueByIndex(1, CharacterId)) return false;
		Statement.Execute([&](const FSQLitePreparedStatement& Row)
		{
			Row.GetColumnValueByIndex(0, GuildId);
			return ESQLitePreparedStatementExecuteRowResult::Stop;
		});
	}
	return GuildId > 0 && LoadGuild(GuildId, OutGuild);
}

bool FMT2PersistenceBackend::LoadGuild(int32 GuildId, FMT2GuildSnapshot& OutGuild)
{
	FScopeLock Lock(&Impl->Mutex);
	OutGuild = {};
	FSQLitePreparedStatement Guild = Impl->Database.PrepareStatement(
		TEXT("SELECT guild_id,name,leader_player_id,level,experience,yang FROM guilds WHERE guild_id=? LIMIT 1;"));
	if (!Guild.IsValid() || !Guild.SetBindingValueByIndex(1, GuildId)) return false;
	const int64 GuildRows = Guild.Execute([&](const FSQLitePreparedStatement& Row)
	{
		return Row.GetColumnValueByIndex(0, OutGuild.GuildId) &&
			Row.GetColumnValueByIndex(1, OutGuild.Name) &&
			Row.GetColumnValueByIndex(2, OutGuild.LeaderCharacterId) &&
			Row.GetColumnValueByIndex(3, OutGuild.Level) &&
			Row.GetColumnValueByIndex(4, OutGuild.Experience) &&
			Row.GetColumnValueByIndex(5, OutGuild.Yang)
			? ESQLitePreparedStatementExecuteRowResult::Continue
			: ESQLitePreparedStatementExecuteRowResult::Error;
	});
	if (GuildRows != 1) return false;

	FSQLitePreparedStatement Ranks = Impl->Database.PrepareStatement(
		TEXT("SELECT rank,name,permissions FROM guild_ranks WHERE guild_id=? ORDER BY rank;"));
	if (!Ranks.IsValid() || !Ranks.SetBindingValueByIndex(1, GuildId)) return false;
	Ranks.Execute([&](const FSQLitePreparedStatement& Row)
	{
		FMT2GuildRank& Rank = OutGuild.Ranks.AddDefaulted_GetRef();
		Row.GetColumnValueByIndex(0, Rank.Rank);
		Row.GetColumnValueByIndex(1, Rank.Name);
		Row.GetColumnValueByIndex(2, Rank.Permissions);
		return ESQLitePreparedStatementExecuteRowResult::Continue;
	});

	FSQLitePreparedStatement Members = Impl->Database.PrepareStatement(
		TEXT("SELECT p.player_id,p.character_name,m.rank,p.level,m.contributed_experience,p.map_id,p.channel ")
		TEXT("FROM guild_members m JOIN players p ON p.player_id=m.player_id ")
		TEXT("WHERE m.guild_id=? ORDER BY m.rank,p.character_name COLLATE NOCASE;"));
	if (!Members.IsValid() || !Members.SetBindingValueByIndex(1, GuildId)) return false;
	Members.Execute([&](const FSQLitePreparedStatement& Row)
	{
		FMT2GuildMember& Member = OutGuild.Members.AddDefaulted_GetRef();
		Row.GetColumnValueByIndex(0, Member.CharacterId);
		Row.GetColumnValueByIndex(1, Member.CharacterName);
		Row.GetColumnValueByIndex(2, Member.Rank);
		Row.GetColumnValueByIndex(3, Member.Level);
		Row.GetColumnValueByIndex(4, Member.ContributedExperience);
		Row.GetColumnValueByIndex(5, Member.MapId);
		Row.GetColumnValueByIndex(6, Member.Channel);
		return ESQLitePreparedStatementExecuteRowResult::Continue;
	});
	return true;
}

EMT2GuildResult FMT2PersistenceBackend::CreateGuild(
	const FString& LeaderCharacterId, const FString& GuildName, FMT2GuildSnapshot& OutGuild)
{
	FString CleanName = GuildName.TrimStartAndEnd().Left(MT2Guild::MaximumNameLength);
	if (CleanName.Len() < 2) return EMT2GuildResult::InvalidName;
	int32 GuildId = 0;
	{
		FScopeLock Lock(&Impl->Mutex);
		FString Error;
		if (!Impl->Execute(TEXT("BEGIN IMMEDIATE;"), Error)) return EMT2GuildResult::Unavailable;
		auto Rollback = [&]() { Impl->Database.Execute(TEXT("ROLLBACK;")); };
		int32 LeaderLevel = 0;
		int64 LeaderYang = 0;
		int32 ExistingGuildId = 0;
		FSQLitePreparedStatement Eligibility = Impl->Database.PrepareStatement(
			TEXT("SELECT level,yang,guild_id FROM players WHERE player_id=? LIMIT 1;"));
		if (!Eligibility.IsValid() || !Eligibility.SetBindingValueByIndex(1, LeaderCharacterId) ||
			Eligibility.Execute([&](const FSQLitePreparedStatement& Row)
			{
				return Row.GetColumnValueByIndex(0, LeaderLevel) &&
					Row.GetColumnValueByIndex(1, LeaderYang) &&
					Row.GetColumnValueByIndex(2, ExistingGuildId)
					? ESQLitePreparedStatementExecuteRowResult::Continue
					: ESQLitePreparedStatementExecuteRowResult::Error;
			}) != 1)
		{
			Rollback(); return EMT2GuildResult::UnknownCharacter;
		}
		if (LeaderLevel < MT2GuildLimits::MinCreateLevel)
			{ Rollback(); return EMT2GuildResult::LevelTooLow; }
		if (ExistingGuildId > 0)
			{ Rollback(); return EMT2GuildResult::AlreadyInGuild; }
		if (LeaderYang < MT2Guild::CreationCost)
			{ Rollback(); return EMT2GuildResult::InsufficientYang; }
		FSQLitePreparedStatement Insert = Impl->Database.PrepareStatement(
			TEXT("INSERT INTO guilds(name,leader_player_id) VALUES (?,?);"));
		if (!Insert.IsValid() || !Insert.SetBindingValueByIndex(1, CleanName) ||
			!Insert.SetBindingValueByIndex(2, LeaderCharacterId) || !Insert.Execute())
		{
			Rollback(); return EMT2GuildResult::NameUnavailable;
		}
		FSQLitePreparedStatement Find = Impl->Database.PrepareStatement(
			TEXT("SELECT guild_id FROM guilds WHERE leader_player_id=? LIMIT 1;"));
		if (!Find.IsValid() || !Find.SetBindingValueByIndex(1, LeaderCharacterId) ||
			Find.Execute([&](const FSQLitePreparedStatement& Row)
			{
				Row.GetColumnValueByIndex(0, GuildId);
				return ESQLitePreparedStatementExecuteRowResult::Stop;
			}) != 1)
		{
			Rollback(); return EMT2GuildResult::Unavailable;
		}
		FSQLitePreparedStatement Member = Impl->Database.PrepareStatement(
			TEXT("INSERT INTO guild_members(guild_id,player_id,rank) VALUES (?,?,1);"));
		if (!Member.IsValid() || !Member.SetBindingValueByIndex(1, GuildId) ||
			!Member.SetBindingValueByIndex(2, LeaderCharacterId) || !Member.Execute())
		{
			Rollback(); return EMT2GuildResult::AlreadyInGuild;
		}
		for (int32 RankIndex = 1; RankIndex <= MT2Guild::RankCount; ++RankIndex)
		{
			FSQLitePreparedStatement Rank = Impl->Database.PrepareStatement(
				TEXT("INSERT INTO guild_ranks(guild_id,rank,name,permissions) VALUES (?,?,?,?);"));
			const int32 AllPermissions = static_cast<int32>(EMT2GuildPermission::InviteMembers) |
				static_cast<int32>(EMT2GuildPermission::RemoveMembers) |
				static_cast<int32>(EMT2GuildPermission::WriteNotice) |
				static_cast<int32>(EMT2GuildPermission::UseSkills);
			if (!Rank.IsValid() || !Rank.SetBindingValueByIndex(1, GuildId) ||
				!Rank.SetBindingValueByIndex(2, RankIndex) ||
				!Rank.SetBindingValueByIndex(3, RankIndex == 1 ? TEXT("Leader") : TEXT("Member")) ||
				!Rank.SetBindingValueByIndex(4, RankIndex == 1 ? AllPermissions : 0) || !Rank.Execute())
			{
				Rollback(); return EMT2GuildResult::Unavailable;
			}
		}
		FSQLitePreparedStatement Player = Impl->Database.PrepareStatement(
			TEXT("UPDATE players SET guild_id=?,guild_name=?,yang=yang-? WHERE player_id=? AND yang>=?;"));
		if (!Player.IsValid() || !Player.SetBindingValueByIndex(1, GuildId) ||
			!Player.SetBindingValueByIndex(2, CleanName) ||
			!Player.SetBindingValueByIndex(3, MT2Guild::CreationCost) ||
			!Player.SetBindingValueByIndex(4, LeaderCharacterId) ||
			!Player.SetBindingValueByIndex(5, MT2Guild::CreationCost) || !Player.Execute() ||
			!Impl->Execute(TEXT("COMMIT;"), Error))
		{
			Rollback(); return EMT2GuildResult::Unavailable;
		}
	}
	return LoadGuild(GuildId, OutGuild) ? EMT2GuildResult::Success : EMT2GuildResult::Unavailable;
}

EMT2GuildResult FMT2PersistenceBackend::AddGuildMember(
	int32 GuildId, const FString& CharacterId, int32 Rank)
{
	FScopeLock Lock(&Impl->Mutex);
	FSQLitePreparedStatement Count = Impl->Database.PrepareStatement(
		TEXT("SELECT COUNT(*) FROM guild_members WHERE guild_id=?;"));
	int64 MemberCount = 0;
	if (!Count.IsValid() || !Count.SetBindingValueByIndex(1, GuildId) ||
		Count.Execute([&](const FSQLitePreparedStatement& Row)
		{ Row.GetColumnValueByIndex(0, MemberCount); return ESQLitePreparedStatementExecuteRowResult::Stop; }) != 1)
		return EMT2GuildResult::Unavailable;
	if (MemberCount >= MT2Guild::MaximumMembers) return EMT2GuildResult::GuildFull;
	FSQLitePreparedStatement Insert = Impl->Database.PrepareStatement(
		TEXT("INSERT INTO guild_members(guild_id,player_id,rank) VALUES (?,?,?);"));
	if (!Insert.IsValid() || !Insert.SetBindingValueByIndex(1, GuildId) ||
		!Insert.SetBindingValueByIndex(2, CharacterId) ||
		!Insert.SetBindingValueByIndex(3, FMath::Clamp(Rank, 2, MT2Guild::RankCount)) || !Insert.Execute())
		return EMT2GuildResult::AlreadyInGuild;
	FSQLitePreparedStatement Player = Impl->Database.PrepareStatement(
		TEXT("UPDATE players SET guild_id=?,guild_name=(SELECT name FROM guilds WHERE guild_id=?) WHERE player_id=?;"));
	return Player.IsValid() && Player.SetBindingValueByIndex(1, GuildId) &&
		Player.SetBindingValueByIndex(2, GuildId) && Player.SetBindingValueByIndex(3, CharacterId) &&
		Player.Execute() ? EMT2GuildResult::Success : EMT2GuildResult::Unavailable;
}

EMT2GuildResult FMT2PersistenceBackend::RemoveGuildMember(int32 GuildId, const FString& CharacterId)
{
	FScopeLock Lock(&Impl->Mutex);
	FSQLitePreparedStatement Delete = Impl->Database.PrepareStatement(
		TEXT("DELETE FROM guild_members WHERE guild_id=? AND player_id=?;"));
	FSQLitePreparedStatement Player = Impl->Database.PrepareStatement(
		TEXT("UPDATE players SET guild_id=0,guild_name='' WHERE player_id=?;"));
	return Delete.IsValid() && Player.IsValid() && Delete.SetBindingValueByIndex(1, GuildId) &&
		Delete.SetBindingValueByIndex(2, CharacterId) && Delete.Execute() &&
		Player.SetBindingValueByIndex(1, CharacterId) && Player.Execute()
		? EMT2GuildResult::Success : EMT2GuildResult::Unavailable;
}

EMT2GuildResult FMT2PersistenceBackend::SetGuildMemberRank(
	int32 GuildId, const FString& CharacterId, int32 Rank)
{
	if (Rank < 2 || Rank > MT2Guild::RankCount) return EMT2GuildResult::InvalidRank;
	FScopeLock Lock(&Impl->Mutex);
	FSQLitePreparedStatement Statement = Impl->Database.PrepareStatement(
		TEXT("UPDATE guild_members SET rank=? WHERE guild_id=? AND player_id=?;"));
	return Statement.IsValid() && Statement.SetBindingValueByIndex(1, Rank) &&
		Statement.SetBindingValueByIndex(2, GuildId) && Statement.SetBindingValueByIndex(3, CharacterId) &&
		Statement.Execute() ? EMT2GuildResult::Success : EMT2GuildResult::Unavailable;
}

EMT2GuildResult FMT2PersistenceBackend::SetGuildRank(
	int32 GuildId, int32 Rank, const FString& Name, int32 Permissions)
{
	if (Rank < 2 || Rank > MT2Guild::RankCount || Name.TrimStartAndEnd().IsEmpty())
		return EMT2GuildResult::InvalidRank;
	FScopeLock Lock(&Impl->Mutex);
	FSQLitePreparedStatement Statement = Impl->Database.PrepareStatement(
		TEXT("UPDATE guild_ranks SET name=?,permissions=? WHERE guild_id=? AND rank=?;"));
	return Statement.IsValid() && Statement.SetBindingValueByIndex(1, Name.TrimStartAndEnd().Left(8)) &&
		Statement.SetBindingValueByIndex(2, Permissions & 15) && Statement.SetBindingValueByIndex(3, GuildId) &&
		Statement.SetBindingValueByIndex(4, Rank) && Statement.Execute()
		? EMT2GuildResult::Success : EMT2GuildResult::Unavailable;
}

EMT2GuildResult FMT2PersistenceBackend::DisbandGuild(int32 GuildId)
{
	FScopeLock Lock(&Impl->Mutex);
	FString Error;
	if (!Impl->Execute(TEXT("BEGIN IMMEDIATE;"), Error)) return EMT2GuildResult::Unavailable;
	FSQLitePreparedStatement Clear = Impl->Database.PrepareStatement(
		TEXT("UPDATE players SET guild_id=0,guild_name='' WHERE player_id IN ")
		TEXT("(SELECT player_id FROM guild_members WHERE guild_id=?);"));
	FSQLitePreparedStatement Delete = Impl->Database.PrepareStatement(
		TEXT("DELETE FROM guilds WHERE guild_id=?;"));
	if (!Clear.IsValid() || !Delete.IsValid() || !Clear.SetBindingValueByIndex(1, GuildId) ||
		!Clear.Execute() || !Delete.SetBindingValueByIndex(1, GuildId) || !Delete.Execute() ||
		!Impl->Execute(TEXT("COMMIT;"), Error))
	{
		Impl->Database.Execute(TEXT("ROLLBACK;"));
		return EMT2GuildResult::Unavailable;
	}
	return EMT2GuildResult::Success;
}
