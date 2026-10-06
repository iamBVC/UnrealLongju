/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Config/MT2PathSettings.h"
#include "Misc/ScopeExit.h"
#include "Abilities/MT2CoreAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Characters/MT2PlayerCharacter.h"
#include "Components/MT2HealthComponent.h"
#include "Components/MT2ManaComponent.h"
#include "Engine/World.h"
#include "Items/MT2InventoryComponent.h"
#include "Items/MT2ItemTemplate.h"
#include "Player/MT2PlayerState.h"
#include "Quests/MT2Quest.h"
#include "Quests/MT2QuestExpression.h"
#include "Quests/MT2QuestLuaPattern.h"
#include "Quests/MT2QuestEntitySubsystem.h"
#include "Quests/MT2QuestManagerComponent.h"
#include "Quests/MT2QuestNode.h"
#include "Quests/MT2QuestComponent.h"
#include "Mobs/MT2Mob.h"
#include "World/MT2MapPresentationActor.h"
#include "Persistence/MT2PersistenceBackend.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2QuestResultListTest, "Metin2.Quests.PlayerApi.ResultLists",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMT2QuestResultListTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AMT2PlayerState* State = World->SpawnActor<AMT2PlayerState>();
	if (!TestNotNull(TEXT("State"), State)) { return false; }
	UMT2QuestManagerComponent* Manager = State->GetQuestManagerComponent();
	FMT2QuestContext Context; Context.PlayerState = State; Context.Manager = Manager;
	auto List = [&](const TCHAR* Expression)
	{
		bool bOk = false; const auto Values = FMT2QuestExpression::EvaluateList({Expression}, Context, bOk);
		TestTrue(FString(TEXT("Result expression evaluates: ")) + Expression, bOk); return Values;
	};
	auto First = [&](const TCHAR* Expression)
	{
		const auto Values = List(Expression);
		if (Values.IsEmpty()) { AddError(TEXT("Expected at least one result")); return FMT2QuestValue(); }
		return Values[0];
	};
	auto Find = List(TEXT("string.find('2026-10-05 12:34', '(%d+)-(%d+)-(%d+) (%d+):(%d+)')"));
	if (TestEqual(TEXT("Find returns boundaries and five captures"), Find.Num(), 7))
	{
		TestEqual(TEXT("Find start"), Find[0].Number, 1.0); TestEqual(TEXT("Find end"), Find[1].Number, 16.0);
		TestEqual(TEXT("Year capture"), Find[2].Text, FString(TEXT("2026"))); TestEqual(TEXT("Minute capture"), Find[6].Text, FString(TEXT("34")));
	}
	const auto NotFound = List(TEXT("string.find('abc', 'z')"));
	TestTrue(TEXT("No match is one nil result"), NotFound.Num() == 1 && NotFound[0].bIsNil);
	const auto Empty = List(TEXT("string.find('abc', '', 99)"));
	TestTrue(TEXT("Oversized init clamps to end for empty search"), Empty.Num() == 2 && Empty[0].Number == 4 && Empty[1].Number == 3);
	const auto Relative = List(TEXT("string.find('a.b.c', '.', -3, 0)"));
	TestTrue(TEXT("Lua zero is truthy plain mode; negative init is relative"), Relative.Num() == 2 && Relative[0].Number == 4);
	const auto Position = List(TEXT("string.find('éa', '()a')"));
	TestTrue(TEXT("Find positions are UTF-8 bytes"), Position.Num() == 3 && Position[0].Number == 3 && Position[1].Number == 3 && Position[2].Number == 3);
	TestEqual(TEXT("Parentheses force one result"), List(TEXT("(string.gsub('aaa', 'a', 'b'))")).Num(), 1);
	TestEqual(TEXT("Logical operator forces one result"), List(TEXT("true and string.gsub('aaa', 'a', 'b')")).Num(), 1);
	const auto Sub = List(TEXT("string.gsub('aaa', 'a', 'b')"));
	TestTrue(TEXT("Substitution returns text and count"), Sub.Num() == 2 && Sub[0].Text == TEXT("bbb") && Sub[1].Number == 3);
	TestEqual(TEXT("Last call expands arguments once"), First(TEXT("string.format('%s:%d', string.gsub('aaa', 'a', 'b'))")).Text, FString(TEXT("bbb:3")));
	TestEqual(TEXT("Non-last call is scalar"), First(TEXT("string.format('%s:%d', string.gsub('aaa', 'a', 'b'), 9)")).Text, FString(TEXT("bbb:9")));
	auto Table = First(TEXT("{10, string.gsub('aaa', 'a', 'b')}"));
	TestEqual(TEXT("Last array field expands"), Table.GetTableLength(), 3);
	TestEqual(TEXT("Substitution count in array"), Table.GetTableValue(FMT2QuestValue(3.0)).Number, 3.0);
	TestFalse(TEXT("Table elements retain no result metadata"), Table.GetTableValue(FMT2QuestValue(2.0)).CallResults.IsValid());
	TestEqual(TEXT("Named field does not expand"), First(TEXT("{x = string.gsub('a','a','b')}")).GetTableLength(), 0);
	TestEqual(TEXT("Parenthesized last field does not expand"), First(TEXT("{(string.gsub('a','a','b'))}")).GetTableLength(), 1);
	bool bStoredOk = false;
	Manager->SetScriptVariable(TEXT("stored"), FMT2QuestExpression::Evaluate(TEXT("string.gsub('a','a','b')"), Context, bStoredOk));
	TestTrue(TEXT("Stored call evaluates"), bStoredOk);
	TestEqual(TEXT("Stored scalar never re-expands"), List(TEXT("stored")).Num(), 1);
	TestEqual(TEXT("Verified no-return API yields zero list results"), List(TEXT("pc.change_alignment(0)")).Num(), 0);
	UMT2QuestNode_AssignValues* Node = NewObject<UMT2QuestNode_AssignValues>(Manager);
	Node->VariableNames = {TEXT("a"), TEXT("b"), TEXT("c")}; Node->QuestScopedTargets = {false, false, false};
	Manager->SetScriptVariable(TEXT("a"), FMT2QuestValue(1.0)); Manager->SetScriptVariable(TEXT("b"), FMT2QuestValue(2.0));
	Node->Expressions = {TEXT("b"), TEXT("a")};
	TestEqual(TEXT("Atomic RHS capture executes"), Node->Execute(Context), EMT2QuestNodeResult::Continue);
	TestEqual(TEXT("Swap first value"), Manager->GetScriptVariable(TEXT("a")).Number, 2.0);
	TestEqual(TEXT("Swap second value"), Manager->GetScriptVariable(TEXT("b")).Number, 1.0);
	TestTrue(TEXT("Missing result pads nil"), Manager->GetScriptVariable(TEXT("c")).bIsNil);
	Node->Expressions = {TEXT("10"), TEXT("string.format('%d', nil)")};
	TestEqual(TEXT("Invalid RHS aborts writes"), Node->Execute(Context), EMT2QuestNodeResult::Stop);
	TestEqual(TEXT("Earlier target unchanged on failure"), Manager->GetScriptVariable(TEXT("a")).Number, 2.0);
	bool bOk = true;
	FMT2QuestExpression::EvaluateList({TEXT("pc.change_alignment(10)"), TEXT("1 trailing")}, Context, bOk);
	TestFalse(TEXT("Entire syntax is validated before side effects"), bOk);
	TestEqual(TEXT("Malformed list performs no native mutation"), State->GetRawAlignment(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2QuestPatternApiTest, "Metin2.Quests.PlayerApi.Patterns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMT2QuestPatternApiTest::RunTest(const FString& Parameters)
{
	auto Check = [&](const TCHAR* Source, const TCHAR* Pattern, const TCHAR* Replacement, const TCHAR* Expected, int32 Count, int32 Maximum = MAX_int32)
	{
		FString Text, Error; int32 ActualCount = 0;
		TestTrue(FString(TEXT("Pattern succeeds: ")) + Pattern, MT2QuestLuaPattern::Substitute(Source, Pattern, Replacement, Maximum, Text, ActualCount, Error));
		TestEqual(TEXT("Replaced bytes"), Text, FString(Expected)); TestEqual(TEXT("Substitution count"), ActualCount, Count);
	};
	Check(TEXT("HeLLo \t WoRLD 123!"), TEXT("(%a*)%s*"), TEXT("%1"), TEXT("HeLLoWoRLD123!"), 7);
	Check(TEXT("abc"), TEXT(""), TEXT("_"), TEXT("_a_b_c_"), 4);
	Check(TEXT(""), TEXT(""), TEXT("x"), TEXT("x"), 1);
	Check(TEXT("abc abc"), TEXT("^abc"), TEXT("x"), TEXT("x abc"), 1);
	Check(TEXT("abc abc"), TEXT("abc$"), TEXT("x"), TEXT("abc x"), 1);
	Check(TEXT("aaaa"), TEXT("a*"), TEXT("x"), TEXT("xx"), 2);
	Check(TEXT("aaaa"), TEXT("a+"), TEXT("x"), TEXT("x"), 1);
	Check(TEXT("a1b2c3"), TEXT("%d"), TEXT("x"), TEXT("axbxc3"), 2, 2);
	Check(TEXT("abc"), TEXT("%"), TEXT("x"), TEXT("abc"), 0, 0);
	Check(TEXT("ab ac abc"), TEXT("ab?c"), TEXT("x"), TEXT("ab x x"), 2);
	Check(TEXT("<a><b>"), TEXT("<.->"), TEXT("x"), TEXT("xx"), 2);
	Check(TEXT("a(b(c)d)e"), TEXT("%b()"), TEXT("x"), TEXT("axe"), 1);
	Check(TEXT("foo food foo"), TEXT("%f[%a]foo%f[%A]"), TEXT("x"), TEXT("x food x"), 2);
	Check(TEXT("abc123"), TEXT("([a-c]+)(%d+)"), TEXT("%2:%1"), TEXT("123:abc"), 1);
	Check(TEXT("aa bb ab"), TEXT("(%a)%1"), TEXT("x"), TEXT("x x ab"), 2);
	Check(TEXT("abc"), TEXT("()b"), TEXT("%1"), TEXT("a2c"), 1);
	Check(TEXT("a.b"), TEXT("%."), TEXT("%%"), TEXT("a%b"), 1);
	Check(TEXT("a1!"), TEXT("[^%a%d]"), TEXT("%q"), TEXT("a1q"), 1);
	Check(TEXT("]a]"), TEXT("[]]"), TEXT("x"), TEXT("xax"), 2);
	Check(TEXT("Ä a É"), TEXT("%a"), TEXT("x"), TEXT("Ä x É"), 1);
	for (const TCHAR* Pattern : {TEXT("%"), TEXT("["), TEXT("%f"), TEXT("%b("), TEXT(")"), TEXT("%0")})
	{
		FString Text, Error; int32 Count = 0;
		TestFalse(TEXT("Malformed reached pattern fails"), MT2QuestLuaPattern::Substitute(TEXT("abc"), Pattern, TEXT("x"), 10, Text, Count, Error));
		TestFalse(TEXT("Failure has a diagnostic"), Error.IsEmpty());
		TestEqual(TEXT("Failure does not publish partial count"), Count, 0);
	}
	for (const TCHAR* Replacement : {TEXT("%0"), TEXT("%2")})
	{
		FString Text, Error; int32 Count = 0;
		TestFalse(TEXT("Lua 5.0 replacement capture range enforced"), MT2QuestLuaPattern::Substitute(TEXT("abc"), TEXT("(%a+)"), Replacement, 10, Text, Count, Error));
	}
	FString Text, Error; int32 Count = 0;
	TestFalse(TEXT("Backtracking work is bounded"), MT2QuestLuaPattern::Substitute(FString::ChrN(100, 'a'), TEXT("a*a*a*a*a*a*b"), TEXT("x"), 100, Text, Count, Error));
	TestFalse(TEXT("Large bracket scans are included in the work budget"), MT2QuestLuaPattern::Substitute(FString::ChrN(1000, 'x'),
		TEXT("[") + FString::ChrN(2000, 'a') + TEXT("x]*"), TEXT("x"), 1001, Text, Count, Error));
	FString Aliased = TEXT("abc");
	TestTrue(TEXT("In-place replacement snapshots the input"), MT2QuestLuaPattern::Substitute(Aliased, TEXT("b"), TEXT("x"), 10, Aliased, Count, Error));
	TestEqual(TEXT("In-place replacement result"), Aliased, FString(TEXT("axc")));
	FMT2QuestContext Context; bool bOk = false;
	const auto Value = FMT2QuestExpression::Evaluate(TEXT("string.lower(string.gsub('HeLLo  HERO', '(%a*)%s*', '%1'))"), Context, bOk);
	TestTrue(TEXT("Actual confirmation normalization evaluates"), bOk);
	TestEqual(TEXT("Actual normalized text"), Value.Text, FString(TEXT("hellohero")));
	FMT2QuestExpression::Evaluate(TEXT("string.gsub('abc', 'a', {})"), Context, bOk);
	TestFalse(TEXT("Lua 5.0 does not support table replacements"), bOk);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2QuestStringApiTest, "Metin2.Quests.PlayerApi.Strings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMT2QuestStringApiTest::RunTest(const FString& Parameters)
{
	FMT2QuestContext Context;
	auto Read = [&](const TCHAR* Source)
	{
		FString Reason;
		TestTrue(TEXT("String expression supported"), FMT2QuestExpression::IsSupported(Source, Reason));
		bool bOk = false;
		const FMT2QuestValue Value = FMT2QuestExpression::Evaluate(Source, Context, bOk);
		TestTrue(TEXT("String expression executes"), bOk);
		return Value;
	};
	TestEqual(TEXT("Formatting substitutes ordered values, not template"),
		Read(TEXT("string.format('%s:%04d:%.2f:%%', 'hero', 12.9, 1.256)")).Text, FString(TEXT("hero:0012:1.26:%")));
	TestEqual(TEXT("Integer bases and signs"), Read(TEXT("string.format('%d %i %o %u %x %X', -2.9, '3.8', 8, -1, 255, 255)")).Text,
		FString(TEXT("-2 3 10 4294967295 ff FF")));
	TestEqual(TEXT("Floating conversions"), Read(TEXT("string.format('%.1e %.1E %.3g %.3G', 1.25, 1.25, 1.25, 1.25)")).Text,
		FString(TEXT("1.2e+00 1.2E+00 1.25 1.25")));
	TestEqual(TEXT("Flags and precision"), Read(TEXT("string.format('%+06d|%-5.3s|%#x', 42, 'abcdef', 16)")).Text,
		FString(TEXT("+00042|abc  |0x10")));
	TestEqual(TEXT("Numeric string coercion"), Read(TEXT("string.format('%s', 12.5)")).Text, FString(TEXT("12.5")));
	TestEqual(TEXT("Percent without arguments"), Read(TEXT("string.format('100%%')")).Text, FString(TEXT("100%")));
	TestEqual(TEXT("Floating formatting preserves infinity instead of integer conversion"), Read(TEXT("string.format('%f', '1e999')")).Text, FString(TEXT("inf")));
	TestEqual(TEXT("Extra arguments are ignored after evaluation"), Read(TEXT("string.format('%d', 1, 99)")).Text, FString(TEXT("1")));
	TestEqual(TEXT("Character and NUL behavior"), Read(TEXT("string.format('%c%c!', 65, 0)")).Text, FString(TEXT("A!")));
	TestEqual(TEXT("Lua quoting escapes quote, slash and physical newline"), Read(TEXT("string.format('%q', 'a\\\"\\\\\\nb')")).Text,
		FString(TEXT("\"a\\\"\\\\\\\nb\"")));
	TestEqual(TEXT("Empty string format"), Read(TEXT("string.format('')")).Text, FString());
	TestEqual(TEXT("ASCII lower preserves non-ASCII bytes"), Read(TEXT("string.lower('HeLLo ÄÉ')")).Text, FString(TEXT("hello ÄÉ")));
	TestEqual(TEXT("ASCII upper preserves non-ASCII bytes"), Read(TEXT("string.upper('HeLLo äé')")).Text, FString(TEXT("HELLO äé")));
	TestEqual(TEXT("Byte length instead of UTF-16 length"), Read(TEXT("string.len('é🙂')")).Number, 6.0);
	TestEqual(TEXT("String length coerces numbers"), Read(TEXT("string.len(1234)")).Number, 4.0);
	for (const TCHAR* Invalid : {TEXT("string.format('%s')"), TEXT("string.format('%d', 'bad')"),
		TEXT("string.format('%n', 1)"), TEXT("string.format('%*d', 1)"), TEXT("string.format('%100d', 1)"),
		TEXT("string.format('%.100f', 1)"), TEXT("string.format('%2$s', 'a')"), TEXT("string.format('%')"),
		TEXT("string.format('%d', '1e40')"), TEXT("string.format('%s', true)"), TEXT("string.len(nil)"),
		TEXT("string.lower({})"), TEXT("string.format('%.1s', 'é')"), TEXT("string.format('%c', 233)")})
	{
		bool bOk = true; FMT2QuestExpression::Evaluate(Invalid, Context, bOk);
		TestFalse(FString(TEXT("Invalid or lossy conversion rejected: ")) + Invalid, bOk);
	}
	const FString Long = FString::ChrN(100, TEXT('a'));
	TestEqual(TEXT("Lua 5.0 long unprecisioned string bypasses formatting"),
		Read(*(TEXT("string.format('%99s', '") + Long + TEXT("')"))).Text, Long);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2QuestAlignmentApiTest, "Metin2.Quests.PlayerApi.Alignment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMT2QuestAlignmentApiTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AMT2PlayerState* State = World->SpawnActor<AMT2PlayerState>();
	AMT2PlayerCharacter* Player = World->SpawnActor<AMT2PlayerCharacter>();
	AMT2PlayerState* OtherState = World->SpawnActor<AMT2PlayerState>();
	AMT2PlayerCharacter* Other = World->SpawnActor<AMT2PlayerCharacter>();
	if (!State || !Player || !OtherState || !Other) { AddError(TEXT("Alignment actors missing")); return false; }
	Player->SetPlayerState(State); Other->SetPlayerState(OtherState);
	UAbilitySystemComponent* AbilitySystem = State->GetAbilitySystemComponent();
	AbilitySystem->AddAttributeSetSubobject(State->GetCoreAttributes());
	AbilitySystem->InitAbilityActorInfo(State, Player);
	FMT2QuestContext Context; Context.PlayerState = State; Context.Player = Player;
	auto Read = [&](const TCHAR* Source)
	{
		FString Unsupported;
		TestTrue(TEXT("Expression supported"), FMT2QuestExpression::IsSupported(Source, Unsupported));
		bool bOk = false;
		const FMT2QuestValue Value = FMT2QuestExpression::Evaluate(Source, Context, bOk);
		TestTrue(TEXT("Expression evaluates"), bOk);
		return Value;
	};
	TestTrue(TEXT("Mutation has no Lua return value"), Read(TEXT("pc.change_alignment(0.19)")).bIsNil);
	TestEqual(TEXT("Multiply before truncating"), State->GetRawAlignment(), 1);
	TestEqual(TEXT("Lua reads truncate tenth points"), Read(TEXT("pc.get_real_alignment()")).Number, 0.0);
	State->SetKarmaPoints(0);
	for (int32 Index = 0; Index < 10; ++Index) { Read(TEXT("pc.changealignment('0.1')")); }
	TestEqual(TEXT("Repeated fractional changes accumulate"), State->GetRawAlignment(), 10);
	TestEqual(TEXT("Public whole-point API stays compatible"), State->GetKarmaPoints(), 1);
	State->SetKarmaPoints(0); Read(TEXT("pc.change_alignment(-0.19)"));
	TestEqual(TEXT("Negative delta truncates toward zero"), State->GetRawAlignment(), -1);
	TestEqual(TEXT("Negative fractional Lua read truncates toward zero"), Read(TEXT("pc.get_real_alignment()")).Number, 0.0);
	TestTrue(TEXT("Fractional negative alignment is an authoritative PvP target"), Other->IsPvPEnabledAgainst(Player));
	State->ChangeKarmaPoints(-5);
	TestEqual(TEXT("Existing whole-point penalties preserve fractional remainder"), State->GetRawAlignment(), -51);
	State->SetKarmaPoints(-42);
	TArray<FMT2ItemSlot> Worn; Worn.SetNum(UMT2InventoryComponent::EquipmentCount);
	Worn[7].Vnum = 70048; Worn[7].Count = 1;
	Player->GetInventoryComponent()->RestoreItems({}, Worn);
	Player->HandleEquipmentChanged();
	TestEqual(TEXT("Hide-title equipment masks displayed alignment"), Read(TEXT("pc.get_alignment()")).Number, 0.0);
	TestEqual(TEXT("Masked real alignment remains available"), Read(TEXT("pc.get_real_alignment()")).Number, -42.0);
	TestEqual(TEXT("Nameplate getter is masked"), State->GetDisplayedKarmaPoints(), 0);
	TestTrue(TEXT("Masking never protects from negative-karma PvP"), Other->IsPvPEnabledAgainst(Player));
	Read(TEXT("pc.change_alignment(2.1)"));
	TestEqual(TEXT("Changes while masked still update real alignment"), State->GetRawAlignment(), -399);
	Player->GetInventoryComponent()->RestoreItems({}, {}); Player->HandleEquipmentChanged();
	TestEqual(TEXT("Unequip reveals current value, not a stale snapshot"), Read(TEXT("pc.get_alignment()")).Number, -39.0);
	Read(TEXT("pc.change_alignment(true)")); Read(TEXT("pc.change_alignment('bad')")); Read(TEXT("pc.change_alignment()"));
	TestEqual(TEXT("lua_tonumber invalid values yield zero delta"), State->GetRawAlignment(), -399);
	Read(TEXT("true or pc.change_alignment(999)"));
	TestEqual(TEXT("Short circuit skips alignment mutation"), State->GetRawAlignment(), -399);
	Read(TEXT("pc.change_alignment(1000000)"));
	TestEqual(TEXT("Quest mutations apply legacy positive clamp"), State->GetRawAlignment(), 200000);
	Read(TEXT("pc.change_alignment(-1000000)"));
	TestEqual(TEXT("Quest mutations apply legacy negative clamp"), State->GetRawAlignment(), -200000);
	bool bOk = true;
	FMT2QuestExpression::Evaluate(TEXT("pc.change_alignment(1e40)"), Context, bOk);
	TestFalse(TEXT("Unsafe integer conversion fails explicitly"), bOk);
	TestEqual(TEXT("Unsafe conversion does not change alignment"), State->GetRawAlignment(), -200000);
	FMT2QuestExpression::Evaluate(TEXT("pc.change_alignment(1) trailing"), Context, bOk);
	TestFalse(TEXT("Trailing syntax fails before mutation"), bOk);
	TestEqual(TEXT("Malformed expression does not change alignment"), State->GetRawAlignment(), -200000);
	State->SetRole(ROLE_SimulatedProxy);
	FMT2QuestExpression::Evaluate(TEXT("pc.change_alignment(1)"), Context, bOk);
	TestFalse(TEXT("Non-authoritative mutation rejected"), bOk);
	State->SetRole(ROLE_Authority);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2QuestAlignmentPersistenceTest, "Metin2.Quests.PlayerApi.AlignmentPersistence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMT2QuestAlignmentPersistenceTest::RunTest(const FString& Parameters)
{
	FMT2PersistenceBackendConfig Config;
	Config.RootDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/QuestAlignment") / FGuid::NewGuid().ToString(EGuidFormats::Digits));
	TSharedPtr<FMT2PersistenceBackend, ESPMode::ThreadSafe> Backend;
	ON_SCOPE_EXIT { Backend.Reset(); IFileManager::Get().DeleteDirectory(*Config.RootDirectory, false, true); };
	auto Open = [&]()
	{
		Backend = MakeShared<FMT2PersistenceBackend, ESPMode::ThreadSafe>(Config);
		FString Error;
		return TestTrue(FString(TEXT("SQLite opens: ")) + Error, Backend->Initialize(Error));
	};
	if (!Open()) { return false; }
	const auto Account = Backend->RegisterAccount(TEXT("alignment_fixture"), TEXT("fixture-salt"), TEXT("fixture-hash"));
	if (!TestTrue(TEXT("Fixture account created"), Account.bSucceeded)) { return false; }
	const auto Character = Backend->CreateCharacter(Account.AccountId, TEXT("AlignmentFixture"), FMT2CharacterAppearance(), EMT2Empire::Shinsoo, TEXT("yongan"));
	if (!TestTrue(TEXT("Fixture character created"), Character.bSucceeded)) { return false; }
	FMT2PersistenceLoadResult Loaded = Backend->Load(TEXT("player"), Character.Character.CharacterId);
	if (!TestTrue(TEXT("Fixture character loads"), Loaded.bSucceeded && Loaded.bFound)) { return false; }
	auto SaveKarma = [&](double Value)
	{
		TSharedPtr<FJsonObject> Root;
		if (!TestTrue(TEXT("Player JSON parses"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Loaded.Record.PayloadJson), Root))) { return false; }
		Root->SetNumberField(TEXT("karma"), Value);
		FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&Loaded.Record.PayloadJson));
		return TestTrue(TEXT("SQLite saves alignment"), Backend->Save(Loaded.Record).bSucceeded);
	};
	if (!SaveKarma(-123.0)) { return false; }
	Backend.Reset(); if (!Open()) { return false; }
	Loaded = Backend->Load(TEXT("player"), Character.Character.CharacterId);
	TSharedPtr<FJsonObject> Root;
	if (!TestTrue(TEXT("Whole-point save reloads"), Loaded.bSucceeded && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Loaded.Record.PayloadJson), Root))) { return false; }
	TestEqual(TEXT("Old whole-point units unchanged"), Root->GetNumberField(TEXT("karma")), -123.0);
	if (!SaveKarma(-0.1)) { return false; }
	Backend.Reset(); if (!Open()) { return false; }
	Loaded = Backend->Load(TEXT("player"), Character.Character.CharacterId);
	Root.Reset();
	if (!TestTrue(TEXT("Fractional save reloads"), Loaded.bSucceeded && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Loaded.Record.PayloadJson), Root))) { return false; }
	TestEqual(TEXT("Existing INTEGER-affinity column preserves fractional value"), Root->GetNumberField(TEXT("karma")), -0.1);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Restore world"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AMT2PlayerState* State = World->SpawnActor<AMT2PlayerState>();
	if (!TestNotNull(TEXT("Restored state"), State)) { return false; }
	State->GetAbilitySystemComponent()->AddAttributeSetSubobject(State->GetCoreAttributes());
	State->GetAbilitySystemComponent()->InitAbilityActorInfo(State, State);
	TestTrue(TEXT("Player applies relational JSON snapshot"), State->ApplyPersistentStateJson_Implementation(Loaded.Record.PayloadJson));
	TestEqual(TEXT("Fraction restores as exact raw tenth point"), State->GetRawAlignment(), -1);
	State->SetAlignmentHidden(true);
	TSharedPtr<FJsonObject> Captured;
	TestTrue(TEXT("Player capture JSON parses"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(State->CapturePersistentStateJson_Implementation()), Captured));
	if (Captured) { TestEqual(TEXT("Capture saves real fractional value while masked"), Captured->GetNumberField(TEXT("karma")), -0.1); }
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2QuestPlayerResourcesTest,
	"Metin2.Quests.PlayerApi.Resources",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMT2QuestPlayerResourcesTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Test world"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	AMT2PlayerCharacter* Player = World->SpawnActor<AMT2PlayerCharacter>();
	AMT2PlayerState* State = World->SpawnActor<AMT2PlayerState>();
	if (!TestNotNull(TEXT("Player"), Player) || !TestNotNull(TEXT("State"), State)) { return false; }
	UAbilitySystemComponent* AbilitySystem = State->GetAbilitySystemComponent();
	AbilitySystem->AddAttributeSetSubobject(State->GetCoreAttributes());
	AbilitySystem->InitAbilityActorInfo(State, Player);
	UMT2HealthComponent* Health = Player->GetHealthComponent();
	UMT2ManaComponent* Mana = Player->GetManaComponent();
	Health->InitializeWithAbilitySystem(AbilitySystem);
	Mana->InitializeWithAbilitySystem(AbilitySystem);
	ON_SCOPE_EXIT
	{
		Health->UninitializeFromAbilitySystem();
		Mana->UninitializeFromAbilitySystem();
	};
	AbilitySystem->SetNumericAttributeBase(UMT2CoreAttributeSet::GetMaxHealthAttribute(), 1200.0f);
	AbilitySystem->SetNumericAttributeBase(UMT2CoreAttributeSet::GetHealthAttribute(), 345.0f);
	AbilitySystem->SetNumericAttributeBase(UMT2CoreAttributeSet::GetMaxManaAttribute(), 800.0f);
	AbilitySystem->SetNumericAttributeBase(UMT2CoreAttributeSet::GetManaAttribute(), 234.0f);

	FMT2QuestContext Context;
	Context.Player = Player;
	Context.PlayerState = State;
	auto CheckRead = [&](const TCHAR* Expression, double Expected)
	{
		FString Unsupported;
		TestTrue(FString::Printf(TEXT("Supported: %s"), Expression),
			FMT2QuestExpression::IsSupported(Expression, Unsupported));
		bool bOk = false;
		const FMT2QuestValue Value = FMT2QuestExpression::Evaluate(Expression, Context, bOk);
		TestTrue(FString::Printf(TEXT("Evaluated: %s"), Expression), bOk);
		TestEqual(FString(Expression), Value.Number, Expected);
	};
	for (const TCHAR* Name : {TEXT("pc.get_hp()"), TEXT("pc.gethp()"), TEXT("pc.hp")})
	{
		CheckRead(Name, 345.0);
	}
	for (const TCHAR* Name : {TEXT("pc.get_max_hp()"), TEXT("pc.getmaxhp()"), TEXT("pc.maxhp")})
	{
		CheckRead(Name, 1200.0);
	}
	for (const TCHAR* Name : {TEXT("pc.get_sp()"), TEXT("pc.getsp()"), TEXT("pc.sp")})
	{
		CheckRead(Name, 234.0);
	}
	for (const TCHAR* Name : {TEXT("pc.get_max_sp()"), TEXT("pc.getmaxsp()"), TEXT("pc.maxsp")})
	{
		CheckRead(Name, 800.0);
	}
	CheckRead(TEXT("pc.is_dead()"), 0.0);
	AbilitySystem->SetNumericAttributeBase(UMT2CoreAttributeSet::GetHealthAttribute(), 0.0f);
	CheckRead(TEXT("pc.is_dead()"), 1.0);
	Context.Player = nullptr;
	CheckRead(TEXT("pc.get_hp()"), 0.0);
	CheckRead(TEXT("pc.get_max_sp()"), 0.0);
	CheckRead(TEXT("pc.is_dead()"), 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2QuestCooldownTest,
	"Metin2.Quests.PlayerApi.Cooldown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMT2QuestCooldownTest::RunTest(const FString& Parameters)
{
	UMT2QuestManagerComponent* Manager = NewObject<UMT2QuestManagerComponent>();
	FMT2QuestContext Context;
	Context.Manager = Manager;
	Context.Quest = GetDefault<UMT2Quest>();
	const FName FlagName(TEXT("__NEXT_TIME__"));
	const int64 Before = FDateTime::UtcNow().ToUnixTimestamp();
	bool bOk = false;
	const double Time = FMT2QuestExpression::Evaluate(TEXT("get_time()"), Context, bOk).Number;
	TestTrue(TEXT("get_time evaluates without a pawn/world"), bOk);
	TestTrue(TEXT("get_time uses Unix time, not world uptime"),
		Time >= Before && Time <= FDateTime::UtcNow().ToUnixTimestamp());
	FString Unsupported;
	TestTrue(TEXT("Cooldown predicate accepted by importer"),
		FMT2QuestExpression::IsSupported(TEXT("next_time_is_now()"), Unsupported));
	TestTrue(TEXT("Unset cooldown is ready"), FMT2QuestExpression::EvaluateBool(TEXT("next_time_is_now()"), Context));

	UMT2QuestNode_SetFlag* Flag = NewObject<UMT2QuestNode_SetFlag>();
	Flag->FlagName = FlagName;
	Flag->ValueExpression = TEXT("get_time() + (60)");
	Flag->Execute(Context);
	const int32 Deadline = Manager->GetQuestFlag(FlagName, Context.Quest);
	TestTrue(TEXT("Cooldown node writes Unix expiry"),
		Deadline >= Before + 60 && Deadline <= FDateTime::UtcNow().ToUnixTimestamp() + 60);
	TestFalse(TEXT("Future cooldown is not ready"), FMT2QuestExpression::EvaluateBool(TEXT("next_time_is_now()"), Context));
	TestEqual(TEXT("Cooldown is quest scoped"), Manager->GetQuestFlag(FlagName, nullptr), 0);
	Manager->SetQuestFlag(FlagName, static_cast<int32>(Before - 1), Context.Quest);
	TestTrue(TEXT("Expired cooldown is ready"), FMT2QuestExpression::EvaluateBool(TEXT("next_time_is_now()"), Context));
	Context.Manager = nullptr;
	FMT2QuestExpression::Evaluate(TEXT("next_time_is_now()"), Context, bOk);
	TestFalse(TEXT("Missing quest manager is not a successful read"), bOk);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2QuestIpairsTest,
	"Metin2.Quests.PlayerApi.Ipairs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMT2QuestIpairsTest::RunTest(const FString& Parameters)
{
	UMT2QuestManagerComponent* Manager = NewObject<UMT2QuestManagerComponent>();
	Manager->ActiveContext.Manager = Manager;
	FMT2QuestTable Table;
	Table.Nodes.SetNum(5);
	Table.Nodes[0].bIsTable = true;
	Table.Nodes[0].Children = {1, 3};
	for (int32 RowIndex : {1, 3})
	{
		Table.Nodes[RowIndex].bIsTable = true;
		Table.Nodes[RowIndex].NamedChildren.Add(TEXT("amount"), RowIndex + 1);
		Table.Nodes[RowIndex + 1].bIsNumber = true;
		Table.Nodes[RowIndex + 1].Number = RowIndex == 1 ? 2 : 3;
	}
	Manager->SetScriptVariable(TEXT("sequence"), FMT2QuestValue(&Table, 0));
	Manager->SetScriptVariable(TEXT("i"), FMT2QuestValue(77.0));
	Manager->SetScriptVariable(TEXT("row"), FMT2QuestValue(FString(TEXT("outer"))));
	UMT2QuestNode_ForEach* Loop = NewObject<UMT2QuestNode_ForEach>();
	Loop->TableExpression = TEXT("sequence");
	Loop->IndexVariable = TEXT("i");
	Loop->ValueVariable = TEXT("row");
	UMT2QuestNode_SetFlag* Sum = NewObject<UMT2QuestNode_SetFlag>();
	Sum->FlagName = TEXT("sum");
	Sum->bAdd = true;
	Sum->ValueExpression = TEXT("row.amount + i");
	UMT2QuestNode_SetVariable* Escape = NewObject<UMT2QuestNode_SetVariable>();
	Escape->VariableName = TEXT("escaped");
	Escape->Expression = TEXT("row");
	Loop->Body = {Sum, Escape, NewObject<UMT2QuestNode_Wait>()};
	Manager->PushFrame({Loop});
	Manager->RunPendingFrames();
	TestEqual(TEXT("First iteration before suspend"), Manager->GetQuestFlag(TEXT("sum")), 3);
	TestEqual(TEXT("Index survives suspension"), Manager->GetScriptVariable(TEXT("i")).Number, 1.0);
	// Rebinding the source must not restart/re-evaluate ipairs on resume.
	Manager->SetScriptVariable(TEXT("sequence"), FMT2QuestValue(0.0));
	Manager->RunPendingFrames();
	TestEqual(TEXT("Second iteration after resume"), Manager->GetQuestFlag(TEXT("sum")), 8);
	Manager->RunPendingFrames();
	TestTrue(TEXT("Completed loop empties stack"), Manager->CallStack.IsEmpty());
	TestEqual(TEXT("Outer index restored"), Manager->GetScriptVariable(TEXT("i")).Number, 77.0);
	TestEqual(TEXT("Outer value restored"), Manager->GetScriptVariable(TEXT("row")).Text, FString(TEXT("outer")));
	bool bOk = false;
	const FMT2QuestValue Escaped = FMT2QuestExpression::Evaluate(TEXT("escaped.amount"), Manager->ActiveContext, bOk);
	TestTrue(TEXT("Escaped table remains readable after frame teardown"), bOk);
	TestEqual(TEXT("Escaped row value"), Escaped.Number, 3.0);

	// Nested loops shadow the same iterator names and restore the outer iteration's values.
	Manager->SetScriptVariable(TEXT("sequence"), FMT2QuestValue(&Table, 0));
	UMT2QuestNode_ForEach* Inner = NewObject<UMT2QuestNode_ForEach>();
	Inner->TableExpression = TEXT("sequence");
	Inner->IndexVariable = TEXT("i");
	Inner->ValueVariable = TEXT("row");
	Inner->Body = {Sum};
	UMT2QuestNode_Say* Say = NewObject<UMT2QuestNode_Say>();
	Say->Lines = {FText::FromString(TEXT("Amount {=row.amount}"))};
	Loop->Body = {Inner, Sum, Say, NewObject<UMT2QuestNode_Wait>()};
	Manager->SetQuestFlag(TEXT("sum"), 0);
	Manager->PushFrame({Loop});
	Manager->RunPendingFrames();
	TestEqual(TEXT("Dialog captures first row at say time"), Manager->PendingLines.Last().ToString(), FString(TEXT("Amount 2")));
	Manager->RunPendingFrames();
	TestEqual(TEXT("Dialog captures second row at say time"), Manager->PendingLines.Last().ToString(), FString(TEXT("Amount 3")));
	Manager->RunPendingFrames();
	TestEqual(TEXT("Nested loops restore outer row/index"), Manager->GetQuestFlag(TEXT("sum")), 24);

	// ipairs stops at the first hole; numeric zero is a value, not a missing key.
	FMT2QuestTable Sparse;
	Sparse.Nodes.SetNum(3);
	Sparse.Nodes[0].bIsTable = true;
	Sparse.Nodes[0].Children = {1, INDEX_NONE, 2};
	Sparse.Nodes[1].bIsNumber = Sparse.Nodes[2].bIsNumber = true;
	Sparse.Nodes[2].Number = 100;
	Manager->SetScriptVariable(TEXT("sequence"), FMT2QuestValue(&Sparse, 0));
	Sum->ValueExpression = TEXT("1");
	Loop->Body = {Sum};
	Manager->SetQuestFlag(TEXT("sum"), 0);
	Manager->PushFrame({Loop});
	Manager->RunPendingFrames();
	TestEqual(TEXT("Zero iterated but hole terminates sequence"), Manager->GetQuestFlag(TEXT("sum")), 1);
	Sparse.Nodes[0].Children.Reset();
	Manager->PushFrame({Loop});
	Manager->RunPendingFrames();
	TestEqual(TEXT("Empty sequence runs no body"), Manager->GetQuestFlag(TEXT("sum")), 1);

	// An early function return unwinds the loop's lexical bindings as well as its body frame.
	Manager->SetScriptVariable(TEXT("sequence"), FMT2QuestValue(&Table, 0));
	UMT2QuestNode_Return* Return = NewObject<UMT2QuestNode_Return>();
	Return->ValueExpression = TEXT("row");
	Loop->Body = {Return};
	Manager->PushFunctionFrame({Loop}, TEXT("returned"), false);
	Manager->RunPendingFrames();
	TestEqual(TEXT("Return restores caller iterator"), Manager->GetScriptVariable(TEXT("i")).Number, 77.0);
	const FMT2QuestValue Returned = FMT2QuestExpression::Evaluate(TEXT("returned.amount"), Manager->ActiveContext, bOk);
	TestTrue(TEXT("Returned table owns its snapshot"), bOk);
	TestEqual(TEXT("Return exits on first row"), Returned.Number, 2.0);

	UMT2QuestNode_SetFlag* After = NewObject<UMT2QuestNode_SetFlag>();
	After->FlagName = TEXT("after");
	After->Value = 1;
	// Break after a dialogue pause exits only the nearest loop, restoring iterator locals.
	UMT2QuestNode_Break* Break = NewObject<UMT2QuestNode_Break>();
	Loop->Body = {NewObject<UMT2QuestNode_Wait>(), Break, Sum};
	Manager->SetQuestFlag(TEXT("sum"), 0);
	Manager->PushFrame({Loop, After});
	Manager->RunPendingFrames();
	TestEqual(TEXT("Break test suspended within first row"), Manager->GetScriptVariable(TEXT("i")).Number, 1.0);
	Manager->RunPendingFrames();
	TestEqual(TEXT("Break skips remaining body and rows"), Manager->GetQuestFlag(TEXT("sum")), 0);
	TestEqual(TEXT("Break continues after loop"), Manager->GetQuestFlag(TEXT("after")), 1);
	TestEqual(TEXT("Break restores iterator local"), Manager->GetScriptVariable(TEXT("i")).Number, 77.0);
	Manager->SetQuestFlag(TEXT("after"), 0);

	Inner->Body = {Break, Sum};
	Loop->Body = {Inner, Sum};
	Manager->PushFrame({Loop});
	Manager->RunPendingFrames();
	TestEqual(TEXT("Nested break preserves outer iterations"), Manager->GetQuestFlag(TEXT("sum")), 2);
	Manager->SetQuestFlag(TEXT("sum"), 0);

	UMT2QuestNode_While* While = NewObject<UMT2QuestNode_While>();
	While->ConditionExpression = TEXT("pc.getqf('sum') < 3");
	While->Body = {Sum};
	Manager->PushFrame({While});
	Manager->RunPendingFrames();
	TestEqual(TEXT("While repeats in retained frame"), Manager->GetQuestFlag(TEXT("sum")), 3);
	While->ConditionExpression = TEXT("pc.getqf('sum') > 0");
	While->bExecuteBodyFirst = true;
	While->Body = {Break, Sum};
	Manager->SetQuestFlag(TEXT("sum"), 0);
	Manager->PushFrame({While, After});
	Manager->RunPendingFrames();
	TestEqual(TEXT("Repeat executes and breaks with initially false condition"), Manager->GetQuestFlag(TEXT("after")), 1);
	TestEqual(TEXT("Repeat break skips rest of body"), Manager->GetQuestFlag(TEXT("sum")), 0);
	Manager->SetQuestFlag(TEXT("after"), 0);

	// A function called inside a loop cannot break its caller's loop.
	TestTrue(TEXT("Function-boundary setup"), Manager->PushIpairsFrame({}, FMT2QuestValue(&Table, 0), TEXT("i"), TEXT("row"), 10));
	Manager->PushFunctionFrame({Break}, NAME_None, false);
	AddExpectedError(TEXT("Quest break executed outside a loop"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Break respects function boundary"), Manager->BreakLoop());
	Manager->CallStack.Reset();
	Manager->SetScriptVariable(TEXT("i"), FMT2QuestValue(77.0));

	Loop->Body = {Sum};
	Loop->MaximumIterations = 1;
	AddExpectedError(TEXT("Quest ipairs exceeded 1 iterations"), EAutomationExpectedErrorFlags::Contains, 1);
	Manager->PushFrame({Loop, After});
	Manager->RunPendingFrames();
	TestEqual(TEXT("Iteration cap aborts block"), Manager->GetQuestFlag(TEXT("after")), 0);
	TestEqual(TEXT("Iteration cap restores outer local"), Manager->GetScriptVariable(TEXT("i")).Number, 77.0);

	UMT2QuestNode_Unconverted* Transaction = NewObject<UMT2QuestNode_Unconverted>();
	Transaction->bStopExecution = true;
	Manager->PushFrame({Transaction, After});
	Manager->RunPendingFrames();
	TestEqual(TEXT("Unsupported transaction cannot consume following materials"), Manager->GetQuestFlag(TEXT("after")), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2QuestTablesTest,
	"Metin2.Quests.PlayerApi.Tables",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMT2QuestTablesTest::RunTest(const FString& Parameters)
{
	UMT2QuestManagerComponent* Manager = NewObject<UMT2QuestManagerComponent>();
	Manager->ActiveContext.Manager = Manager;
	Manager->SetScriptVariable(TEXT("n"), FMT2QuestValue(7.0));
	bool bOk = false;
	const FMT2QuestValue Table = FMT2QuestExpression::Evaluate(
		TEXT("{ n + 1, [2] = 'two', named = { value = n * 2 } }"), Manager->ActiveContext, bOk);
	TestTrue(TEXT("Dynamic nested constructor evaluates"), bOk);
	Manager->SetScriptVariable(TEXT("t"), Table);
	Manager->SetScriptVariable(TEXT("alias"), Table);
	TestEqual(TEXT("Dynamic sequence value"), Table.GetTableValue(FMT2QuestValue(1.0)).Number, 8.0);
	TestEqual(TEXT("Mixed table length"), Table.GetTableLength(), 2);
	TestEqual(TEXT("Nested dynamic value"), FMT2QuestExpression::Evaluate(TEXT("t.named.value"), Manager->ActiveContext, bOk).Number, 14.0);
	TestTrue(TEXT("Nested indexing succeeded"), bOk);
	TestEqual(TEXT("Named field after bracket lookup"), FMT2QuestExpression::Evaluate(TEXT("t['named'].value"), Manager->ActiveContext, bOk).Number, 14.0);
	TestTrue(TEXT("Bracket then field lookup succeeded"), bOk);
	UMT2QuestNode_MutateTable* Write = NewObject<UMT2QuestNode_MutateTable>();
	Write->TableExpression = TEXT("alias.named");
	Write->KeyExpression = TEXT("'value'");
	Write->ValueExpression = TEXT("19");
	TestEqual(TEXT("Named mutation executes"), Write->Execute(Manager->ActiveContext), EMT2QuestNodeResult::Continue);
	TestEqual(TEXT("Aliases share nested identity"), FMT2QuestExpression::Evaluate(TEXT("t.named.value"), Manager->ActiveContext, bOk).Number, 19.0);
	TestTrue(TEXT("Nil deletes entry"), Table.SetTableValue(FMT2QuestValue(2.0), FMT2QuestValue()));
	TestEqual(TEXT("Deletion updates sequence length"), Table.GetTableLength(), 1);
	TestTrue(TEXT("Missing table key is nil"), FMT2QuestExpression::EvaluateBool(TEXT("t[2] == nil"), Manager->ActiveContext));
	TestTrue(TEXT("Numeric zero is not nil"), FMT2QuestExpression::EvaluateBool(TEXT("0 ~= nil"), Manager->ActiveContext));
	TestFalse(TEXT("Cycle rejected"), Table.SetTableValue(FMT2QuestValue(2.0), Table));
	TestEqual(TEXT("Rejected cycle leaves sequence unchanged"), Table.GetTableLength(), 1);

	Write->TableExpression = TEXT("t");
	Write->KeyExpression = TEXT("1");
	Write->ValueExpression = TEXT("23");
	Write->bInsert = true;
	TestEqual(TEXT("Insertion executes"), Write->Execute(Manager->ActiveContext), EMT2QuestNodeResult::Continue);
	TestEqual(TEXT("Insertion shifts old first element"), Table.GetTableValue(FMT2QuestValue(2.0)).Number, 8.0);
	Write->ValueExpression = TEXT("t");
	TestEqual(TEXT("Cyclic insertion rejected before shifting"), Write->Execute(Manager->ActiveContext), EMT2QuestNodeResult::Stop);
	TestEqual(TEXT("Rejected insertion preserves first element"), Table.GetTableValue(FMT2QuestValue(1.0)).Number, 23.0);
	TestEqual(TEXT("Rejected insertion preserves length"), Table.GetTableLength(), 2);

	UMT2QuestNode_SetVariable* Declare = NewObject<UMT2QuestNode_SetVariable>();
	Declare->VariableName = TEXT("fresh");
	Declare->bHasInlineTable = true;
	Declare->InlineTable.Nodes.SetNum(2);
	Declare->InlineTable.Nodes[0].bIsTable = true;
	Declare->InlineTable.Nodes[0].Children = {1};
	Declare->InlineTable.Nodes[1].bIsNumber = true;
	Declare->InlineTable.Nodes[1].Number = 10;
	Declare->Execute(Manager->ActiveContext);
	FMT2QuestValue First = Manager->GetScriptVariable(TEXT("fresh"));
	TestTrue(TEXT("Inline table is mutable private instance"), First.SetTableValue(FMT2QuestValue(1.0), FMT2QuestValue(99.0)));
	Declare->Execute(Manager->ActiveContext);
	TestEqual(TEXT("Repeated declaration creates fresh table"), Manager->GetScriptVariable(TEXT("fresh")).GetTableValue(FMT2QuestValue(1.0)).Number, 10.0);
	TestEqual(TEXT("Generated inline defaults not mutated"), Declare->InlineTable.Nodes[1].Number, 10.0);

	UMT2QuestNode_ForEach* Loop = NewObject<UMT2QuestNode_ForEach>();
	Loop->TableExpression = TEXT("t");
	Loop->IndexVariable = TEXT("i");
	Loop->ValueVariable = TEXT("v");
	UMT2QuestNode_SetFlag* Sum = NewObject<UMT2QuestNode_SetFlag>();
	Sum->FlagName = TEXT("table_sum");
	Sum->ValueExpression = TEXT("v");
	Sum->bAdd = true;
	Loop->Body = {Sum, NewObject<UMT2QuestNode_Wait>()};
	Manager->PushFrame({Loop});
	Manager->RunPendingFrames();
	TestEqual(TEXT("Mutable ipairs first value before pause"), Manager->GetQuestFlag(TEXT("table_sum")), 23);
	Table.SetTableValue(FMT2QuestValue(2.0), FMT2QuestValue(5.0));
	Manager->SetScriptVariable(TEXT("t"), FMT2QuestValue::NewTable());
	Manager->RunPendingFrames();
	TestEqual(TEXT("Ipairs retains table identity and sees aliased mutation"), Manager->GetQuestFlag(TEXT("table_sum")), 28);
	Manager->RunPendingFrames();
	TestTrue(TEXT("Mutable loop completes"), Manager->CallStack.IsEmpty());
	FString Reason;
	TestTrue(TEXT("Constructor syntax accepted"), FMT2QuestExpression::IsSupported(TEXT("{ value = n + 1, [2] = {} }"), Reason));
	TestFalse(TEXT("Malformed constructor rejected"), FMT2QuestExpression::IsSupported(TEXT("{ value = }"), Reason));
	TestEqual(TEXT("Numeric concatenation tokenizes separately"), FMT2QuestExpression::Evaluate(TEXT("1..2"), Manager->ActiveContext, bOk).Text,
		FString(TEXT("12")));
	TestTrue(TEXT("Concatenation evaluated"), bOk);
	TestEqual(TEXT("Escaped locale line break decoded"), FMT2QuestExpression::Evaluate(TEXT("'first\\nsecond'"), Manager->ActiveContext, bOk).Text,
		FString(TEXT("first\nsecond")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2QuestFunctionsTest,
	"Metin2.Quests.PlayerApi.Functions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMT2QuestFunctionsTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Test world"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AMT2PlayerState* State = World->SpawnActor<AMT2PlayerState>();
	UMT2QuestManagerComponent* Manager = State->GetQuestManagerComponent();
	FMT2QuestContext Context;
	Context.Manager = Manager;
	Context.PlayerState = State;
	Manager->ActiveContext = Context;
	Manager->SetScriptVariable(TEXT("a"), FMT2QuestValue(1.0));
	Manager->SetScriptVariable(TEXT("b"), FMT2QuestValue(2.0));
	auto MakeCall = [&](const TArray<TObjectPtr<UMT2QuestNode>>& Body)
	{
		UMT2QuestNode_CallFunction* Call = NewObject<UMT2QuestNode_CallFunction>();
		Call->FunctionName = TEXT("swap");
		Call->ParameterNames = {TEXT("a"), TEXT("b")};
		Call->ArgumentExpressions = {TEXT("b"), TEXT("a")};
		Call->ResultVariable = TEXT("result");
		Call->Body = Body;
		return Call;
	};
	UMT2QuestNode_Return* Return = NewObject<UMT2QuestNode_Return>();
	Return->ValueExpression = TEXT("a * 10 + b");
	UMT2QuestNode_CallFunction* Call = MakeCall({Return});
	Manager->PushFrame({Call});
	Manager->RunPendingFrames();
	TestEqual(TEXT("Arguments captured before parameters overwrite caller names"), Manager->GetScriptVariable(TEXT("result")).Number, 21.0);
	TestEqual(TEXT("Explicit return restores a"), Manager->GetScriptVariable(TEXT("a")).Number, 1.0);
	TestEqual(TEXT("Explicit return restores b"), Manager->GetScriptVariable(TEXT("b")).Number, 2.0);

	UMT2QuestNode_CallFunction* Inner = MakeCall({Return});
	Inner->ResultVariable = TEXT("inner");
	UMT2QuestNode_Return* OuterReturn = NewObject<UMT2QuestNode_Return>();
	OuterReturn->ValueExpression = TEXT("inner * 100 + a * 10 + b");
	Call->Body = {Inner, OuterReturn};
	Manager->PushFrame({Call});
	Manager->RunPendingFrames();
	TestEqual(TEXT("Nested call restores outer parameter scope"), Manager->GetScriptVariable(TEXT("result")).Number, 1221.0);
	TestEqual(TEXT("Nested return restores caller scope"), Manager->GetScriptVariable(TEXT("a")).Number, 1.0);

	Call->Body = {NewObject<UMT2QuestNode_Wait>(), Return};
	Manager->PushFrame({Call});
	Manager->RunPendingFrames();
	TestEqual(TEXT("Suspended function retains its parameter scope"), Manager->GetScriptVariable(TEXT("a")).Number, 2.0);
	State->GetQuestComponent()->ServerAnswerDialog_Implementation(1);
	TestEqual(TEXT("Resumed function result"), Manager->GetScriptVariable(TEXT("result")).Number, 21.0);
	TestEqual(TEXT("Resumed function restores caller scope"), Manager->GetScriptVariable(TEXT("a")).Number, 1.0);
	Manager->PushFrame({Call});
	Manager->RunPendingFrames();
	Manager->CancelConversation();
	TestEqual(TEXT("Cancellation restores parameters"), Manager->GetScriptVariable(TEXT("a")).Number, 1.0);

	UMT2QuestNode_SetVariable* Write = NewObject<UMT2QuestNode_SetVariable>();
	Write->VariableName = TEXT("a");
	Write->Expression = TEXT("99");
	Call->Body = {Write};
	Manager->PushFrame({Call});
	Manager->RunPendingFrames();
	TestTrue(TEXT("Implicit return is nil"), Manager->GetScriptVariable(TEXT("result")).bIsNil);
	TestEqual(TEXT("Implicit return restores modified parameter"), Manager->GetScriptVariable(TEXT("a")).Number, 1.0);
	Call->ParameterNames = {TEXT("absent")};
	Call->ArgumentExpressions = {TEXT("true")};
	Call->Body = {Return};
	Manager->PushFrame({Call});
	Manager->RunPendingFrames();
	TestFalse(TEXT("New parameter removed on unwind"), Manager->ScriptVariables.Contains(TEXT("absent")));

	Call = MakeCall({Return});
	Call->ResultVariable = TEXT("a");
	Manager->PushFrame({Call});
	Manager->RunPendingFrames();
	TestEqual(TEXT("Result publishes after restoring same-named parameter"), Manager->GetScriptVariable(TEXT("a")).Number, 21.0);
	Manager->SetScriptVariable(TEXT("a"), FMT2QuestValue(1.0));
	Call->ArgumentExpressions[1] = TEXT("1 / 0");
	TestEqual(TEXT("Invalid argument stops rather than silently passing nil"), Call->Execute(Context), EMT2QuestNodeResult::Stop);
	TestEqual(TEXT("Argument failure does not partially bind parameters"), Manager->GetScriptVariable(TEXT("a")).Number, 1.0);
	TestTrue(TEXT("Argument failure does not enter function"), Manager->CallStack.IsEmpty());
	Call->ArgumentExpressions[1] = TEXT("a");
	Call->ArgumentExpressions.Add(TEXT("1 / 0"));
	TestEqual(TEXT("Ignored extra argument still evaluates"), Call->Execute(Context), EMT2QuestNodeResult::Stop);
	TestTrue(TEXT("Extra argument failure does not enter function"), Manager->CallStack.IsEmpty());
	Call->ArgumentExpressions = {TEXT("b")};
	Return->ValueExpression = TEXT("b");
	Manager->PushFrame({Call});
	Manager->RunPendingFrames();
	TestTrue(TEXT("Omitted argument binds nil rather than caller's b"), Manager->GetScriptVariable(TEXT("a")).bIsNil);
	TestEqual(TEXT("Omitted parameter restores caller's b"), Manager->GetScriptVariable(TEXT("b")).Number, 2.0);
	Manager->SetScriptVariable(TEXT("a"), FMT2QuestValue(1.0));
	Call->ArgumentExpressions = {TEXT("b"), TEXT("a")};
	UMT2QuestNode_SetVariable* Fail = NewObject<UMT2QuestNode_SetVariable>();
	Fail->VariableName = TEXT("unused");
	Fail->Expression = TEXT("1 / 0");
	Call->Body = {Fail};
	Manager->PushFrame({Call});
	Manager->RunPendingFrames();
	TestEqual(TEXT("Runtime stop unwinds parameters"), Manager->GetScriptVariable(TEXT("a")).Number, 1.0);
	TestTrue(TEXT("Runtime failure terminates frames"), Manager->CallStack.IsEmpty());
	UMT2QuestNode_Return* Multiple = NewObject<UMT2QuestNode_Return>();
	Multiple->ValueExpressions = {TEXT("a"), TEXT("b"), TEXT("string.gsub('aaa', 'a', 'b')")};
	Call = MakeCall({NewObject<UMT2QuestNode_Wait>(), Multiple});
	Call->ResultVariable = NAME_None;
	Call->ResultVariables = {TEXT("a"), TEXT("second"), TEXT("text"), TEXT("count"), TEXT("padded")};
	Call->ResultQuestScopes = {false, false, false, false, false};
	Manager->SetScriptVariable(TEXT("second"), FMT2QuestValue(55.0));
	Manager->PushFrame({Call}); Manager->RunPendingFrames();
	TestEqual(TEXT("Suspended multi-result call has not published destinations"), Manager->GetScriptVariable(TEXT("second")).Number, 55.0);
	State->GetQuestComponent()->ServerAnswerDialog_Implementation(1);
	TestEqual(TEXT("Multiple results publish after same-named parameter restoration"), Manager->GetScriptVariable(TEXT("a")).Number, 2.0);
	TestEqual(TEXT("Second function result"), Manager->GetScriptVariable(TEXT("second")).Number, 1.0);
	TestEqual(TEXT("Final native return expands text"), Manager->GetScriptVariable(TEXT("text")).Text, FString(TEXT("bbb")));
	TestEqual(TEXT("Final native return expands count"), Manager->GetScriptVariable(TEXT("count")).Number, 3.0);
	TestTrue(TEXT("Extra function destination pads nil"), Manager->GetScriptVariable(TEXT("padded")).bIsNil);
	TestFalse(TEXT("Published return values do not keep tuple metadata"), Manager->GetScriptVariable(TEXT("text")).CallResults.IsValid());
	Manager->SetScriptVariable(TEXT("second"), FMT2QuestValue(77.0));
	Manager->PushFrame({Call}); Manager->RunPendingFrames(); Manager->CancelConversation();
	TestEqual(TEXT("Cancelled call does not publish return destinations"), Manager->GetScriptVariable(TEXT("second")).Number, 77.0);
	Call->Body = {Multiple};
	Call->ArgumentExpressions = {TEXT("string.gsub('aaa', 'a', 'b')")};
	Multiple->ValueExpressions = {TEXT("type(a)"), TEXT("b")};
	Manager->PushFrame({Call}); Manager->RunPendingFrames();
	TestEqual(TEXT("Final native argument expands first parameter"), Manager->GetScriptVariable(TEXT("a")).Text, FString(TEXT("string")));
	TestEqual(TEXT("Final native argument expands second parameter"), Manager->GetScriptVariable(TEXT("second")).Number, 3.0);
	Call->ResultVariables = {TEXT("dup"), TEXT("dup")}; Call->ResultQuestScopes = {false, false};
	Multiple->ValueExpressions = {TEXT("10"), TEXT("20")};
	Manager->PushFrame({Call}); Manager->RunPendingFrames();
	TestEqual(TEXT("Duplicate return destinations write left to right"), Manager->GetScriptVariable(TEXT("dup")).Number, 20.0);
	Call->Body.Reset();
	Manager->PushFrame({Call}); Manager->RunPendingFrames();
	TestTrue(TEXT("Empty function pads every result destination"), Manager->GetScriptVariable(TEXT("dup")).bIsNil);
	Call->Body = {Write};
	Manager->PushFrame({Call}); Manager->RunPendingFrames();
	TestTrue(TEXT("Implicit return pads every result destination"), Manager->GetScriptVariable(TEXT("dup")).bIsNil);
	Call->Body = {Multiple}; Multiple->ValueExpressions = {TEXT("7"), TEXT("1 / 0")};
	Manager->SetScriptVariable(TEXT("dup"), FMT2QuestValue(88.0));
	Manager->PushFrame({Call}); Manager->RunPendingFrames();
	TestEqual(TEXT("Failed return list publishes no destinations"), Manager->GetScriptVariable(TEXT("dup")).Number, 88.0);
	Call->ResultQuestScopes.Reset(); Call->ArgumentExpressions = {TEXT("pc.change_alignment(1)")};
	const int32 AlignmentBefore = State->GetRawAlignment();
	TestEqual(TEXT("Invalid result scopes reject function node"), Call->Execute(Context), EMT2QuestNodeResult::Stop);
	TestEqual(TEXT("Invalid frame configuration performs no argument side effects"), State->GetRawAlignment(), AlignmentBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2QuestNpcLocksTest,
	"Metin2.Quests.PlayerApi.NpcLocks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMT2QuestNpcLocksTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Lease test world"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AMT2PlayerState* FirstState = World->SpawnActor<AMT2PlayerState>();
	AMT2PlayerState* SecondState = World->SpawnActor<AMT2PlayerState>();
	AMT2PlayerCharacter* FirstPawn = World->SpawnActor<AMT2PlayerCharacter>();
	AMT2PlayerCharacter* SecondPawn = World->SpawnActor<AMT2PlayerCharacter>();
	AMT2Mob* Npc = World->SpawnActor<AMT2Mob>();
	AMT2Mob* OtherNpc = World->SpawnActor<AMT2Mob>();
	if (!FirstState || !SecondState || !FirstPawn || !SecondPawn || !Npc || !OtherNpc) { AddError(TEXT("Lease actors missing")); return false; }
	FirstPawn->SetPlayerState(FirstState);
	SecondPawn->SetPlayerState(SecondState);
	UMT2QuestManagerComponent* First = FirstState->GetQuestManagerComponent();
	UMT2QuestManagerComponent* Second = SecondState->GetQuestManagerComponent();
	UMT2QuestComponent* Dialog = FirstState->GetQuestComponent();
	FMT2QuestContext A;
	A.Manager = First;
	A.PlayerState = FirstState;
	A.Player = FirstPawn;
	A.TargetActor = Npc;
	FMT2QuestContext B;
	B.Manager = Second;
	B.PlayerState = SecondState;
	B.Player = SecondPawn;
	B.TargetActor = Npc;
	First->ActiveContext = A;
	Second->ActiveContext = B;
	auto ExpectLock = [&](const FMT2QuestContext& Context, bool Expected)
	{
		bool bOk = false;
		const FMT2QuestValue Value = FMT2QuestExpression::Evaluate(TEXT("npc.lock()"), Context, bOk);
		TestTrue(TEXT("Lock returns a typed boolean"), bOk && Value.bIsBoolean);
		TestEqual(TEXT("Lock result"), Value.AsBool(), Expected);
	};
	ExpectLock(A, true);
	ExpectLock(A, true);
	ExpectLock(B, false);
	bool bOk = false;
	FMT2QuestValue Result = FMT2QuestExpression::Evaluate(TEXT("npc.unlock()"), B, bOk);
	TestTrue(TEXT("Non-owner unlock is a nil-returning no-op"), bOk && Result.bIsNil);
	ExpectLock(B, false);
	UMT2QuestNode_NpcLock* Unlock = NewObject<UMT2QuestNode_NpcLock>();
	Unlock->bUnlock = true;
	TestEqual(TEXT("Imported unlock node executes"), Unlock->Execute(A), EMT2QuestNodeResult::Continue);
	ExpectLock(B, true);
	Second->CancelConversation();
	FirstState->SetRole(ROLE_SimulatedProxy);
	ExpectLock(A, false);
	FirstState->SetRole(ROLE_Authority);
	Npc->SetRole(ROLE_SimulatedProxy);
	ExpectLock(A, false);
	Npc->SetRole(ROLE_Authority);
	FMT2QuestContext Forged = A;
	Forged.Player = SecondPawn;
	ExpectLock(Forged, false);
	FMT2QuestContext NoNpc = A;
	NoNpc.TargetActor = nullptr;
	ExpectLock(NoNpc, true);
	NoNpc.TargetActor = SecondPawn;
	ExpectLock(NoNpc, true);
	TestEqual(TEXT("Player/no-NPC contexts do not reserve a mob"), First->LockedQuestNpcs.Num(), 0);
	ExpectLock(A, true);
	const uint64 Checkpoint = First->GetNpcLockCheckpoint();
	FMT2QuestContext Other = A;
	Other.TargetActor = OtherNpc;
	ExpectLock(Other, true);
	First->ReleaseNpcLocksAfter(Checkpoint);
	FMT2QuestContext OtherB = B;
	OtherB.TargetActor = OtherNpc;
	ExpectLock(OtherB, true);
	ExpectLock(B, false);
	Second->CancelConversation();
	First->CancelConversation();

	UMT2QuestNode_NpcLock* Lock = NewObject<UMT2QuestNode_NpcLock>();
	UMT2QuestNode_SetFlag* After = NewObject<UMT2QuestNode_SetFlag>();
	After->FlagName = TEXT("after_wait");
	First->PushFrame({Lock, NewObject<UMT2QuestNode_Wait>(), After});
	First->RunPendingFrames();
	TestTrue(TEXT("Lease survives a suspended wait"), First->HasPendingConversation());
	ExpectLock(B, false);
	Dialog->ServerAnswerDialog_Implementation(0);
	TestTrue(TEXT("Dismissed wait aborts its frames"), First->CallStack.IsEmpty());
	TestEqual(TEXT("Dismissal does not run later rewards"), First->GetQuestFlag(TEXT("after_wait")), 0);
	ExpectLock(B, true);
	Second->CancelConversation();
	First->PushFrame({Lock, NewObject<UMT2QuestNode_Wait>(), After});
	First->RunPendingFrames();
	Dialog->ServerAnswerDialog_Implementation(1);
	TestEqual(TEXT("Confirmation resumes the block"), First->GetQuestFlag(TEXT("after_wait")), 1);
	ExpectLock(B, true);
	Second->CancelConversation();

	UMT2QuestNode_Say* Say = NewObject<UMT2QuestNode_Say>();
	Say->Lines = {FText::FromString(TEXT("final page"))};
	First->PushFrame({Lock, Say});
	First->RunPendingFrames();
	TestTrue(TEXT("Finished executor retains the final-page lease"), First->CallStack.IsEmpty() && Dialog->HasPendingDialog());
	ExpectLock(B, false);
	Dialog->ServerAnswerDialog_Implementation(1);
	ExpectLock(B, true);
	Second->CancelConversation();
	UMT2QuestNode_Input* Input = NewObject<UMT2QuestNode_Input>();
	Input->ResultVariable = TEXT("answer");
	First->PushFrame({Lock, Input, After});
	First->RunPendingFrames();
	Dialog->ServerAnswerDialog_Implementation(0);
	TestTrue(TEXT("Input dismissal aborts without submitting text"), First->CallStack.IsEmpty() && First->PendingInputVariable.IsNone());
	TestEqual(TEXT("Input dismissal does not run later rewards"), First->GetQuestFlag(TEXT("after_wait")), 1);
	ExpectLock(B, true);
	Second->CancelConversation();
	First->PushFrame({Lock, NewObject<UMT2QuestNode_Wait>(), After});
	First->RunPendingFrames();
	AMT2PlayerCharacter* Replacement = World->SpawnActor<AMT2PlayerCharacter>();
	Replacement->SetPlayerState(FirstState);
	TestTrue(TEXT("Pawn replacement cancels its conversation"), First->CallStack.IsEmpty());
	ExpectLock(B, true);
	Second->CancelConversation();
	A.Player = Replacement;
	First->ActiveContext = A;
	First->PushFrame({Lock, NewObject<UMT2QuestNode_Wait>(), After});
	First->RunPendingFrames();
	Replacement->Destroy();
	TestTrue(TEXT("Pawn destruction cancels its conversation"), First->CallStack.IsEmpty());
	ExpectLock(B, true);
	Second->CancelConversation();
	AMT2PlayerCharacter* LastPawn = World->SpawnActor<AMT2PlayerCharacter>();
	LastPawn->SetPlayerState(FirstState);
	A.Player = LastPawn;
	First->ActiveContext = A;
	First->PushFrame({Lock, NewObject<UMT2QuestNode_Wait>(), After});
	First->RunPendingFrames();
	Npc->Destroy();
	TestTrue(TEXT("NPC destruction cancels its conversation"), First->CallStack.IsEmpty() && !Dialog->HasPendingDialog());
	A.TargetActor = OtherNpc;
	First->ActiveContext = A;
	ExpectLock(A, true);
	First->RegisterAllComponentTickFunctions(true);
	First->BeginPlay();
	First->EndPlay(EEndPlayReason::Destroyed);
	ExpectLock(OtherB, true);
	Second->CancelConversation();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2QuestLuaValuesTest,
	"Metin2.Quests.PlayerApi.LuaValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMT2QuestLuaValuesTest::RunTest(const FString& Parameters)
{
	FMT2QuestContext Context;
	auto ExpectBool = [&](const TCHAR* Expression, bool Expected)
	{
		bool bOk = false;
		const FMT2QuestValue Value = FMT2QuestExpression::Evaluate(Expression, Context, bOk);
		TestTrue(FString(Expression) + TEXT(" evaluated"), bOk);
		TestEqual(FString(Expression), Value.AsBool(), Expected);
	};
	ExpectBool(TEXT("0"), true);
	ExpectBool(TEXT("''"), true);
	ExpectBool(TEXT("{}"), true);
	ExpectBool(TEXT("nil"), false);
	ExpectBool(TEXT("false"), false);
	ExpectBool(TEXT("not 0"), false);
	ExpectBool(TEXT("false == 0"), false);
	ExpectBool(TEXT("true == 1"), false);
	ExpectBool(TEXT("'' == 0"), false);
	ExpectBool(TEXT("'3' == 3"), false);
	ExpectBool(TEXT("false ~= nil"), true);
	ExpectBool(TEXT("'a' < 'b'"), true);
	ExpectBool(TEXT("'a' == 'A'"), false);
	ExpectBool(TEXT("not party.is_party()"), true);
	ExpectBool(TEXT("pc.is_gm() == false"), true);
	ExpectBool(TEXT("pc.get_gm_level() == false"), false);
	ExpectBool(TEXT("false and next_time_is_now()"), false);
	ExpectBool(TEXT("true or next_time_is_now()"), true);
	ExpectBool(TEXT("false and (1 / 0)"), false);
	ExpectBool(TEXT("true or (false + 1)"), true);
	ExpectBool(TEXT("false and nil.bad or true"), true);
	ExpectBool(TEXT("type(nil) == 'nil'"), true);
	ExpectBool(TEXT("type(false) == 'boolean' and type(true) == 'boolean'"), true);
	ExpectBool(TEXT("type(0) == 'number' and type(-3.5) == 'number'"), true);
	ExpectBool(TEXT("type('3') == 'string' and type('') == 'string'"), true);
	ExpectBool(TEXT("type({}) == 'table' and type({7}) == 'table'"), true);
	ExpectBool(TEXT("type(string.gsub('aaa', 'a', 'b')) == 'string'"), true);
	ExpectBool(TEXT("type(string.find('abc', 'missing')) == 'nil'"), true);
	ExpectBool(TEXT("type(7, false) == 'number'"), true);
	bool bOk = false;
	FMT2QuestExpression::Evaluate(TEXT("type()"), Context, bOk);
	TestFalse(TEXT("Type requires a value, including explicit nil"), bOk);
	FMT2QuestExpression::Evaluate(TEXT("type(pc.change_alignment(1))"), Context, bOk);
	TestFalse(TEXT("A final zero-result call leaves type without an argument"), bOk);
	FMT2QuestValue Value = FMT2QuestExpression::Evaluate(TEXT("0 or 17"), Context, bOk);
	TestTrue(TEXT("Numeric operand is retained"), bOk && !Value.bIsBoolean && Value.Number == 0.0);
	Value = FMT2QuestExpression::Evaluate(TEXT("'' or 'fallback'"), Context, bOk);
	TestTrue(TEXT("Empty-string operand is retained"), bOk && Value.bIsText && Value.Text.IsEmpty());
	Value = FMT2QuestExpression::Evaluate(TEXT("true and 'chosen'"), Context, bOk);
	TestEqual(TEXT("And yields second operand"), Value.Text, FString(TEXT("chosen")));
	TestTrue(TEXT("And operand evaluated"), bOk);
	Value = FMT2QuestExpression::Evaluate(TEXT("tonumber(false)"), Context, bOk);
	TestTrue(TEXT("Boolean tonumber is nil, not zero"), bOk && Value.bIsNil);
	for (const TCHAR* Invalid : {TEXT("tonumber('.')"), TEXT("tonumber('1e')"), TEXT("tonumber('3f')")})
	{
		Value = FMT2QuestExpression::Evaluate(Invalid, Context, bOk);
		TestTrue(FString(Invalid) + TEXT(" returns nil"), bOk && Value.bIsNil);
	}
	Value = FMT2QuestExpression::Evaluate(TEXT("tonumber(' 1.2e2 ')"), Context, bOk);
	TestTrue(TEXT("Numeric string exponent and whitespace accepted"), bOk && Value.Number == 120.0);
	Value = FMT2QuestExpression::Evaluate(TEXT("tostring(false)..tostring(nil)"), Context, bOk);
	TestTrue(TEXT("Boolean and nil string conversions"), bOk && Value.Text == TEXT("falsenil"));
	Value = FMT2QuestExpression::Evaluate(TEXT("'3' + 2"), Context, bOk);
	TestTrue(TEXT("Numeric string arithmetic coerces"), bOk && Value.Number == 5.0);
	Value = FMT2QuestExpression::Evaluate(TEXT("'value='..1 + 2..'!'"), Context, bOk);
	TestTrue(TEXT("Arithmetic binds more tightly than concatenation"), bOk && Value.Text == TEXT("value=3!"));
	for (const TCHAR* Invalid : {TEXT("false + 1"), TEXT("nil * 2"), TEXT("true < 1"), TEXT("false..'x'"), TEXT("1 / 0")})
	{
		FMT2QuestExpression::Evaluate(Invalid, Context, bOk);
		TestFalse(FString(Invalid) + TEXT(" rejected"), bOk);
	}
	Value = FMT2QuestExpression::Evaluate(TEXT("{[false]='boolean', [0]='number', [true]=false}"), Context, bOk);
	TestTrue(TEXT("Boolean-keyed table constructed"), bOk);
	TestEqual(TEXT("False key does not alias numeric zero"), Value.GetTableValue(FMT2QuestValue::Boolean(false)).Text, FString(TEXT("boolean")));
	TestEqual(TEXT("Zero key retains separate value"), Value.GetTableValue(FMT2QuestValue(0)).Text, FString(TEXT("number")));
	TestTrue(TEXT("Boolean table value retained"), Value.GetTableValue(FMT2QuestValue::Boolean(true)).bIsBoolean);
	TestEqual(TEXT("Foreach includes boolean keys"), Value.GetTableKeys().Num(), 3);
	FMT2QuestValue Other = FMT2QuestValue::NewTable();
	Other.SetTableValue(FMT2QuestValue::Boolean(false), Value);
	TestFalse(TEXT("Boolean-keyed cycles rejected"), Value.SetTableValue(FMT2QuestValue::Boolean(true), Other));
	TestTrue(TEXT("Boolean key can be deleted"), Value.SetTableValue(FMT2QuestValue::Boolean(false), FMT2QuestValue()));
	TestTrue(TEXT("Deleted boolean key reads nil"), Value.GetTableValue(FMT2QuestValue::Boolean(false)).bIsNil);
	FMT2QuestTable StaticTable;
	StaticTable.Nodes.SetNum(3);
	StaticTable.Nodes[0].bIsTable = true;
	StaticTable.Nodes[0].Children = {1, 2};
	StaticTable.Nodes[1].bIsBoolean = true;
	StaticTable.Nodes[2].bIsNil = true;
	const FMT2QuestValue StaticValue(&StaticTable, 0);
	TestTrue(TEXT("Serialized boolean leaves retain type"), StaticValue.GetTableValue(FMT2QuestValue(1)).bIsBoolean);
	TestFalse(TEXT("Serialized false is false"), StaticValue.GetTableValue(FMT2QuestValue(1)).AsBool());
	TestTrue(TEXT("Serialized nil is absent"), StaticValue.GetTableValue(FMT2QuestValue(2)).bIsNil);
	FString Reason;
	TestFalse(TEXT("Skipped syntax still validated"), FMT2QuestExpression::IsSupported(TEXT("true or (1 + )"), Reason));
	TestFalse(TEXT("Skipped unbound APIs remain unconverted"), FMT2QuestExpression::IsSupported(TEXT("true or missing_api()"), Reason));
	TestTrue(TEXT("Skipped runtime-invalid operation has valid syntax"), FMT2QuestExpression::IsSupported(TEXT("false and nil.bad"), Reason));
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Village test world"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	Context.Player = World->SpawnActor<AMT2PlayerCharacter>();
	AMT2MapPresentationActor* Map = World->SpawnActor<AMT2MapPresentationActor>();
	if (!TestNotNull(TEXT("Village player"), Context.Player.Get()) || !TestNotNull(TEXT("Village map"), Map)) { return false; }
	Map->MapIndex = 1;
	Value = FMT2QuestExpression::Evaluate(TEXT("is_destination_village(1)"), Context, bOk);
	TestTrue(TEXT("Matching first village returns truthy empty string"), bOk && Value.bIsText && Value.Text.IsEmpty() && Value.AsBool());
	Value = FMT2QuestExpression::Evaluate(TEXT("is_destination_village(2)"), Context, bOk);
	TestTrue(TEXT("Nonmatching village returns nil"), bOk && Value.bIsNil);
	Map->MapIndex = 43;
	ExpectBool(TEXT("is_destination_village(2)"), true);
	ExpectBool(TEXT("is_destination_village(3)"), true);
	Map->MapIndex = 65;
	ExpectBool(TEXT("is_destination_village(65)"), true);
	ExpectBool(TEXT("is_destination_village(3)"), false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2QuestCallbacksTest,
	"Metin2.Quests.PlayerApi.Callbacks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMT2QuestCallbacksTest::RunTest(const FString& Parameters)
{
	UMT2QuestManagerComponent* Manager = NewObject<UMT2QuestManagerComponent>();
	Manager->ActiveContext.Manager = Manager;
	bool bOk = false;
	FMT2QuestValue Table = FMT2QuestExpression::Evaluate(TEXT("{10,20,30}"), Manager->ActiveContext, bOk);
	TestTrue(TEXT("Callback table constructed"), bOk);
	Manager->SetScriptVariable(TEXT("t"), Table);
	Manager->SetScriptVariable(TEXT("k"), FMT2QuestValue(77));
	Manager->SetScriptVariable(TEXT("v"), FMT2QuestValue(TEXT("outer")));
	Manager->SetScriptVariable(TEXT("private"), FMT2QuestValue(12));
	UMT2QuestNode_TableCallback* Callback = NewObject<UMT2QuestNode_TableCallback>();
	Callback->TableExpression = TEXT("t");
	Callback->KeyVariable = TEXT("k");
	Callback->ValueVariable = TEXT("v");
	Callback->LocalVariables = {TEXT("private")};
	Callback->ResultVariable = TEXT("result");
	UMT2QuestNode_SetFlag* Count = NewObject<UMT2QuestNode_SetFlag>();
	Count->FlagName = TEXT("callback_count");
	Count->bAdd = true;
	UMT2QuestNode_SetVariable* Local = NewObject<UMT2QuestNode_SetVariable>();
	Local->VariableName = TEXT("private");
	Local->Expression = TEXT("k");
	Callback->Body = {Count, Local, NewObject<UMT2QuestNode_Wait>()};
	Manager->PushFrame({Callback});
	Manager->RunPendingFrames();
	TestEqual(TEXT("First callback suspends"), Manager->GetQuestFlag(TEXT("callback_count")), 1);
	Table.SetTableValue(FMT2QuestValue(2), FMT2QuestValue());
	Table.SetTableValue(FMT2QuestValue(4), FMT2QuestValue(40));
	Manager->RunPendingFrames();
	TestEqual(TEXT("Foreachi invokes a removed sequence slot"), Manager->GetQuestFlag(TEXT("callback_count")), 2);
	TestTrue(TEXT("Removed sequence slot supplies nil"), Manager->GetScriptVariable(TEXT("v")).bIsNil);
	Manager->RunPendingFrames();
	Manager->RunPendingFrames();
	TestTrue(TEXT("Callback completes after original length"), Manager->CallStack.IsEmpty());
	TestEqual(TEXT("Appended sequence slot is not visited"), Manager->GetQuestFlag(TEXT("callback_count")), 3);
	TestTrue(TEXT("Implicit callback return is nil"), Manager->GetScriptVariable(TEXT("result")).bIsNil);
	TestEqual(TEXT("Key parameter restored"), Manager->GetScriptVariable(TEXT("k")).Number, 77.0);
	TestEqual(TEXT("Value parameter restored"), Manager->GetScriptVariable(TEXT("v")).Text, FString(TEXT("outer")));
	TestEqual(TEXT("Callback local restored"), Manager->GetScriptVariable(TEXT("private")).Number, 12.0);

	Table = FMT2QuestExpression::Evaluate(TEXT("{[2]=20, answer=42}"), Manager->ActiveContext, bOk);
	Manager->SetScriptVariable(TEXT("t"), Table);
	Callback->bSequence = false;
	Callback->Body = {Count};
	Manager->PushFrame({Callback});
	Manager->RunPendingFrames();
	TestEqual(TEXT("Foreach visits numeric and named keys"), Manager->GetQuestFlag(TEXT("callback_count")), 5);
	UMT2QuestNode_Return* Return = NewObject<UMT2QuestNode_Return>();
	Return->ValueExpression = TEXT("0");
	Callback->Body = {Count, Return};
	UMT2QuestNode_SetFlag* After = NewObject<UMT2QuestNode_SetFlag>();
	After->FlagName = TEXT("after_callback");
	Manager->PushFrame({Callback, After});
	Manager->RunPendingFrames();
	TestEqual(TEXT("Zero return terminates iteration"), Manager->GetQuestFlag(TEXT("callback_count")), 6);
	TestFalse(TEXT("Zero callback result is not nil"), Manager->GetScriptVariable(TEXT("result")).bIsNil);
	TestEqual(TEXT("Return exits callback, not enclosing trigger"), Manager->GetQuestFlag(TEXT("after_callback")), 1);
	Return->ValueExpression = TEXT("nil");
	Manager->PushFrame({Callback});
	Manager->RunPendingFrames();
	TestEqual(TEXT("Explicit nil return continues iteration"), Manager->GetQuestFlag(TEXT("callback_count")), 8);
	Callback->MaximumIterations = 1;
	Callback->Body = {Count};
	After->FlagName = TEXT("after_failed_callback");
	AddExpectedError(TEXT("Quest table callback exceeded its iteration/nesting limit"), EAutomationExpectedErrorFlags::Contains, 1);
	Manager->PushFrame({Callback, After});
	Manager->RunPendingFrames();
	TestEqual(TEXT("Iteration limit aborts remaining trigger"), Manager->GetQuestFlag(TEXT("after_failed_callback")), 0);
	TestEqual(TEXT("Failed callback restores parameters"), Manager->GetScriptVariable(TEXT("k")).Number, 77.0);

	UMT2QuestNode_Select* Select = NewObject<UMT2QuestNode_Select>();
	Select->TableExpression = TEXT("menu");
	FMT2QuestValue Menu = FMT2QuestExpression::Evaluate(TEXT("{'first', '{literal}', 'cancel'}"), Manager->ActiveContext, bOk);
	Manager->SetScriptVariable(TEXT("menu"), Menu);
	TArray<FText> Options;
	TestTrue(TEXT("select_table evaluates current table"), Select->BuildTableOptions(Manager->ActiveContext, Options));
	TestEqual(TEXT("Table menu produces three choices"), Options.Num(), 3);
	TestEqual(TEXT("Runtime label remains literal"), Options[1].ToString(), FString(TEXT("{literal}")));
	Menu.SetTableValue(FMT2QuestValue(2), FMT2QuestValue(TEXT("changed")));
	TestTrue(TEXT("Menu reads aliases at selection time"), Select->BuildTableOptions(Manager->ActiveContext, Options));
	TestEqual(TEXT("Changed table label displayed"), Options[1].ToString(), FString(TEXT("changed")));
	Menu.SetTableValue(FMT2QuestValue(2), FMT2QuestValue::NewTable());
	TestFalse(TEXT("Invalid menu does not display misleading table label"), Select->BuildTableOptions(Manager->ActiveContext, Options));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2QuestItemCopyTest,
	"Metin2.Quests.PlayerApi.ItemCopy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMT2QuestItemCopyTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AMT2PlayerCharacter* Player = World->SpawnActor<AMT2PlayerCharacter>();
	if (!TestNotNull(TEXT("Player"), Player)) { return false; }
	UMT2InventoryComponent* Inventory = Player->GetInventoryComponent();
	UMT2ItemArmorTemplate* SourceTemplate = NewObject<UMT2ItemArmorTemplate>();
	SourceTemplate->Vnum = 11290;
	SourceTemplate->VnumRange = 9;
	SourceTemplate->InventorySize = 3;
	UMT2ItemArmorTemplate* ResultTemplate = NewObject<UMT2ItemArmorTemplate>();
	ResultTemplate->Vnum = 20000;
	ResultTemplate->VnumRange = 9;
	ResultTemplate->InventorySize = 3;
	FMT2ItemSlot Source;
	Source.Vnum = 11299;
	Source.Bonuses.SetNum(1);
	Source.Bonuses[0].Type = EMT2ItemBonusType::MaxHP;
	Source.Bonuses[0].Value = 1000;
	Source.Bonuses[0].Kind = EMT2ItemBonusKind::Rare;
	Source.MetinSockets.SetNum(3);
	Source.MetinSockets[0].Type = EMT2MetinSocketType::Gold;
	Source.MetinSockets[0].Value = 28960;
	Source.bNewlyAcquired = true;
	const TMap<int32, int32> Materials = {{70031, 3}, {51001, 100}, {25040, 2}};
	auto Reset = [&]()
	{
		Inventory->Slots.Reset();
		Inventory->Slots.SetNum(UMT2InventoryComponent::SlotCount);
		Inventory->Slots[0] = Source;
		Inventory->Slots[1].Vnum = 70031;
		Inventory->Slots[1].Count = 2;
		Inventory->Slots[2].Vnum = 70031;
		Inventory->Slots[2].Count = 2;
		Inventory->Slots[3].Vnum = 51001;
		Inventory->Slots[3].Count = 100;
		Inventory->Slots[4].Vnum = 25040;
		Inventory->Slots[4].Count = 2;
	};
	auto Replace = [&]()
	{
		return Inventory->CopyAndReplaceQuestItemWithTemplates(0, Source, 20009,
			SourceTemplate, ResultTemplate, Materials);
	};
	auto CheckUnchanged = [&](const TArray<FMT2ItemSlot>& Before)
	{
		for (int32 Index = 0; Index < Before.Num(); ++Index)
		{
			if (!FMT2ItemSlot::StaticStruct()->CompareScriptStruct(&Before[Index], &Inventory->Slots[Index], 0))
			{
				AddError(FString::Printf(TEXT("Failed transaction changed slot %d"), Index));
				break;
			}
		}
	};
	Reset();
	Inventory->Slots[0].bNewlyAcquired = false; // Viewing a tooltip is not an item change.
	TestTrue(TEXT("Copy and complete material batch commit"), Replace());
	TestEqual(TEXT("Requested ranged VNUM retained"), Inventory->Slots[0].Vnum, 20009);
	TestEqual(TEXT("Bonus value retained"), Inventory->Slots[0].Bonuses[0].Value, 1000);
	TestEqual(TEXT("Rare bonus kind retained"), Inventory->Slots[0].Bonuses[0].Kind, EMT2ItemBonusKind::Rare);
	TestEqual(TEXT("Broken stone cleared"), Inventory->Slots[0].MetinSockets[0].Value, 0);
	TestEqual(TEXT("Sockets become silver"), Inventory->Slots[0].MetinSockets[0].Type, EMT2MetinSocketType::Silver);
	TestEqual(TEXT("Materials removed across stacks"), Inventory->CountItemByVnum(70031), 1);
	TestEqual(TEXT("Other material fully consumed"), Inventory->CountItemByVnum(51001), 0);

	Reset();
	Inventory->Slots[4].Count = 1;
	TArray<FMT2ItemSlot> Before = Inventory->Slots;
	TestFalse(TEXT("Missing materials reject complete transaction"), Replace());
	CheckUnchanged(Before);
	Reset();
	Inventory->Slots[0].Bonuses[0].Value = 500;
	Before = Inventory->Slots;
	TestFalse(TEXT("Source modified while dialog suspended is rejected"), Replace());
	CheckUnchanged(Before);
	Reset();
	Inventory->Slots[5].Vnum = 12345;
	Before = Inventory->Slots;
	TestFalse(TEXT("Blocked result footprint rejected"), Replace());
	CheckUnchanged(Before);
	Reset();
	Before = Inventory->Slots;
	TestFalse(TEXT("Missing destination template rejected"), Inventory->CopyAndReplaceQuestItemWithTemplates(
		0, Source, 20000, SourceTemplate, nullptr, Materials));
	CheckUnchanged(Before);
	Inventory->Slots[0] = FMT2ItemSlot();
	Inventory->Slots[44] = Source;
	Before = Inventory->Slots;
	TestFalse(TEXT("Result cannot cross inventory page"), Inventory->CopyAndReplaceQuestItemWithTemplates(
		44, Source, 20000, SourceTemplate, ResultTemplate, Materials));
	CheckUnchanged(Before);
	Reset();
	Inventory->Slots[0].Count = 2;
	FMT2ItemSlot Stack = Inventory->Slots[0];
	Before = Inventory->Slots;
	TestFalse(TEXT("Copy cannot destroy a source stack"), Inventory->CopyAndReplaceQuestItemWithTemplates(
		0, Stack, 20000, SourceTemplate, ResultTemplate, Materials));
	CheckUnchanged(Before);

	// Verify the actual imported equipment metadata and a surviving stone, without editing CDOs.
	UClass* ImportedSource = LoadClass<UMT2ItemTemplate>(nullptr,
		UMT2PathSettings::Path(TEXT("Items_Blueprints_BP_Item_11299")));
	UClass* ImportedResult = LoadClass<UMT2ItemTemplate>(nullptr,
		UMT2PathSettings::Path(TEXT("Items_Blueprints_BP_Item_20000")));
	UClass* Stone = LoadClass<UMT2ItemMetinStoneTemplate>(nullptr,
		UMT2PathSettings::Path(TEXT("Items_Blueprints_BP_Item_28030")));
	if (TestNotNull(TEXT("Imported source item"), ImportedSource) &&
		TestNotNull(TEXT("Imported replacement item"), ImportedResult) && TestNotNull(TEXT("Imported stone"), Stone))
	{
		Source.MetinSockets[1].Stone = Stone;
		Reset();
		TestTrue(TEXT("Imported metadata accepts cube replacement"), Inventory->CopyAndReplaceQuestItemWithTemplates(
			0, Source, 20000, ImportedSource->GetDefaultObject<UMT2ItemTemplate>(),
			ImportedResult->GetDefaultObject<UMT2ItemTemplate>(), Materials));
		TestEqual(TEXT("Surviving stone packed ahead of broken/open sockets"),
			Inventory->Slots[0].MetinSockets[0].Stone.Get(), Stone);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2QuestItemRangeTest,
	"Metin2.Quests.PlayerApi.ItemRanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMT2QuestItemRangeTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AMT2PlayerCharacter* Player = World->SpawnActor<AMT2PlayerCharacter>();
	if (!TestNotNull(TEXT("Player"), Player)) { return false; }
	UMT2InventoryComponent* Inventory = Player->GetInventoryComponent();
	FMT2QuestContext Context;
	Context.Player = Player;
	TArray<FMT2ItemSlot> Items;
	Items.SetNum(5);
	Items[0].Vnum = 30269; Items[0].Count = 4;
	Items[1].Vnum = 30265; Items[1].Count = 2;
	Items[2].Vnum = 30265; Items[2].Count = 3;
	Items[3].Vnum = 30266; Items[3].Count = 6;
	Items[4].Vnum = 30264; Items[4].Count = 9;
	TArray<FMT2ItemSlot> Worn;
	Worn.SetNum(1);
	Worn[0].Vnum = 30265; Worn[0].Count = 1;
	Inventory->RestoreItems(Items, Worn);
	auto CheckUnchanged = [&](const TArray<FMT2ItemSlot>& Before)
	{
		for (int32 Index = 0; Index < Before.Num(); ++Index)
		{
			TestTrue(TEXT("Inventory unchanged"), FMT2ItemSlot::StaticStruct()->CompareScriptStruct(
				&Before[Index], &Inventory->GetSlots()[Index], 0));
		}
	};
	bool bOk = false;
	auto Evaluate = [&](const TCHAR* Expression)
	{
		bOk = false;
		const FMT2QuestValue Value = FMT2QuestExpression::Evaluate(Expression, Context, bOk);
		TestTrue(FString::Printf(TEXT("Evaluated %s"), Expression), bOk);
		return Value;
	};
	TestEqual(TEXT("Inclusive count, all stacks, no worn items"),
		Evaluate(TEXT("count_item_range(30265,30269)")).Number, 15.0);
	TestEqual(TEXT("Numeric-string arguments"), Evaluate(TEXT("count_item_range('30265','30265')")).Number, 5.0);
	TestEqual(TEXT("Reversed range is empty"), Evaluate(TEXT("count_item_range(30269,30265)")).Number, 0.0);
	TestEqual(TEXT("Large range scans inventory, not all identifiers"),
		Evaluate(TEXT("count_item_range(0,2147483647)")).Number, 24.0);
	FString Unsupported;
	const TArray<FMT2ItemSlot> Before = Inventory->GetSlots();
	TestTrue(TEXT("Import validation supports removal"), FMT2QuestExpression::IsSupported(
		TEXT("remove_item_range(7,30265,30269)"), Unsupported));
	CheckUnchanged(Before);
	TestFalse(TEXT("Short circuit does not consume items"),
		Evaluate(TEXT("false and remove_item_range(7,30265,30269)")).AsBool());
	CheckUnchanged(Before);
	const FMT2QuestValue Insufficient = Evaluate(TEXT("remove_item_range(16,30265,30269)"));
	TestTrue(TEXT("Insufficient result is a typed false"), Insufficient.bIsBoolean && !Insufficient.AsBool());
	CheckUnchanged(Before);
	TestTrue(TEXT("Zero cost with matching items is true"), Evaluate(TEXT("remove_item_range(0,30265,30269)")).AsBool());
	TestTrue(TEXT("Zero cost without matching items falls through to nil"),
		Evaluate(TEXT("remove_item_range(0,40000,40001)")).bIsNil);
	CheckUnchanged(Before);
	const FMT2QuestValue Removed = Evaluate(TEXT("remove_item_range(7,30265,30269)"));
	TestTrue(TEXT("Successful removal is a typed true"), Removed.bIsBoolean && Removed.AsBool());
	TestTrue(TEXT("First lower-vnum stack consumed"), Inventory->GetSlots()[1].IsEmpty());
	TestTrue(TEXT("Second lower-vnum stack consumed"), Inventory->GetSlots()[2].IsEmpty());
	TestEqual(TEXT("Next vnum partially consumed"), Inventory->GetSlots()[3].Count, 4);
	TestEqual(TEXT("Higher vnum preserved despite earlier slot"), Inventory->GetSlots()[0].Count, 4);
	TestEqual(TEXT("Outside range preserved"), Inventory->GetSlots()[4].Count, 9);
	TestEqual(TEXT("Equipment preserved"), Inventory->GetEquipment()[0].Count, 1);
	TestEqual(TEXT("Count reflects removal"), Evaluate(TEXT("count_item_range(30265,30269)")).Number, 8.0);
	TestTrue(TEXT("Exact remaining total removed"), Evaluate(TEXT("remove_item_range(8,30265,30269)")).AsBool());
	TestEqual(TEXT("Range exhausted"), Evaluate(TEXT("count_item_range(30265,30269)")).Number, 0.0);
	for (const TCHAR* Expression : {TEXT("remove_item_range(-1,30265,30269)"),
		TEXT("remove_item_range(1.5,30265,30269)"), TEXT("count_item_range(nil,30269)"),
		TEXT("count_item_range(true,30269)"), TEXT("count_item_range(0,2147483648)")})
	{
		FMT2QuestExpression::Evaluate(Expression, Context, bOk);
		TestFalse(TEXT("Unsafe inventory arguments fail evaluation"), bOk);
	}
	Context.Player = nullptr;
	FMT2QuestExpression::Evaluate(TEXT("count_item_range(30265,30269)"), Context, bOk);
	TestFalse(TEXT("Missing player does not report successful zero"), bOk);
	UMT2InventoryComponent* Ownerless = NewObject<UMT2InventoryComponent>();
	TestFalse(TEXT("Ownerless mutation rejected"), Ownerless->RemoveItemsInVnumRange(1,30265,30269));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2QuestNpcPurgeTest,
	"Metin2.Quests.PlayerApi.NpcPurge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMT2QuestNpcPurgeTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AMT2PlayerState* State = World->SpawnActor<AMT2PlayerState>();
	AMT2PlayerCharacter* Player = World->SpawnActor<AMT2PlayerCharacter>();
	AMT2Mob* Npc = World->SpawnActor<AMT2Mob>();
	if (!State || !Player || !Npc) { AddError(TEXT("Purge actors missing")); return false; }
	Player->SetPlayerState(State);
	UMT2QuestManagerComponent* Manager = State->GetQuestManagerComponent();
	FMT2QuestContext Context;
	Context.Manager = Manager;
	Context.PlayerState = State;
	Context.Player = Player;
	Context.TargetActor = Npc;
	Manager->ActiveContext = Context;
	UMT2QuestNode_PurgeNpc* Purge = NewObject<UMT2QuestNode_PurgeNpc>();
	UMT2QuestNode_SetFlag* After = NewObject<UMT2QuestNode_SetFlag>();
	After->FlagName = TEXT("after_purge");
	FString Unsupported;
	TestTrue(TEXT("Purge expression supported"), FMT2QuestExpression::IsSupported(TEXT("npc.purge()"), Unsupported));
	TestFalse(TEXT("Syntax validation does not destroy NPC"), Npc->IsActorBeingDestroyed());
	bool bOk = false;
	FMT2QuestExpression::Evaluate(TEXT("false and npc.purge()"), Manager->ActiveContext, bOk);
	TestTrue(TEXT("Short circuit succeeds without destruction"), bOk && !Npc->IsActorBeingDestroyed());
	TestTrue(TEXT("Acquire initiating conversation lease"), Manager->TryLockQuestNpc(Context));
	const float HealthBefore = Npc->GetHealthComponent()->GetHealth();
	const int64 YangBefore = State->GetYang();
	Manager->PushFrame({Purge, After});
	Manager->RunPendingFrames();
	TestTrue(TEXT("NPC destroyed"), Npc->IsActorBeingDestroyed());
	TestEqual(TEXT("Purge does not run health/death transition"), Npc->GetHealthComponent()->GetHealth(), HealthBefore);
	TestEqual(TEXT("No death reward"), State->GetYang(), YangBefore);
	TestNull(TEXT("Current quest NPC cleared"), Manager->ActiveContext.TargetActor.Get());
	TestEqual(TEXT("Initiating script continues after purge"), Manager->GetQuestFlag(TEXT("after_purge")), 1);
	TestTrue(TEXT("Initiating frames finish normally"), Manager->CallStack.IsEmpty());
	const FMT2QuestValue NoNpc = FMT2QuestExpression::Evaluate(TEXT("npc.purge()"), Manager->ActiveContext, bOk);
	TestTrue(TEXT("No NPC is a nil-returning no-op"), bOk && NoNpc.bIsNil);
	AMT2Mob* ExpressionNpc = World->SpawnActor<AMT2Mob>();
	Manager->ActiveContext.TargetActor = ExpressionNpc;
	const FMT2QuestValue Result = FMT2QuestExpression::Evaluate(TEXT("npc.purge()"), Manager->ActiveContext, bOk);
	TestTrue(TEXT("Native call destroys NPC and returns nil"), bOk && Result.bIsNil && ExpressionNpc->IsActorBeingDestroyed());
	TestNull(TEXT("Native call clears target"), Manager->ActiveContext.TargetActor.Get());
	AMT2Mob* ProtectedNpc = World->SpawnActor<AMT2Mob>();
	Manager->ActiveContext.TargetActor = ProtectedNpc;
	State->SetRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("Non-authoritative manager rejected"), Manager->PurgeQuestNpc(Manager->ActiveContext));
	State->SetRole(ROLE_Authority);
	ProtectedNpc->SetRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("Non-authoritative NPC rejected"), Manager->PurgeQuestNpc(Manager->ActiveContext));
	ProtectedNpc->SetRole(ROLE_Authority);
	FMT2QuestContext Invalid = Manager->ActiveContext;
	Invalid.Player = nullptr;
	TestFalse(TEXT("Missing pawn rejected"), Manager->PurgeQuestNpc(Invalid));
	Invalid = Manager->ActiveContext;
	Invalid.TargetActor = Player;
	TestFalse(TEXT("Player cannot be purged as NPC"), Manager->PurgeQuestNpc(Invalid));
	TestFalse(TEXT("Failed validation preserves target"), ProtectedNpc->IsActorBeingDestroyed());
	TestEqual(TEXT("Failed validation preserves active context"), Manager->ActiveContext.TargetActor.Get(), static_cast<AActor*>(ProtectedNpc));
	TestTrue(TEXT("Acquire second NPC lease"), Manager->TryLockQuestNpc(Manager->ActiveContext));
	Manager->PushFrame({NewObject<UMT2QuestNode_Wait>(), After});
	Manager->RunPendingFrames();
	ProtectedNpc->Destroy();
	TestTrue(TEXT("External destruction still cancels suspended conversation"), Manager->CallStack.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2QuestEntitiesTest,
	"Metin2.Quests.PlayerApi.Entities",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMT2QuestEntitiesTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UWorld* OtherWorld = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World || !OtherWorld) { AddError(TEXT("Entity worlds missing")); return false; }
	ON_SCOPE_EXIT { OtherWorld->DestroyWorld(false); World->DestroyWorld(false); };
	AMT2PlayerState* State = World->SpawnActor<AMT2PlayerState>();
	AMT2PlayerState* OtherState = World->SpawnActor<AMT2PlayerState>();
	AMT2PlayerCharacter* Player = World->SpawnActor<AMT2PlayerCharacter>();
	AMT2PlayerCharacter* OtherPlayer = World->SpawnActor<AMT2PlayerCharacter>();
	AMT2Mob* Npc = World->SpawnActor<AMT2Mob>();
	AMT2Mob* Duplicate = World->SpawnActor<AMT2Mob>();
	Player->SetPlayerState(State); OtherPlayer->SetPlayerState(OtherState);
	OtherState->SetPlayerName(TEXT("Opponent"));
	FMT2MobDefinition Definition;
	Definition.Vnum = 20349;
	Npc->ConfigureFromDefinition(Definition); Duplicate->ConfigureFromDefinition(Definition);
	UMT2QuestEntitySubsystem* Entities = World->GetSubsystem<UMT2QuestEntitySubsystem>();
	const int32 PlayerId = Entities->GetEntityId(Player);
	const int32 NpcId = Entities->GetEntityId(Npc);
	const int32 DuplicateId = Entities->GetEntityId(Duplicate);
	TestTrue(TEXT("Nonzero instance IDs are distinct"), PlayerId > 0 && NpcId > 0 && DuplicateId > 0 && NpcId != DuplicateId && PlayerId != NpcId);
	TestEqual(TEXT("Repeated lookup stable"), Entities->GetEntityId(Npc), NpcId);
	TestEqual(TEXT("Resolve exact actor"), Entities->FindEntity(NpcId), static_cast<AMT2CharacterBase*>(Npc));
	TestNull(TEXT("Other world cannot resolve ID"), OtherWorld->GetSubsystem<UMT2QuestEntitySubsystem>()->FindEntity(NpcId));
	AMT2Mob* Foreign = OtherWorld->SpawnActor<AMT2Mob>();
	TestEqual(TEXT("Foreign actor cannot register"), Entities->GetEntityId(Foreign), 0);
	TestTrue(TEXT("Different world allocates distinct session ID"), OtherWorld->GetSubsystem<UMT2QuestEntitySubsystem>()->GetEntityId(Foreign) != NpcId);
	FMT2QuestContext Context;
	Context.Player = Player; Context.PlayerState = State;
	Context.Manager = State->GetQuestManagerComponent(); Context.Quest = GetDefault<UMT2Quest>();
	Context.TargetActor = Npc;
	bool bOk = false;
	auto Read = [&](const FString& Expression)
	{
		const FMT2QuestValue Value = FMT2QuestExpression::Evaluate(Expression, Context, bOk);
		TestTrue(TEXT("Entity expression evaluated"), bOk);
		return Value;
	};
	TestEqual(TEXT("pc.get_vid is actual player"), Read(TEXT("pc.get_vid()")).AsInt(), PlayerId);
	TestEqual(TEXT("pc.vid alias"), Read(TEXT("pc.vid")).AsInt(), PlayerId);
	TestEqual(TEXT("npc.get_vid is actual NPC"), Read(TEXT("npc.get_vid()")).AsInt(), NpcId);
	const int32 Found = Read(TEXT("find_npc_by_vnum(20349)")).AsInt();
	TestTrue(TEXT("Vnum lookup returns one real matching instance"), Found == NpcId || Found == DuplicateId);
	TestEqual(TEXT("Player-name lookup returns real instance"), Read(TEXT("find_pc_by_name('Opponent')")).AsInt(), Entities->GetEntityId(OtherPlayer));
	TestEqual(TEXT("Player-name lookup case-sensitive"), Read(TEXT("find_pc_by_name('opponent')")).AsInt(), 0);
	Npc->SetActorLocation(FVector(0, 0, 0));
	Player->SetActorLocation(FVector(1040, 0, 5000));
	TestTrue(TEXT("Default range uses approximate XY distance"), Read(TEXT("npc.is_near()")).AsBool());
	Player->SetActorLocation(FVector(1041, 0, 5000));
	TestFalse(TEXT("Strict boundary, approximate distance exactly 1000"), Read(TEXT("npc.is_near(10)")).AsBool());
	TestFalse(TEXT("Negative range"), Read(TEXT("npc.is_near(-1)")).AsBool());
	Player->SetActorLocation(FVector(10000, 0, 0));
	OtherPlayer->SetActorLocation(FVector(500, 0, 9000));
	const FString Near = FString::Printf(TEXT("npc.is_near_vid(%d,10)"), Entities->GetEntityId(OtherPlayer));
	TestTrue(TEXT("Near-VID compares opponent to NPC, not current player"), Read(Near).AsBool());
	TestTrue(TEXT("Proximity returns typed boolean"), Read(Near).bIsBoolean);
	TestFalse(TEXT("Missing entity"), Read(TEXT("npc.is_near_vid(0,10)")).AsBool());
	Context.TargetActor = nullptr;
	TestFalse(TEXT("Missing NPC"), Read(Near).AsBool());
	Context.TargetActor = Npc;
	UMT2QuestNode_Target* Target = NewObject<UMT2QuestNode_Target>();
	Target->Operation = EMT2QuestTargetOp::SetActor;
	Target->TargetName = TEXT("exact"); Target->Vnum = NpcId;
	TestEqual(TEXT("Actor target executes"), Target->Execute(Context), EMT2QuestNodeResult::Continue);
	TestTrue(TEXT("Exact instance marked"), Context.Manager->IsQuestTargetActor(Npc));
	TestFalse(TEXT("Duplicate vnum not marked"), Context.Manager->IsQuestTargetActor(Duplicate));
	TestFalse(TEXT("Actor ID not treated as a template target"), Context.Manager->IsQuestTargetVnum(20349));
	Npc->Destroy();
	TestNull(TEXT("Destroyed entity no longer resolves"), Entities->FindEntity(NpcId));
	TestFalse(TEXT("Destroyed actor no longer targeted"), Context.Manager->IsQuestTargetActor(Npc));
	Context.Manager->CheckTargetArrivals();
	TestTrue(TEXT("Destroyed actor marker cleaned without arrival"), Context.Manager->GetTargetMarkers().IsEmpty());
	AMT2Mob* Replacement = World->SpawnActor<AMT2Mob>();
	TestTrue(TEXT("IDs not reused after destruction"), Entities->GetEntityId(Replacement) != NpcId);
	const int32 ReplacementId = Entities->GetEntityId(Replacement);
	// Test the lifecycle delegate directly; this is not a streamed-level playthrough.
	Replacement->OnEndPlay.Broadcast(Replacement, EEndPlayReason::RemovedFromWorld);
	TestNull(TEXT("EndPlay notification invalidates entity ID"), Entities->FindEntity(ReplacementId));
	Replacement->SetRole(ROLE_SimulatedProxy);
	TestEqual(TEXT("Non-authoritative actor cannot obtain ID"), Entities->GetEntityId(Replacement), 0);
	Target->Operation = EMT2QuestTargetOp::SetNpc; Target->Vnum = 20349;
	TestEqual(TEXT("Old serialized vnum target remains supported"), Target->Execute(Context), EMT2QuestNodeResult::Continue);
	TestTrue(TEXT("Legacy vnum marker matches remaining duplicate"), Context.Manager->IsQuestTargetActor(Duplicate));
	return true;
}

#endif
