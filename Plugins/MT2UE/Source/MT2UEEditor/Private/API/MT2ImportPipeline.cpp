/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "API/MT2ImportPipeline.h"

FMT2ImportPipeline::FMT2ImportPipeline()
	: Registry(FMT2DefaultImportRegistry::Create())
{
}

FMT2ImportPipeline::FMT2ImportPipeline(TSharedRef<FMT2ImportRegistry> InRegistry)
	: Registry(InRegistry)
{
}

bool FMT2ImportPipeline::ScanSource(const FMT2ImportContext& Context, FMT2AssetScanResult& OutScanResult, FMT2ImportResult& OutResult) const
{
	FString Error;
	FMT2AssetScanner Scanner;
	if (!Scanner.Scan(Context.SourceRoot, OutScanResult, Error))
	{
		OutResult.AddError(Error, Context.SourceRoot);
		return false;
	}

	OutResult.ItemsDiscovered = OutScanResult.Records.Num();
	OutResult.bSucceeded = true;
	return true;
}

bool FMT2ImportPipeline::Discover(const FMT2ImportContext& Context, const FMT2AssetScanResult& ScanResult, EMT2ImportDomain Domain, FMT2ImportDiscovery& OutDiscovery, FMT2ImportResult& OutResult) const
{
	TSharedPtr<IMT2Importer> Importer = Registry->FindImporterForDomain(Domain);
	if (!Importer.IsValid())
	{
		OutResult.AddError(FString::Printf(TEXT("No importer registered for %s."), *LexToString(Domain)));
		return false;
	}

	const bool bDiscovered = Importer->Discover(Context, ScanResult, OutDiscovery);
	OutResult.ItemsDiscovered = OutDiscovery.ItemsDiscovered;
	OutResult.Messages.Append(OutDiscovery.Messages);
	OutResult.bSucceeded = bDiscovered && !OutResult.HasErrors();
	return OutResult.bSucceeded;
}

bool FMT2ImportPipeline::Import(const FMT2ImportRequest& Request, FMT2ImportResult& OutResult) const
{
	TSharedPtr<IMT2Importer> Importer = Registry->FindImporterForDomain(Request.Selection.Domain);
	if (!Importer.IsValid())
	{
		OutResult.AddError(FString::Printf(TEXT("No importer registered for %s."), *LexToString(Request.Selection.Domain)));
		return false;
	}

	return Importer->Import(Request, OutResult);
}

TSharedRef<FMT2ImportRegistry> FMT2ImportPipeline::GetRegistry() const
{
	return Registry;
}
