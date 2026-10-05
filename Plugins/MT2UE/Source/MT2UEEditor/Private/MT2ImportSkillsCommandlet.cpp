/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "MT2ImportSkillsCommandlet.h"

#include "Importers/MT2SkillImporter.h"
#include "Misc/Parse.h"

UMT2ImportSkillsCommandlet::UMT2ImportSkillsCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMT2ImportSkillsCommandlet::Main(const FString& Params)
{
	FString SourceRoot = TEXT("D:/Giochi/Metin2/Development/Dumps/my_dump");
	FString DestinationRoot = TEXT("/Game");
	FParse::Value(*Params, TEXT("Source="), SourceRoot);
	FParse::Value(*Params, TEXT("Destination="), DestinationRoot);

	FMT2SkillImportResult Result;
	const bool bSucceeded = FMT2SkillImporter::Import(SourceRoot, DestinationRoot, Result,
		FParse::Param(*Params, TEXT("LegacyMetadataOnly")));
	for (const FString& Warning : Result.Warnings)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s"), *Warning);
	}
	for (const FString& ImportError : Result.Errors)
	{
		UE_LOG(LogTemp, Error, TEXT("%s"), *ImportError);
	}
	UE_LOG(LogTemp, Display, TEXT("%s"), *Result.BuildSummary());
	return bSucceeded ? 0 : 1;
}
