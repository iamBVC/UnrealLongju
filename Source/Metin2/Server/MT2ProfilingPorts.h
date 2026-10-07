#pragma once

#include "CoreMinimal.h"
#include "Misc/Parse.h"
#include "String/LexFromString.h"

namespace MT2ProfilingPorts
{
	inline constexpr int32 FrameProPortOffset = 200;

	// FramePro is process-wide: resolve once before its first frame/listener startup.
	inline bool TryDeriveFrameProPort(const TCHAR* CommandLine, int32 DefaultGamePort, int32& OutPort)
	{
		int32 GamePort = DefaultGamePort;
		const TCHAR* Cursor = CommandLine;
		FString Token;
		bool bFoundGamePort = false;
		while (FParse::Token(Cursor, Token, true))
		{
			Token.RemoveFromStart(TEXT("-"));
			if (Token.StartsWith(TEXT("FrameProPort="), ESearchCase::IgnoreCase)) { return false; }
			// FParse::Value on the entire command line also matches voice_port/co_port.
			if (!bFoundGamePort && Token.StartsWith(TEXT("port="), ESearchCase::IgnoreCase))
			{
				if (!LexTryParseString(GamePort, *Token.Mid(5))) { return false; }
				bFoundGamePort = true;
			}
		}
		if (GamePort < 1 || GamePort > 65535 - FrameProPortOffset) { return false; }
		OutPort = GamePort + FrameProPortOffset;
		return true;
	}
}
