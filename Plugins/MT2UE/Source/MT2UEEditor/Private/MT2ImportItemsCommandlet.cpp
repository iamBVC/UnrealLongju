/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "MT2ImportItemsCommandlet.h"
#include "Config/MT2PathSettings.h"

#include "Importers/MT2ItemImporter.h"
#include "Misc/Parse.h"

UMT2ImportItemsCommandlet::UMT2ImportItemsCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMT2ImportItemsCommandlet::Main(const FString& Params)
{
	// English client data is the canonical import source. The reader decodes its MIPX v1 container.
	FString SourceRoot = UMT2PathSettings::Path(TEXT("LegacyDumpRoot"));
	FString DestinationRoot = UMT2PathSettings::Path(TEXT("ImportDestinationRoot"));
	FParse::Value(*Params, TEXT("Source="), SourceRoot);
	FParse::Value(*Params, TEXT("Destination="), DestinationRoot);

	TArray<FMT2ItemImportRecord> Records;
	TArray<FString> Warnings;
	FString Error;
	if (!FMT2ItemImporter::Discover(SourceRoot, DestinationRoot, Records, Warnings, Error))
	{
		UE_LOG(LogTemp, Error, TEXT("Item discovery failed: %s"), *Error);
		return 1;
	}
	for (const FString& Warning : Warnings)
	{
		UE_LOG(LogTemp, Display, TEXT("%s"), *Warning);
	}
	UE_LOG(LogTemp, Display, TEXT("Discovered %d importable items."), Records.Num());
	for (const FMT2ItemImportRecord& Record : Records)
	{
		if (Record.Definition.Vnum == 10)
		{
			const FMT2ItemDefinition& D = Record.Definition;
			UE_LOG(LogTemp, Display,
				TEXT("[MT2ItemDiag] Vnum=%d Name=%s Type=%d SubType=%d Size=%d WearFlags=%d BuyPrice=%lld SellPrice=%lld Values=[%d,%d,%d,%d,%d,%d] Icon=%s Mesh=%s"),
				D.Vnum, *D.InternalName, D.ItemType, D.SubType, D.Size, D.WearFlags, D.BuyPrice, D.SellPrice,
				D.Values.IsValidIndex(0) ? D.Values[0] : -1, D.Values.IsValidIndex(1) ? D.Values[1] : -1,
				D.Values.IsValidIndex(2) ? D.Values[2] : -1, D.Values.IsValidIndex(3) ? D.Values[3] : -1,
				D.Values.IsValidIndex(4) ? D.Values[4] : -1, D.Values.IsValidIndex(5) ? D.Values[5] : -1,
				*D.IconObjectPath, *D.WorldMeshObjectPath);
			break;
		}
	}

	if (FParse::Param(*Params, TEXT("DiscoverOnly")))
	{
		return 0;
	}

	if (FParse::Param(*Params, TEXT("LootCratesOnly")))
	{
		Records.RemoveAll([](const FMT2ItemImportRecord& Record)
		{
			return Record.LootCrateRewards.IsEmpty();
		});
		UE_LOG(LogTemp, Display, TEXT("Filtered import to %d configured loot crates."), Records.Num());
	}
	int32 OnlyVnum = 0;
	if (FParse::Param(*Params, TEXT("FishingRodsOnly")))
	{
		Records.RemoveAll([](const FMT2ItemImportRecord& Record) { return Record.Definition.ItemType != 13; });
		UE_LOG(LogTemp, Display, TEXT("Filtered import to %d fishing rods."), Records.Num());
	}
	if (FParse::Value(*Params, TEXT("OnlyVnum="), OnlyVnum) && OnlyVnum > 0)
	{
		Records.RemoveAll([OnlyVnum](const FMT2ItemImportRecord& Record)
		{
			return Record.Definition.Vnum != OnlyVnum;
		});
		UE_LOG(LogTemp, Display, TEXT("Filtered import to item %d (%d record)."),
			OnlyVnum, Records.Num());
	}

	int32 MaxRecords = 0;
	FParse::Value(*Params, TEXT("Max="), MaxRecords);
	if (MaxRecords > 0 && Records.Num() > MaxRecords)
	{
		Records.SetNum(MaxRecords);
	}

	FMT2ItemImportResult Result;
	FMT2ItemImporter::Import(Records, DestinationRoot, []() { return false; }, Result);
	UE_LOG(LogTemp, Display, TEXT("%s"), *Result.BuildSummary());
	for (const FString& ImportError : Result.Errors)
	{
		UE_LOG(LogTemp, Error, TEXT("%s"), *ImportError);
	}
	return Result.Errors.IsEmpty() ? 0 : 1;
}
