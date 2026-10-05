/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Animation/MT2AnimationMotionData.h"
#include "MT2AssetScanner.h"
#include "Misc/Paths.h"

namespace MT2MotionScriptParser
{
	inline bool ReadTokens(const FString& Block, const TCHAR* Key, TArray<FString>& OutTokens)
	{
		TArray<FString> Lines;
		Block.ParseIntoArrayLines(Lines, false);
		for (FString Line : Lines)
		{
			Line.TrimStartAndEndInline();
			TArray<FString> Tokens;
			Line.ParseIntoArrayWS(Tokens);
			if (!Tokens.IsEmpty() && Tokens[0].Equals(Key, ESearchCase::IgnoreCase))
			{
				OutTokens = MoveTemp(Tokens);
				return true;
			}
		}
		return false;
	}

	inline bool ReadFloat(const FString& Block, const TCHAR* Key, float& OutValue)
	{
		TArray<FString> Tokens;
		if (!ReadTokens(Block, Key, Tokens) || Tokens.Num() < 2)
		{
			return false;
		}
		OutValue = FCString::Atof(*Tokens[1]);
		return true;
	}

	inline bool ReadBool(const FString& Block, const TCHAR* Key, bool& OutValue)
	{
		float Value = 0.0f;
		if (!ReadFloat(Block, Key, Value))
		{
			return false;
		}
		OutValue = !FMath::IsNearlyZero(Value);
		return true;
	}

	inline bool ReadVector(const FString& Block, const TCHAR* Key, FVector& OutValue)
	{
		TArray<FString> Tokens;
		if (!ReadTokens(Block, Key, Tokens) || Tokens.Num() < 4)
		{
			return false;
		}
		OutValue = FVector(
			FCString::Atof(*Tokens[1]), FCString::Atof(*Tokens[2]), FCString::Atof(*Tokens[3]));
		return true;
	}

	inline bool ReadQuoted(const FString& Block, const TCHAR* Key, FString& OutValue)
	{
		TArray<FString> Lines;
		Block.ParseIntoArrayLines(Lines, false);
		for (FString Line : Lines)
		{
			Line.TrimStartAndEndInline();
			if (!Line.StartsWith(Key, ESearchCase::IgnoreCase))
			{
				continue;
			}
			int32 FirstQuote = INDEX_NONE;
			int32 LastQuote = INDEX_NONE;
			if (Line.FindChar(TCHAR('"'), FirstQuote) && Line.FindLastChar(TCHAR('"'), LastQuote)
				&& LastQuote > FirstQuote)
			{
				OutValue = Line.Mid(FirstQuote + 1, LastQuote - FirstQuote - 1);
				return true;
			}
		}
		return false;
	}

	inline void ExtractEventBlocks(const FString& Script, TArray<FString>& OutBlocks)
	{
		OutBlocks.Reset();
		int32 SearchFrom = 0;
		while (SearchFrom < Script.Len())
		{
			const int32 Header = Script.Find(
				TEXT("Group Event"), ESearchCase::IgnoreCase, ESearchDir::FromStart, SearchFrom);
			if (Header == INDEX_NONE)
			{
				break;
			}
			const int32 OpenBrace = Script.Find(
				TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Header);
			if (OpenBrace == INDEX_NONE)
			{
				break;
			}
			int32 Depth = 0;
			int32 CloseBrace = INDEX_NONE;
			for (int32 Index = OpenBrace; Index < Script.Len(); ++Index)
			{
				if (Script[Index] == TCHAR('{'))
				{
					++Depth;
				}
				else if (Script[Index] == TCHAR('}') && --Depth == 0)
				{
					CloseBrace = Index;
					break;
				}
			}
			if (CloseBrace == INDEX_NONE)
			{
				break;
			}
			OutBlocks.Add(Script.Mid(OpenBrace + 1, CloseBrace - OpenBrace - 1));
			SearchFrom = CloseBrace + 1;
		}
	}

	inline FString BuildImportedEffectPath(
		FString LegacyPath, const FMT2AssetRecord& MotionRecord, FString DestinationRoot)
	{
		LegacyPath.TrimStartAndEndInline();
		LegacyPath = LegacyPath.TrimQuotes();
		LegacyPath.ReplaceInline(TEXT("\\"), TEXT("/"));
		while (LegacyPath.Contains(TEXT("//")))
		{
			LegacyPath.ReplaceInline(TEXT("//"), TEXT("/"));
		}
		LegacyPath.RemoveFromStart(TEXT("d:/"), ESearchCase::IgnoreCase);

		if (!LegacyPath.Contains(TEXT("/")) ||
			(!LegacyPath.StartsWith(TEXT("ymir work/"), ESearchCase::IgnoreCase)
				&& !LegacyPath.StartsWith(TEXT("locale/"), ESearchCase::IgnoreCase)))
		{
			LegacyPath = FPaths::GetPath(MotionRecord.ContentPath) / LegacyPath;
		}

		TArray<FString> Parts;
		LegacyPath.ParseIntoArray(Parts, TEXT("/"), true);
		if (Parts.IsEmpty())
		{
			return FString();
		}
		const FString BaseName = FMT2AssetScanner::SanitizePackagePathSegment(
			FPaths::GetBaseFilename(Parts.Pop()));
		DestinationRoot.RemoveFromEnd(TEXT("/"));
		FString PackagePath = DestinationRoot;
		for (const FString& Part : Parts)
		{
			PackagePath /= FMT2AssetScanner::SanitizePackagePathSegment(Part);
		}
		const FString AssetName = BaseName.StartsWith(TEXT("PS_"), ESearchCase::IgnoreCase)
			? BaseName : TEXT("PS_") + BaseName;
		PackagePath /= AssetName;
		return PackagePath + TEXT(".") + AssetName;
	}

	inline void PopulateEffectEvents(
		const FString& Script, const FMT2AssetRecord& MotionRecord,
		const FString& DestinationRoot, UMT2AnimationMotionData* MotionData)
	{
		if (!MotionData)
		{
			return;
		}
		MotionData->EffectEvents.Reset();
		TArray<FString> EventBlocks;
		ExtractEventBlocks(Script, EventBlocks);
		for (const FString& Block : EventBlocks)
		{
			float EventType = 0.0f;
			if (!ReadFloat(Block, TEXT("MotionEventType"), EventType)
				|| FMath::RoundToInt(EventType) != 1)
			{
				continue;
			}

			FString EffectFileName;
			if (!ReadQuoted(Block, TEXT("EffectFileName"), EffectFileName))
			{
				continue;
			}
			FMT2MotionEffectEvent& Event = MotionData->EffectEvents.AddDefaulted_GetRef();
			ReadFloat(Block, TEXT("StartingTime"), Event.TimeSeconds);
			ReadBool(Block, TEXT("IndependentFlag"), Event.bIndependent);
			ReadBool(Block, TEXT("AttachingEnable"), Event.bAttach);
			ReadBool(Block, TEXT("FollowingEnable"), Event.bFollow);
			FString BoneName;
			if (ReadQuoted(Block, TEXT("AttachingBoneName"), BoneName))
			{
				Event.BoneName = FName(*BoneName);
			}
			FVector LegacyPosition = FVector::ZeroVector;
			ReadVector(Block, TEXT("EffectPosition"), LegacyPosition);
			Event.RelativeLocation = FVector(-LegacyPosition.Y, LegacyPosition.X, LegacyPosition.Z);
			Event.SourceEffectPath = EffectFileName;
			const FString ObjectPath =
				BuildImportedEffectPath(EffectFileName, MotionRecord, DestinationRoot);
			if (!ObjectPath.IsEmpty())
			{
				Event.Effect = TSoftObjectPtr<UParticleSystem>(FSoftObjectPath(ObjectPath));
			}
		}
		MotionData->EffectEvents.Sort([](const FMT2MotionEffectEvent& A, const FMT2MotionEffectEvent& B)
		{
			return A.TimeSeconds < B.TimeSeconds;
		});
	}
}
