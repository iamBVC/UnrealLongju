#pragma once

// Included by MT2QuestImporter.cpp to retain access to its private parser helpers.
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2QuestCompactReturnTest, "Metin2.Quests.Importer.CompactReturns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMT2QuestCompactReturnTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Compact return normalized"), NormalizeStatement(TEXT("return({[20060]=50601})[race]")),
		FString(TEXT("return ({[20060]=50601})[race]")));
	TestEqual(TEXT("Return-prefixed variable unchanged"), NormalizeStatement(TEXT("return_value = 19")),
		FString(TEXT("return_value = 19")));
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) { return false; }
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AMT2PlayerState* PlayerState = World->SpawnActor<AMT2PlayerState>();
	UMT2QuestManagerComponent* Manager = PlayerState->GetQuestManagerComponent();
	FMT2QuestContext Context; Context.Manager = Manager; Context.PlayerState = PlayerState;
	Manager->ActiveContext = Context;
	const TArray<FString> Lines = {TEXT("return({"), TEXT("[20060] = 50601,"),
		TEXT("[20061] = 50602"), TEXT("})[race]"), TEXT("result = probe.ore(20060)"), TEXT("missing = probe.ore(999)")};
	const TArray<FParsedFunction> Functions = {{TEXT("ore"), {TEXT("race")}, 0, 4}};
	const TMap<FString, FString> Locale;
	FMT2QuestImportResult Result;
	FTranslateState State; State.Locale = &Locale; State.Outer = Manager; State.Result = &Result;
	State.ScriptName = State.QuestName = TEXT("probe"); State.Functions = &Functions;
	const auto Nodes = TranslateBlock(Lines, 4, Lines.Num(), State);
	TestEqual(TEXT("Multiline compact helper translated"), Result.StatementsUnconverted, 0);
	Manager->PushFrame(Nodes); Manager->RunPendingFrames();
	TestEqual(TEXT("Indexed return survives function lowering"), Manager->GetScriptVariable(TEXT("result")).Number, 50601.0);
	TestTrue(TEXT("Missing table key remains nil"), Manager->GetScriptVariable(TEXT("missing")).bIsNil);
	return true;
}

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
	UMT2ItemTemplate* Armor = MakeTemplate(UMT2ItemArmorTemplate::StaticClass(), 200);
	UMT2ItemTemplate* Material = MakeTemplate(UMT2ItemMaterialTemplate::StaticClass(), 300);
	Material->Limits = {{EMT2ItemLimitType::Level, 99}};
	Material->Flags |= UMT2ItemTemplate::StackableFlag;
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
	TestTrue(TEXT("Empty grid space available"), Read(TEXT("pc.enough_inventory('300')")).AsBool());
	TestFalse(TEXT("Unknown item cannot fit"), Read(TEXT("pc.enough_inventory(99999)")).AsBool());
	for (FMT2ItemSlot& Slot : Slots) { Slot.Vnum = 300; Slot.Count = 1; }
	Player->GetInventoryComponent()->RestoreItems(Slots, {});
	TestFalse(TEXT("Existing stack space is not empty grid space"), Read(TEXT("pc.enough_inventory(300)")).AsBool());
	Armor->InventorySize = 3;
	Slots[0] = FMT2ItemSlot(); Slots[1] = FMT2ItemSlot(); Slots[2] = FMT2ItemSlot();
	Player->GetInventoryComponent()->RestoreItems(Slots, {});
	TestFalse(TEXT("Horizontal holes do not fit vertical item"), Read(TEXT("pc.enough_inventory(200)")).AsBool());
	Slots[5] = FMT2ItemSlot(); Slots[10] = FMT2ItemSlot();
	Player->GetInventoryComponent()->RestoreItems(Slots, {});
	TestTrue(TEXT("Contiguous vertical cells fit"), Read(TEXT("pc.enough_inventory(200)")).AsBool());
	for (FMT2ItemSlot& Slot : Slots) { Slot.Vnum = 300; Slot.Count = 1; }
	Slots[40] = FMT2ItemSlot(); Slots[45] = FMT2ItemSlot(); Slots[50] = FMT2ItemSlot();
	Player->GetInventoryComponent()->RestoreItems(Slots, {});
	TestFalse(TEXT("Vertical item cannot cross a page boundary"), Read(TEXT("pc.enough_inventory(200)")).AsBool());
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
