/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Importers/MT2MapEnvironmentReader.h"

#include "Misc/FileHelper.h"

namespace
{
	bool ParseBool(const TArray<FString>& Tokens, bool& OutValue)
	{
		if (Tokens.Num() < 2) return false;
		OutValue = FCString::Atoi(*Tokens[1]) != 0;
		return true;
	}

	bool ParseFloat(const TArray<FString>& Tokens, float& OutValue)
	{
		if (Tokens.Num() < 2) return false;
		OutValue = FCString::Atof(*Tokens[1]);
		return true;
	}

	bool ParseVector(const TArray<FString>& Tokens, FVector& OutValue)
	{
		if (Tokens.Num() < 4) return false;
		OutValue = FVector(
			FCString::Atof(*Tokens[1]), FCString::Atof(*Tokens[2]), FCString::Atof(*Tokens[3]));
		return true;
	}

	bool ParseColor(const TArray<FString>& Tokens, FLinearColor& OutValue)
	{
		if (Tokens.Num() < 4) return false;
		OutValue = FLinearColor(
			FCString::Atof(*Tokens[1]), FCString::Atof(*Tokens[2]), FCString::Atof(*Tokens[3]),
			Tokens.Num() > 4 ? FCString::Atof(*Tokens[4]) : 1.0f);
		return true;
	}
}

bool FMT2MapEnvironmentReader::Load(
	const FString& Path, FMT2MapEnvironmentData& OutData, FString& OutError)
{
	FString Text;
	if (Path.IsEmpty() || !FFileHelper::LoadFileToString(Text, *Path))
	{
		OutError = FString::Printf(TEXT("Could not read map environment file: %s"), *Path);
		return false;
	}

	OutData = FMT2MapEnvironmentData();
	OutData.SourcePath = Path;
	TArray<FString> GroupStack;
	FString PendingGroup;
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, false);
	for (FString Line : Lines)
	{
		Line.TrimStartAndEndInline();
		if (Line.IsEmpty() || Line.StartsWith(TEXT("//"))) continue;
		if (Line.StartsWith(TEXT("Group "), ESearchCase::IgnoreCase))
		{
			PendingGroup = Line.Mid(6).TrimStartAndEnd().ToLower();
			continue;
		}
		if (Line == TEXT("{"))
		{
			if (!PendingGroup.IsEmpty()) GroupStack.Add(PendingGroup);
			PendingGroup.Reset();
			continue;
		}
		if (Line == TEXT("}"))
		{
			if (!GroupStack.IsEmpty()) GroupStack.Pop();
			continue;
		}

		TArray<FString> Tokens;
		Line.ParseIntoArrayWS(Tokens);
		if (Tokens.IsEmpty()) continue;
		const FString Key = Tokens[0].ToLower();
		const FString Group = GroupStack.IsEmpty() ? FString() : GroupStack.Last();
		const FString Parent = GroupStack.Num() > 1 ? GroupStack[GroupStack.Num() - 2] : FString();

		if (Group == TEXT("directionallight") && Key == TEXT("direction"))
		{
			ParseVector(Tokens, OutData.Direction);
		}
		else if (Parent == TEXT("directionallight") && Group == TEXT("background"))
		{
			if (Key == TEXT("enable")) ParseBool(Tokens, OutData.bDirectionalLightEnabled);
			else if (Key == TEXT("diffuse")) ParseColor(Tokens, OutData.DirectionalDiffuse);
			else if (Key == TEXT("ambient")) ParseColor(Tokens, OutData.DirectionalAmbient);
		}
		else if (Group == TEXT("material"))
		{
			if (Key == TEXT("ambient")) ParseColor(Tokens, OutData.MaterialAmbient);
			else if (Key == TEXT("emissive")) ParseColor(Tokens, OutData.MaterialEmissive);
		}
		else if (Group == TEXT("fog"))
		{
			if (Key == TEXT("enable")) ParseBool(Tokens, OutData.bFogEnabled);
			else if (Key == TEXT("isdensity")) ParseBool(Tokens, OutData.bDensityFog);
			else if (Key == TEXT("neardistance")) ParseFloat(Tokens, OutData.FogNearDistance);
			else if (Key == TEXT("fardistance")) ParseFloat(Tokens, OutData.FogFarDistance);
			else if (Key == TEXT("color")) ParseColor(Tokens, OutData.FogColor);
		}
	}

	if (OutData.FogFarDistance < OutData.FogNearDistance)
	{
		Swap(OutData.FogNearDistance, OutData.FogFarDistance);
	}
	return true;
}
