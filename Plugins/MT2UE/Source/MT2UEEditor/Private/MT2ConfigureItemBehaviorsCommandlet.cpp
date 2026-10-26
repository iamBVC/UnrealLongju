#include "MT2ConfigureItemBehaviorsCommandlet.h"

#include "Config/MT2PathSettings.h"
#include "Importers/MT2ItemImporter.h"
#include "Misc/Parse.h"
#include "Engine/Blueprint.h"
#include "Items/MT2ItemTemplate.h"

UMT2ConfigureItemBehaviorsCommandlet::UMT2ConfigureItemBehaviorsCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UMT2ConfigureItemBehaviorsCommandlet::Main(const FString& Params)
{
	FString SourceRoot = UMT2PathSettings::Path(TEXT("LegacyDumpRoot"));
	FString DestinationRoot = TEXT("/Game");
	FParse::Value(*Params, TEXT("SourceRoot="), SourceRoot);
	FParse::Value(*Params, TEXT("DestinationRoot="), DestinationRoot);
	TArray<FMT2ItemImportRecord> Records;
	TArray<FString> Warnings;
	FString Error;
	if (!FMT2ItemImporter::Discover(SourceRoot, DestinationRoot, Records, Warnings, Error))
	{
		UE_LOG(LogTemp, Error, TEXT("Item behavior discovery failed: %s"), *Error);
		return 1;
	}
	for (const auto& Warning : Warnings) UE_LOG(LogTemp, Display, TEXT("%s"), *Warning);
	const bool bBeginnerBonusesOnly = FParse::Param(*Params, TEXT("BeginnerBonusesOnly"));
	const bool bInstantPotionsOnly = FParse::Param(*Params, TEXT("InstantPotionsOnly"));
	const bool bRecoveryPotionsOnly = FParse::Param(*Params, TEXT("RecoveryPotionsOnly"));
	if (static_cast<int32>(bBeginnerBonusesOnly) + static_cast<int32>(bInstantPotionsOnly) + static_cast<int32>(bRecoveryPotionsOnly) > 1)
	{
		UE_LOG(LogTemp, Error, TEXT("Choose only one item behavior filter."));
		return 1;
	}
	Records.RemoveAll([bBeginnerBonusesOnly, bInstantPotionsOnly, bRecoveryPotionsOnly](const FMT2ItemImportRecord& Record)
	{
		if (bRecoveryPotionsOnly)
		{
			const auto Type = Record.Definition.SubType.Use;
			return Record.Definition.ItemType != EMT2ItemType::Use ||
				(Type != EMT2UseSubType::Potion && Type != EMT2UseSubType::PotionNoDelay && Type != EMT2UseSubType::PotionContinue);
		}
		if (bInstantPotionsOnly)
			return Record.Definition.ItemType != EMT2ItemType::Use || Record.Definition.SubType.Use != EMT2UseSubType::PotionNoDelay;
		if (bBeginnerBonusesOnly)
		{
			const int32 Vnum = Record.Definition.Vnum;
			return Vnum != 71151 && Vnum != 71152 && Vnum != 76023 && Vnum != 76024;
		}
		if (Record.Definition.ItemType == EMT2ItemType::Blend || Record.Definition.ItemType == EMT2ItemType::Fish ||
			Record.Definition.ItemType == EMT2ItemType::Campfire) return false;
		if (Record.Definition.ItemType != EMT2ItemType::Use) return true;
		const auto Type = Record.Definition.SubType.Use;
		return Type != EMT2UseSubType::AbilityUp && Type != EMT2UseSubType::PotionContinue && Type != EMT2UseSubType::Clear;
	});
	if (bBeginnerBonusesOnly && Records.Num() != 4)
	{
		UE_LOG(LogTemp, Error, TEXT("Expected all four beginner bonus-item definitions; refusing a partial refresh."));
		return 1;
	}
	if (Records.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("No item behavior definitions matched the source proto."));
		return 1;
	}
	UE_LOG(LogTemp, Display, TEXT("Item behavior definitions: %d"), Records.Num());
	if (FParse::Param(*Params, TEXT("Preview"))) return 0;
	if (bRecoveryPotionsOnly)
	{
		// A behavior migration must not replace authored ground visuals with newly discovered meshes.
		for (FMT2ItemImportRecord& Record : Records)
		{
			const FString Path = FMT2ItemImporter::BuildBlueprintObjectPath(DestinationRoot, Record.Definition);
			const UBlueprint* Existing = LoadObject<UBlueprint>(nullptr, *Path);
			const UMT2ItemTemplate* Defaults = Existing && Existing->GeneratedClass
				? Cast<UMT2ItemTemplate>(Existing->GeneratedClass->GetDefaultObject()) : nullptr;
			if (Defaults) Record.Definition.WorldMeshObjectPath = Defaults->WorldMesh.ToSoftObjectPath().ToString();
		}
	}
	FMT2ItemImportResult Result;
	const bool bSuccess = FMT2ItemImporter::Import(Records, DestinationRoot, [] { return false; }, Result);
	for (const auto& ImportError : Result.Errors) UE_LOG(LogTemp, Error, TEXT("%s"), *ImportError);
	UE_LOG(LogTemp, Display, TEXT("%s"), *Result.BuildSummary());
	return bSuccess ? 0 : 1;
}
