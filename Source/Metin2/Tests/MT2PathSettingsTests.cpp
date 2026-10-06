#if WITH_DEV_AUTOMATION_TESTS
#include "Config/MT2PathSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2PathCatalogTest, "Metin2.Config.PathCatalog", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2PathCatalogTest::RunTest(const FString& Parameters)
{
	const UMT2PathSettings* Settings = GetDefault<UMT2PathSettings>();
	TestTrue(TEXT("Asset references loaded from Game INI"), !Settings->Assets.IsEmpty());
	TestTrue(TEXT("Locations loaded from Game INI"), !Settings->Locations.IsEmpty());
	TSet<FName> Keys;
	for (const auto& Entry : Settings->Assets)
	{
		TestFalse(TEXT("Asset key is named"), Entry.Key.IsNone());
		TestFalse(*FString::Printf(TEXT("Unique asset key %s"), *Entry.Key.ToString()), Keys.Contains(Entry.Key));
		Keys.Add(Entry.Key);
		TestTrue(TEXT("Asset is a serialized soft reference"), Entry.Value.IsValid());
		TestEqual(TEXT("Catalog preserves the asset reference"), FString(UMT2PathSettings::Path(Entry.Key)), Entry.Value.ToString());
	}
	for (const auto& Entry : Settings->Locations)
	{
		TestFalse(TEXT("Location key is named"), Entry.Key.IsNone());
		TestFalse(*FString::Printf(TEXT("Unique location key %s"), *Entry.Key.ToString()), Keys.Contains(Entry.Key));
		Keys.Add(Entry.Key);
		TestFalse(TEXT("Location is not empty"), Entry.Value.IsEmpty());
	}
	TestEqual(TEXT("External files inherit the dump root"), FString(UMT2PathSettings::Path(TEXT("Legacy_db"))),
		FString(UMT2PathSettings::Path(TEXT("LegacyDumpRoot"))) / UMT2PathSettings::Path(TEXT("Part_db")));
	TestEqual(TEXT("Reports inherit platform-aware Saved directory"), FString(UMT2PathSettings::Path(TEXT("ImporterWorkingDirectory"))),
		FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("MT2UE")));
	const TCHAR* Stable = UMT2PathSettings::Path(TEXT("VnumRegistry"));
	TestTrue(TEXT("Catalog returns stable pointer storage"), Stable == UMT2PathSettings::Path(TEXT("VnumRegistry")));
	const FString ExpectedMap = FString(UMT2PathSettings::Path(TEXT("GameMapTemplate"), TEXT("%s"))).Replace(TEXT("%s"), TEXT("FixtureMap"));
	TestEqual(TEXT("Safe string interpolation"), UMT2PathSettings::Format(TEXT("GameMapTemplate"), TEXT("%s"), TEXT("FixtureMap")), ExpectedMap);
	const FString ExpectedGauge = FString(UMT2PathSettings::Path(TEXT("ymir_work_ui_pattern_Name_T_Index"), TEXT("%s%02d%02d")))
		.Replace(TEXT("%s"), TEXT("HPGauge")).Replace(TEXT("%02d"), TEXT("03"));
	TestEqual(TEXT("Number padding retained"), UMT2PathSettings::Format(TEXT("ymir_work_ui_pattern_Name_T_Index"), TEXT("%s%02d%02d"), TEXT("HPGauge"), 3, 3), ExpectedGauge);
	TestTrue(TEXT("Format signature accepted"), UMT2PathSettings::HasFormat(TEXT("prefix/%s/%02d"), TEXT("%s%02d")));
	TestFalse(TEXT("Argument type substitution rejected"), UMT2PathSettings::HasFormat(TEXT("%d"), TEXT("%s")));
	TestFalse(TEXT("Extra argument rejected"), UMT2PathSettings::HasFormat(TEXT("%s%s"), TEXT("%s")));
	TestFalse(TEXT("Write-through format rejected"), UMT2PathSettings::HasFormat(TEXT("%n"), TEXT("")));
	TestFalse(TEXT("Incomplete format rejected"), UMT2PathSettings::HasFormat(TEXT("broken%"), TEXT("")));
	return true;
}
#endif
