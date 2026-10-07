#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Server/MT2ProfilingPorts.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMT2ProfilingPortsTest, "Metin2.Config.FrameProPort",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMT2ProfilingPortsTest::RunTest(const FString& Parameters)
{
	int32 Port = 0;
	TestTrue(TEXT("Derive from game launch port"), MT2ProfilingPorts::TryDeriveFrameProPort(TEXT("-port=11001"), 7777, Port));
	TestEqual(TEXT("Game + 200"), Port, 11201);
	TestTrue(TEXT("Ignore separate voice override"), MT2ProfilingPorts::TryDeriveFrameProPort(TEXT("-voice_port=11101 -port=11002"), 7777, Port));
	TestEqual(TEXT("Next game process gets distinct profiler port"), Port, 11202);
	TestTrue(TEXT("Default URL port used when launch port absent"), MT2ProfilingPorts::TryDeriveFrameProPort(TEXT(""), 7777, Port));
	TestEqual(TEXT("Default + 200"), Port, 7977);
	Port = 123;
	TestFalse(TEXT("Explicit FramePro override preserved"), MT2ProfilingPorts::TryDeriveFrameProPort(TEXT("-port=11001 -FrameProPort=9000"), 7777, Port));
	TestEqual(TEXT("No rewrite for explicit override"), Port, 123);
	TestFalse(TEXT("No overflow or clamping to another process's port"), MT2ProfilingPorts::TryDeriveFrameProPort(TEXT("-port=65336"), 7777, Port));
	TestFalse(TEXT("Invalid game port rejected"), MT2ProfilingPorts::TryDeriveFrameProPort(TEXT("-port=0"), 7777, Port));
	TestTrue(TEXT("Highest usable game port"), MT2ProfilingPorts::TryDeriveFrameProPort(TEXT("-port=65335"), 7777, Port));
	TestEqual(TEXT("Highest TCP port"), Port, 65535);
	return true;
}
#endif
