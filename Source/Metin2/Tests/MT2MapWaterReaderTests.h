#pragma once
#if WITH_DEV_AUTOMATION_TESTS
#include "Importers/MT2MapWaterReader.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2WaterReaderTest, "Metin2.World.Water.Reader",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2WaterReaderTest::RunTest(const FString& Parameters)
{
	TArray<uint8> File; File.Init(0xff, 7 + 128 * 128);
	File[0] = 0x32; File[1] = 0x15; // 5426, little endian.
	File[2] = 128; File[3] = 0; File[4] = 128; File[5] = 0; File[6] = 1;
	File[7] = 0; File.Append({0xe8, 0x03});
	TArray<float> Heights; FString Error;
	TestTrue(TEXT("16-bit legacy height table"), FMT2MapWaterReader::Decode(File, Heights, .5f, Error));
	TestEqual(TEXT("HeightScale applied exactly once"), Heights[0], 500.f);
	TestEqual(TEXT("Dry cell has no invented height"), Heights[1], -MAX_flt);
	File.Append({0, 0});
	TestTrue(TEXT("32-bit legacy height table"), FMT2MapWaterReader::Decode(File, Heights, .5f, Error));
	TestEqual(TEXT("Same surface height"), Heights[0], 500.f);
	File[7] = 1;
	TestFalse(TEXT("Out-of-range layer rejected"), FMT2MapWaterReader::Decode(File, Heights, .5f, Error));
	TestEqual(TEXT("Decode failure preserves output"), Heights[0], 500.f);
	File[7] = 0; File.RemoveAt(File.Num() - 1);
	TestFalse(TEXT("Truncated height table rejected"), FMT2MapWaterReader::Decode(File, Heights, .5f, Error));
	File[0] = 0;
	TestFalse(TEXT("Bad magic rejected"), FMT2MapWaterReader::Decode(File, Heights, .5f, Error));
	return true;
}
#endif
