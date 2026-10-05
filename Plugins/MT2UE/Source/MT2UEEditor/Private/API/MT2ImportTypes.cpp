/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "API/MT2ImportTypes.h"

namespace
{
	void AddMessage(TArray<FMT2ImportMessage>& Messages, EMT2ImportSeverity Severity, const FString& Text, const FString& SourcePath)
	{
		FMT2ImportMessage Message;
		Message.Severity = Severity;
		Message.Text = Text;
		Message.SourcePath = SourcePath;
		Messages.Add(MoveTemp(Message));
	}
}

void FMT2ImportResult::AddInfo(const FString& Text, const FString& SourcePath)
{
	AddMessage(Messages, EMT2ImportSeverity::Info, Text, SourcePath);
}

void FMT2ImportResult::AddWarning(const FString& Text, const FString& SourcePath)
{
	AddMessage(Messages, EMT2ImportSeverity::Warning, Text, SourcePath);
}

void FMT2ImportResult::AddError(const FString& Text, const FString& SourcePath)
{
	AddMessage(Messages, EMT2ImportSeverity::Error, Text, SourcePath);
}

bool FMT2ImportResult::HasErrors() const
{
	for (const FMT2ImportMessage& Message : Messages)
	{
		if (Message.Severity == EMT2ImportSeverity::Error)
		{
			return true;
		}
	}
	return false;
}

FString LexToString(EMT2ImportDomain Domain)
{
	switch (Domain)
	{
	case EMT2ImportDomain::Textures:
		return TEXT("Textures");
	case EMT2ImportDomain::StaticMeshes:
		return TEXT("Static Meshes");
	case EMT2ImportDomain::SkeletalMeshes:
		return TEXT("Skeletal Meshes");
	case EMT2ImportDomain::StaticObjects:
		return TEXT("Static Objects");
	case EMT2ImportDomain::MapTerrains:
		return TEXT("Map Terrains");
	case EMT2ImportDomain::MapObjects:
		return TEXT("Map Objects");
	case EMT2ImportDomain::LandscapeMaterials:
		return TEXT("Landscape Materials");
	case EMT2ImportDomain::Characters:
		return TEXT("Characters");
	case EMT2ImportDomain::Skeletons:
		return TEXT("Skeletons");
	case EMT2ImportDomain::Animations:
		return TEXT("Animations");
	case EMT2ImportDomain::Effects:
		return TEXT("Effects");
	case EMT2ImportDomain::Audio:
		return TEXT("Audio");
	case EMT2ImportDomain::Scripts:
		return TEXT("Scripts");
	case EMT2ImportDomain::Archives:
		return TEXT("Archives");
	default:
		return TEXT("Unknown");
	}
}
