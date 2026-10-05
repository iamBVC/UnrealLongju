/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Skills/MT2SkillFormula.h"

namespace
{
	// Recursive-descent parser over the old CPoly grammar. Variable names are matched
	// case-insensitively because skilltable mixes "MinATK" and "minatk" spellings.
	class FSkillPolyParser
	{
	public:
		FSkillPolyParser(const FString& InText, const TMap<FString, double>& InVariables)
			: Text(InText), Variables(InVariables)
		{
		}

		bool Parse(double& OutValue)
		{
			Position = 0;
			bFailed = false;
			const double Value = ParseSum();
			SkipWhitespace();
			if (bFailed || Position != Text.Len())
			{
				return false;
			}
			OutValue = Value;
			return true;
		}

	private:
		void SkipWhitespace()
		{
			while (Position < Text.Len() && FChar::IsWhitespace(Text[Position]))
			{
				++Position;
			}
		}

		bool Consume(TCHAR Character)
		{
			SkipWhitespace();
			if (Position < Text.Len() && Text[Position] == Character)
			{
				++Position;
				return true;
			}
			return false;
		}

		double ParseSum()
		{
			double Value = ParseProduct();
			while (!bFailed)
			{
				if (Consume(TEXT('+'))) { Value += ParseProduct(); }
				else if (Consume(TEXT('-'))) { Value -= ParseProduct(); }
				else { break; }
			}
			return Value;
		}

		double ParseProduct()
		{
			double Value = ParseUnary();
			while (!bFailed)
			{
				if (Consume(TEXT('*'))) { Value *= ParseUnary(); }
				else if (Consume(TEXT('/')))
				{
					const double Divisor = ParseUnary();
					if (FMath::IsNearlyZero(Divisor)) { bFailed = true; return 0.0; }
					Value /= Divisor;
				}
				else { break; }
			}
			return Value;
		}

		double ParseUnary()
		{
			if (Consume(TEXT('-'))) { return -ParseUnary(); }
			if (Consume(TEXT('+'))) { return ParseUnary(); }
			return ParseAtom();
		}

		double ParseAtom()
		{
			SkipWhitespace();
			if (Consume(TEXT('(')))
			{
				const double Value = ParseSum();
				if (!Consume(TEXT(')'))) { bFailed = true; }
				return Value;
			}
			if (Position < Text.Len() && (FChar::IsDigit(Text[Position]) || Text[Position] == TEXT('.')))
			{
				const int32 Start = Position;
				while (Position < Text.Len() && (FChar::IsDigit(Text[Position]) || Text[Position] == TEXT('.')))
				{
					++Position;
				}
				return FCString::Atod(*Text.Mid(Start, Position - Start));
			}
			if (Position < Text.Len() && (FChar::IsAlpha(Text[Position]) || Text[Position] == TEXT('_')))
			{
				const int32 Start = Position;
				while (Position < Text.Len() && (FChar::IsAlnum(Text[Position]) || Text[Position] == TEXT('_')))
				{
					++Position;
				}
				const FString Identifier = Text.Mid(Start, Position - Start).ToLower();

				if (Identifier == TEXT("floor") || Identifier == TEXT("ceil"))
				{
					if (!Consume(TEXT('('))) { bFailed = true; return 0.0; }
					const double Value = ParseSum();
					if (!Consume(TEXT(')'))) { bFailed = true; }
					return Identifier == TEXT("floor") ? FMath::FloorToDouble(Value) : FMath::CeilToDouble(Value);
				}
				if (Identifier == TEXT("number"))
				{
					// number(a, b): the old server's inclusive random range.
					if (!Consume(TEXT('('))) { bFailed = true; return 0.0; }
					const double Min = ParseSum();
					if (!Consume(TEXT(','))) { bFailed = true; return 0.0; }
					const double Max = ParseSum();
					if (!Consume(TEXT(')'))) { bFailed = true; }
					return FMath::FRandRange(FMath::Min(Min, Max), FMath::Max(Min, Max));
				}
				if (const double* Variable = Variables.Find(Identifier))
				{
					return *Variable;
				}
				bFailed = true;
				return 0.0;
			}
			bFailed = true;
			return 0.0;
		}

		const FString& Text;
		const TMap<FString, double>& Variables;
		int32 Position = 0;
		bool bFailed = false;
	};
}

bool MT2SkillFormula::Evaluate(
	const FString& Expression, const TMap<FString, double>& Variables, double& OutValue)
{
	if (Expression.TrimStartAndEnd().IsEmpty())
	{
		return false;
	}
	FSkillPolyParser Parser(Expression, Variables);
	return Parser.Parse(OutValue);
}
