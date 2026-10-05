/* Pattern semantics adapted from Lua 5.0.3 lstrlib.c.
 * Copyright (C) 1994-2006 Tecgraf, PUC-Rio. All rights reserved.
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 * and associated documentation files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge, publish, distribute,
 * sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "Quests/MT2QuestLuaPattern.h"
#include "Misc/ScopeExit.h"

namespace
{
	struct FCapture { int32 Start = 0; int32 Length = -1; };
	struct FMatcher
	{
		TArray<uint8> Source, Pattern;
		TArray<FCapture, TInlineAllocator<32>> Captures;
		FString Error;
		int32 Work = 1000000;
		int32 SourceLength = 0;
		bool Spend(int32 Cost = 1)
		{
			if (Cost <= Work && Error.IsEmpty()) { Work -= Cost; return true; }
			if (Error.IsEmpty()) { Error = TEXT("Lua pattern work limit exceeded"); }
			return false;
		}
		int32 Fail(const TCHAR* Reason) { Error = Reason; return INDEX_NONE; }
		static bool Class(uint8 Byte, uint8 Kind)
		{
			const bool Lower = Kind >= 'a' && Kind <= 'z';
			const uint8 Name = Kind >= 'A' && Kind <= 'Z' ? Kind + ('a' - 'A') : Kind;
			const bool Alpha = (Byte >= 'a' && Byte <= 'z') || (Byte >= 'A' && Byte <= 'Z');
			const bool Digit = Byte >= '0' && Byte <= '9';
			bool Result = false;
			switch (Name)
			{
			case 'a': Result = Alpha; break;
			case 'c': Result = Byte < 32 || Byte == 127; break;
			case 'd': Result = Digit; break;
			case 'l': Result = Byte >= 'a' && Byte <= 'z'; break;
			case 'p': Result = Byte >= 33 && Byte <= 126 && !Alpha && !Digit; break;
			case 's': Result = Byte == 32 || (Byte >= 9 && Byte <= 13); break;
			case 'u': Result = Byte >= 'A' && Byte <= 'Z'; break;
			case 'w': Result = Alpha || Digit; break;
			case 'x': Result = Digit || (Byte >= 'a' && Byte <= 'f') || (Byte >= 'A' && Byte <= 'F'); break;
			case 'z': Result = Byte == 0; break;
			default: return Byte == Kind;
			}
			return Lower ? Result : !Result;
		}
		int32 ClassEnd(int32 Position)
		{
			if (Pattern[Position] == '%')
			{
				return Pattern[Position + 1] ? Position + 2 : Fail(TEXT("Pattern ends with %"));
			}
			if (Pattern[Position] != '[') { return Position + 1; }
			int32 Cursor = Position + 1;
			if (Pattern[Cursor] == '^') { ++Cursor; }
			do
			{
				if (!Spend()) { return INDEX_NONE; }
				if (!Pattern[Cursor]) { return Fail(TEXT("Pattern missing ]")); }
				if (Pattern[Cursor++] == '%' && Pattern[Cursor]) { ++Cursor; }
			} while (Pattern[Cursor] != ']');
			return Cursor + 1;
		}
		bool Bracket(uint8 Byte, int32 Position, int32 End)
		{
			bool Positive = true;
			if (Pattern[Position + 1] == '^') { Positive = false; ++Position; }
			while (++Position < End)
			{
				if (!Spend()) { return false; }
				if (Pattern[Position] == '%')
				{
					if (Class(Byte, Pattern[++Position])) { return Positive; }
				}
				else if (Pattern[Position + 1] == '-' && Position + 2 < End)
				{
					Position += 2;
					if (Pattern[Position - 2] <= Byte && Byte <= Pattern[Position]) { return Positive; }
				}
				else if (Pattern[Position] == Byte) { return Positive; }
			}
			return !Positive;
		}
		bool Single(uint8 Byte, int32 Position, int32 End)
		{
			if (Pattern[Position] == '.') { return true; }
			if (Pattern[Position] == '%') { return Class(Byte, Pattern[Position + 1]); }
			if (Pattern[Position] == '[') { return Bracket(Byte, Position, End - 1); }
			return Byte == Pattern[Position];
		}
		int32 CaptureIndex(uint8 Digit)
		{
			const int32 Index = Digit - '1';
			if (!Captures.IsValidIndex(Index) || Captures[Index].Length == -1) { return Fail(TEXT("Invalid capture index")); }
			return Index;
		}
		int32 Match(int32 SourcePosition, int32 PatternPosition, int32 Depth = 0)
		{
			if (Depth >= 128) { return Fail(TEXT("Lua pattern depth limit exceeded")); }
			const auto Saved = Captures;
			bool bMatched = false;
			ON_SCOPE_EXIT { if (!bMatched) { Captures = Saved; } };
			auto Success = [&](int32 Result) { bMatched = Result != INDEX_NONE; return Result; };
			while (Spend())
			{
				const uint8 Item = Pattern[PatternPosition];
				if (!Item) { return Success(SourcePosition); }
				if (Item == '(')
				{
					if (Captures.Num() >= 32) { return Fail(TEXT("Too many captures")); }
					const bool PositionCapture = Pattern[PatternPosition + 1] == ')';
					Captures.Add({SourcePosition, PositionCapture ? -2 : -1});
					return Success(Match(SourcePosition, PatternPosition + (PositionCapture ? 2 : 1), Depth + 1));
				}
				if (Item == ')')
				{
					int32 Index = Captures.Num() - 1;
					while (Index >= 0 && Captures[Index].Length != -1) { --Index; }
					if (Index < 0) { return Fail(TEXT("Invalid pattern capture")); }
					Captures[Index].Length = SourcePosition - Captures[Index].Start;
					return Success(Match(SourcePosition, PatternPosition + 1, Depth + 1));
				}
				if (Item == '$' && !Pattern[PatternPosition + 1]) { return SourcePosition == SourceLength ? Success(SourcePosition) : INDEX_NONE; }
				if (Item == '%' && Pattern[PatternPosition + 1] == 'b')
				{
					if (!Pattern.IsValidIndex(PatternPosition + 3) || !Pattern[PatternPosition + 2] || !Pattern[PatternPosition + 3]) { return Fail(TEXT("Unbalanced pattern")); }
					if (Source[SourcePosition] != Pattern[PatternPosition + 2]) { return INDEX_NONE; }
					int32 Balance = 1;
					while (++SourcePosition < SourceLength)
					{
						if (!Spend()) { return INDEX_NONE; }
						if (Source[SourcePosition] == Pattern[PatternPosition + 3]) { if (--Balance == 0) { break; } }
						else if (Source[SourcePosition] == Pattern[PatternPosition + 2]) { ++Balance; }
					}
					if (Balance) { return INDEX_NONE; }
					++SourcePosition; PatternPosition += 4; continue;
				}
				if (Item == '%' && Pattern[PatternPosition + 1] == 'f')
				{
					PatternPosition += 2;
					if (Pattern[PatternPosition] != '[') { return Fail(TEXT("Missing [ after %f")); }
					const int32 End = ClassEnd(PatternPosition);
					if (End == INDEX_NONE) { return INDEX_NONE; }
					const uint8 Previous = SourcePosition ? Source[SourcePosition - 1] : 0;
					if (Bracket(Previous, PatternPosition, End - 1) || !Bracket(Source[SourcePosition], PatternPosition, End - 1)) { return INDEX_NONE; }
					PatternPosition = End; continue;
				}
				if (Item == '%' && Pattern[PatternPosition + 1] >= '0' && Pattern[PatternPosition + 1] <= '9')
				{
					const int32 Index = CaptureIndex(Pattern[PatternPosition + 1]);
					if (Index == INDEX_NONE) { return INDEX_NONE; }
					const FCapture& Capture = Captures[Index];
					if (Capture.Length < 0 || Capture.Length > SourceLength - SourcePosition) { return INDEX_NONE; }
					if (!Spend(Capture.Length)) { return INDEX_NONE; }
					if (FMemory::Memcmp(Source.GetData() + Capture.Start, Source.GetData() + SourcePosition, Capture.Length) != 0) { return INDEX_NONE; }
					SourcePosition += Capture.Length; PatternPosition += 2; continue;
				}
				const int32 End = ClassEnd(PatternPosition);
				if (End == INDEX_NONE) { return INDEX_NONE; }
				const bool bSingle = SourcePosition < SourceLength && Single(Source[SourcePosition], PatternPosition, End);
				const uint8 Suffix = Pattern[End];
				if (Suffix == '?')
				{
					if (bSingle)
					{
						const int32 Result = Match(SourcePosition + 1, End + 1, Depth + 1);
						if (Result != INDEX_NONE) { return Success(Result); }
						if (!Error.IsEmpty()) { return INDEX_NONE; }
					}
					PatternPosition = End + 1; continue;
				}
				if (Suffix == '*' || Suffix == '+' || Suffix == '-')
				{
					if (Suffix == '+' && !bSingle) { return INDEX_NONE; }
					int32 Minimum = SourcePosition + (Suffix == '+' ? 1 : 0);
					int32 Maximum = Minimum;
					if (Suffix != '-')
					{
						while (Maximum < SourceLength && Single(Source[Maximum], PatternPosition, End)) { if (!Spend()) { return INDEX_NONE; } ++Maximum; }
					}
					for (int32 Candidate = Suffix == '-' ? Minimum : Maximum;; Candidate += Suffix == '-' ? 1 : -1)
					{
						const int32 Result = Match(Candidate, End + 1, Depth + 1);
						if (Result != INDEX_NONE) { return Success(Result); }
						if (!Error.IsEmpty()) { return INDEX_NONE; }
						if (Suffix == '-') { if (Candidate >= SourceLength || !Single(Source[Candidate], PatternPosition, End)) { break; } }
						else if (Candidate == Minimum) { break; }
					}
					return INDEX_NONE;
				}
				if (!bSingle) { return INDEX_NONE; }
				++SourcePosition; PatternPosition = End;
			}
			return INDEX_NONE;
		}
	};
	TArray<uint8> Bytes(const FString& Text)
	{
		const FTCHARToUTF8 Converted(*Text, Text.Len());
		TArray<uint8> Result;
		Result.Append(reinterpret_cast<const uint8*>(Converted.Get()), Converted.Length());
		Result.Add(0); return Result;
	}
}

bool MT2QuestLuaPattern::Find(const FString& Source, const FString& Pattern, int32 InitialPosition, bool bPlain,
	bool& bOutFound, int32& OutStart, int32& OutEnd, TArray<FCaptureValue>& OutCaptures, FString& OutError)
{
	FMatcher Matcher; Matcher.Source = Bytes(Source); Matcher.Pattern = Bytes(Pattern);
	Matcher.SourceLength = Matcher.Source.Num() - 1;
	bOutFound = false; OutStart = OutEnd = 0; OutCaptures.Reset(); OutError.Empty();
	const int64 Relative = InitialPosition >= 0 ? InitialPosition : int64(Matcher.SourceLength) + InitialPosition + 1;
	const int32 Start = static_cast<int32>(FMath::Clamp<int64>(Relative - 1, 0, Matcher.SourceLength));
	bool bHasSpecial = false;
	for (uint8 Byte : Matcher.Pattern)
	{
		if (!Byte) { break; }
		if (Byte == '^' || Byte == '$' || Byte == '*' || Byte == '+' || Byte == '?' || Byte == '.' ||
			Byte == '(' || Byte == '[' || Byte == '%' || Byte == '-') { bHasSpecial = true; break; }
	}
	if (bPlain || !bHasSpecial)
	{
		const int32 Length = Matcher.Pattern.Num() - 1;
		for (int32 Position = Start; Position <= Matcher.SourceLength - Length; ++Position)
		{
			if (!Matcher.Spend(Length + 1)) { OutError = Matcher.Error; return false; }
			if (FMemory::Memcmp(Matcher.Source.GetData() + Position, Matcher.Pattern.GetData(), Length) == 0)
			{
				bOutFound = true; OutStart = Position + 1; OutEnd = Position + Length; return true;
			}
		}
		return true;
	}
	const bool Anchor = Matcher.Pattern[0] == '^';
	for (int32 Position = Start; Position <= Matcher.SourceLength; ++Position)
	{
		Matcher.Captures.Reset();
		const int32 End = Matcher.Match(Position, Anchor ? 1 : 0);
		if (!Matcher.Error.IsEmpty()) { OutError = Matcher.Error; return false; }
		if (End != INDEX_NONE)
		{
			TArray<FCaptureValue> Captures;
			for (const FCapture& Capture : Matcher.Captures)
			{
				if (Capture.Length == -1) { OutError = TEXT("Unfinished capture"); return false; }
				FCaptureValue Value; Value.bPosition = Capture.Length == -2;
				if (Value.bPosition) { Value.Position = Capture.Start + 1; }
				else if (Capture.Length)
				{
					const uint8* Data = Matcher.Source.GetData() + Capture.Start;
					const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Data), Capture.Length);
					Value.Text = FString(Converted.Length(), Converted.Get());
					const TArray<uint8> RoundTrip = Bytes(Value.Text);
					if (RoundTrip.Num() - 1 != Capture.Length || FMemory::Memcmp(RoundTrip.GetData(), Data, Capture.Length) != 0)
					{
						OutError = TEXT("Lua capture is not representable as UTF-8"); return false;
					}
				}
				Captures.Add(MoveTemp(Value));
			}
			bOutFound = true; OutStart = Position + 1; OutEnd = End; OutCaptures = MoveTemp(Captures); return true;
		}
		if (Anchor) { break; }
	}
	return true;
}

bool MT2QuestLuaPattern::Substitute(const FString& Source, const FString& Pattern, const FString& Replacement,
	int32 Maximum, FString& OutText, int32& OutCount, FString& OutError)
{
	FMatcher Matcher; Matcher.Source = Bytes(Source); Matcher.Pattern = Bytes(Pattern);
	Matcher.SourceLength = Matcher.Source.Num() - 1;
	const TArray<uint8> Replace = Bytes(Replacement);
	// Snapshot all inputs before clearing outputs: callers may replace a string in place.
	OutText.Empty(); OutCount = 0; OutError.Empty();
	TArray<uint8> Output;
	int32 Count = 0, Position = 0;
	const bool Anchor = Matcher.Pattern[0] == '^';
	auto Append = [&](const uint8* Data, int32 Length)
	{
		if (Length > 1024 * 1024 - Output.Num()) { Matcher.Error = TEXT("Lua substitution output limit exceeded"); return false; }
		Output.Append(Data, Length); return true;
	};
	while (Count < Maximum)
	{
		Matcher.Captures.Reset();
		const int32 End = Matcher.Match(Position, Anchor ? 1 : 0);
		if (!Matcher.Error.IsEmpty()) { break; }
		if (End != INDEX_NONE)
		{
			++Count;
			for (int32 Index = 0; Index < Replace.Num() - 1; ++Index)
			{
				if (Replace[Index] != '%') { if (!Append(Replace.GetData() + Index, 1)) { break; } continue; }
				const uint8 Next = Replace[++Index];
				if (Next < '0' || Next > '9') { if (!Append(&Next, 1)) { break; } continue; }
				const int32 CaptureIndex = Matcher.CaptureIndex(Next);
				if (CaptureIndex == INDEX_NONE) { break; }
				const FCapture& Capture = Matcher.Captures[CaptureIndex];
				if (Capture.Length == -2)
				{
					const TArray<uint8> Number = Bytes(FString::FromInt(Capture.Start + 1));
					if (!Append(Number.GetData(), Number.Num() - 1)) { break; }
				}
				else if (!Append(Matcher.Source.GetData() + Capture.Start, Capture.Length)) { break; }
			}
		}
		if (!Matcher.Error.IsEmpty()) { break; }
		if (End > Position) { Position = End; }
		else if (Position < Matcher.SourceLength) { if (!Append(Matcher.Source.GetData() + Position++, 1)) { break; } }
		else { break; }
		if (Anchor) { break; }
	}
	if (!Matcher.Error.IsEmpty()) { OutError = Matcher.Error; return false; }
	if (!Append(Matcher.Source.GetData() + Position, Matcher.SourceLength - Position)) { OutError = Matcher.Error; return false; }
	FString Text;
	if (!Output.IsEmpty())
	{
		const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Output.GetData()), Output.Num());
		Text = FString(Converted.Length(), Converted.Get());
		const TArray<uint8> RoundTrip = Bytes(Text);
		if (RoundTrip.Num() - 1 != Output.Num() || FMemory::Memcmp(RoundTrip.GetData(), Output.GetData(), Output.Num()) != 0)
		{
			OutError = TEXT("Lua byte-string result is not representable as UTF-8"); return false;
		}
	}
	OutText = MoveTemp(Text); OutCount = Count; return true;
}
