/*
 * Copyright © 2026 Castellese Brian Vincenzo.
 * Licensed under the PolyForm Noncommercial License 1.0.0.
 * See LICENSE for the complete terms.
 */
#pragma once

#include "CoreMinimal.h"

// Import-time syntax only. Runtime values and resumable calls remain ordinary quest nodes.
namespace MT2QuestExpressionSyntax
{
	enum class EKind { Leaf, Unary, Binary, Call, Index };
	struct FNode
	{
		EKind Kind = EKind::Leaf;
		FString Text;
		TArray<TSharedPtr<FNode>> Children;
	};

	struct FParser
	{
		TArray<FString> Tokens;
		int32 Cursor = 0;
		int32 Depth = 0;
		bool bOk = true;

		explicit FParser(const FString& Source)
		{
			for (int32 I = 0; I < Source.Len();)
			{
				if (FChar::IsWhitespace(Source[I])) { ++I; continue; }
				const int32 Start = I;
				const TCHAR Ch = Source[I++];
				if (Ch == TEXT('\'') || Ch == TEXT('"'))
				{
					bool bClosed = false;
					while (I < Source.Len())
					{
						const TCHAR Next = Source[I++];
						if (Next == TEXT('\\')) { if (I < Source.Len()) { ++I; } continue; }
						if (Next == Ch) { bClosed = true; break; }
					}
					bOk &= bClosed;
				}
				else if (FChar::IsAlpha(Ch) || Ch == TEXT('_'))
				{
					while (I < Source.Len() && (FChar::IsAlnum(Source[I]) || Source[I] == TEXT('_') ||
						(Source[I] == TEXT('.') && I + 1 < Source.Len() &&
							(FChar::IsAlpha(Source[I + 1]) || Source[I + 1] == TEXT('_'))))) { ++I; }
				}
				else if (FChar::IsDigit(Ch) || (Ch == TEXT('.') && I < Source.Len() && FChar::IsDigit(Source[I])))
				{
					while (I < Source.Len() && FChar::IsDigit(Source[I])) { ++I; }
					if (I < Source.Len() && Source[I] == TEXT('.') &&
						(I + 1 == Source.Len() || Source[I + 1] != TEXT('.')))
					{
						++I;
						while (I < Source.Len() && FChar::IsDigit(Source[I])) { ++I; }
					}
					if (I < Source.Len() && (Source[I] == TEXT('e') || Source[I] == TEXT('E')))
					{
						++I;
						if (I < Source.Len() && (Source[I] == TEXT('+') || Source[I] == TEXT('-'))) { ++I; }
						while (I < Source.Len() && FChar::IsDigit(Source[I])) { ++I; }
					}
				}
				else if (I < Source.Len() && ((Ch == TEXT('.') && Source[I] == TEXT('.')) ||
					((Ch == TEXT('=') || Ch == TEXT('~') || Ch == TEXT('!') || Ch == TEXT('<') || Ch == TEXT('>')) && Source[I] == TEXT('='))))
				{
					++I;
				}
				Tokens.Add(Source.Mid(Start, I - Start));
			}
		}

		FString Peek() const { return Tokens.IsValidIndex(Cursor) ? Tokens[Cursor] : FString(); }
		bool Take(const TCHAR* Text)
		{
			if (Peek() != Text) { return false; }
			++Cursor;
			return true;
		}
		static int32 Precedence(const FString& Op)
		{
			if (Op == TEXT("or")) { return 1; }
			if (Op == TEXT("and")) { return 2; }
			if (Op == TEXT("==") || Op == TEXT("~=") || Op == TEXT("!=") || Op == TEXT("<") ||
				Op == TEXT("<=") || Op == TEXT(">") || Op == TEXT(">=")) { return 3; }
			if (Op == TEXT("..")) { return 4; }
			if (Op == TEXT("+") || Op == TEXT("-")) { return 5; }
			if (Op == TEXT("*") || Op == TEXT("/") || Op == TEXT("%")) { return 6; }
			return 0;
		}
		TSharedPtr<FNode> Expression(int32 Minimum = 1)
		{
			if (++Depth > 128) { bOk = false; --Depth; return nullptr; }
			TSharedPtr<FNode> Left = Primary();
			while (bOk && Precedence(Peek()) >= Minimum)
			{
				const FString Op = Tokens[Cursor++];
				auto Binary = MakeShared<FNode>();
				Binary->Kind = EKind::Binary;
				Binary->Text = Op;
				Binary->Children = {Left, Expression(Precedence(Op) + (Op == TEXT("..") ? 0 : 1))};
				Left = Binary;
			}
			--Depth;
			return Left;
		}
		TSharedPtr<FNode> Primary()
		{
			if (!bOk || !Tokens.IsValidIndex(Cursor)) { bOk = false; return nullptr; }
			if (Peek() == TEXT("not") || Peek() == TEXT("-"))
			{
				auto Unary = MakeShared<FNode>();
				Unary->Kind = EKind::Unary;
				Unary->Text = Tokens[Cursor++];
				Unary->Children.Add(Expression(7));
				return Unary;
			}
			TSharedPtr<FNode> Node;
			if (Take(TEXT("{")))
			{
				// Constructors without quest calls can be evaluated atomically by the existing
				// runtime parser. Constructors containing calls are rejected during lowering.
				const int32 Start = Cursor - 1;
				int32 Nesting = 1;
				while (Cursor < Tokens.Num() && Nesting > 0)
				{
					const FString Token = Tokens[Cursor++];
					if (Token == TEXT("{")) { ++Nesting; }
					else if (Token == TEXT("}")) { --Nesting; }
				}
				bOk &= Nesting == 0;
				Node = MakeShared<FNode>();
				for (int32 I = Start; I < Cursor; ++I) { Node->Text += Tokens[I] + TEXT(" "); }
			}
			else if (Take(TEXT("(")))
			{
				Node = Expression();
				bOk &= Take(TEXT(")"));
			}
			else
			{
				Node = MakeShared<FNode>();
				Node->Text = Tokens[Cursor++];
				const TCHAR First = Node->Text[0];
				if (!(FChar::IsAlnum(First) || First == TEXT('_') || First == TEXT('.') || First == TEXT('\'') || First == TEXT('"')))
				{
					bOk = false;
					return nullptr;
				}
				if (Take(TEXT("(")))
				{
					Node->Kind = EKind::Call;
					if (!Take(TEXT(")")))
					{
						do { Node->Children.Add(Expression()); } while (bOk && Take(TEXT(",")));
						bOk &= Take(TEXT(")"));
					}
				}
			}
			while (bOk && (Peek() == TEXT("[") || Peek() == TEXT(".")))
			{
				auto Index = MakeShared<FNode>();
				Index->Kind = EKind::Index;
				Index->Children.Add(Node);
				if (Take(TEXT("[")))
				{
					Index->Children.Add(Expression());
					bOk &= Take(TEXT("]"));
				}
				else
				{
					++Cursor;
					const FString Field = Peek();
					if (Field.IsEmpty() || !(FChar::IsAlpha(Field[0]) || Field[0] == TEXT('_'))) { bOk = false; break; }
					++Cursor;
					TArray<FString> Fields;
					Field.ParseIntoArray(Fields, TEXT("."), true);
					for (int32 I = 0; I < Fields.Num(); ++I)
					{
						if (I > 0)
						{
							auto NextIndex = MakeShared<FNode>();
							NextIndex->Kind = EKind::Index;
							NextIndex->Children.Add(Index);
							Index = NextIndex;
						}
						auto Key = MakeShared<FNode>();
						Key->Text = FString::Printf(TEXT("\"%s\""), *Fields[I]);
						Index->Children.Add(Key);
					}
				}
				Node = Index;
			}
			return Node;
		}
	};
}
