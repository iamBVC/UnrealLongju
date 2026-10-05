/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Importers/MT2StaticObjectImporter.h"

FMT2StaticObjectImporter::FMT2StaticObjectImporter()
	: FMT2ImporterBase(EMT2ImportDomain::StaticObjects, TEXT("StaticObjectImporter"))
{
}

bool FMT2StaticObjectImporter::Discover(const FMT2ImportContext& Context, const FMT2AssetScanResult& ScanResult, FMT2ImportDiscovery& OutDiscovery) const
{
	OutDiscovery.Domain = GetDomain();
	OutDiscovery.AssetRecords = ScanResult.GetRecordsByKind(EMT2AssetKind::Property);
	OutDiscovery.ItemsDiscovered = OutDiscovery.AssetRecords.Num();
	for (const FMT2AssetRecord& Record : OutDiscovery.AssetRecords)
	{
		OutDiscovery.EntryNames.Add(Record.VirtualPath.IsEmpty() ? Record.ContentPath : Record.VirtualPath);
	}
	return true;
}

bool FMT2StaticObjectImporter::Import(const FMT2ImportRequest& Request, FMT2ImportResult& OutResult)
{
	if (!CanImport(Request, OutResult))
	{
		return false;
	}

	if (Request.Context.ScanResult)
	{
		FMT2ResolvedWorldResult ResolvedWorld;
		FString Error;
		FMT2PropertyResolver Resolver;
		if (!Resolver.Resolve(*Request.Context.ScanResult, ResolvedWorld, Error))
		{
			OutResult.AddError(Error);
			return false;
		}

		OutResult.ItemsDiscovered = ResolvedWorld.MapObjects.Num();
		OutResult.ItemsImported = ResolvedWorld.StaticGrannyObjects;
		OutResult.AddInfo(FString::Printf(TEXT("Resolved %d map object placement(s), %d property-backed object(s), and %d static Granny object reference(s)."),
			ResolvedWorld.MapObjects.Num(),
			ResolvedWorld.ObjectsWithProperty,
			ResolvedWorld.StaticGrannyObjects));
		OutResult.AddInfo(FString::Printf(TEXT("Read %d property file(s), %d AreaData file(s), skipped %d invalid property file(s), and found %d duplicate CRC(s)."),
			ResolvedWorld.PropertyFilesRead,
			ResolvedWorld.AreaDataFilesRead,
			ResolvedWorld.InvalidPropertyFiles,
			ResolvedWorld.DuplicatePropertyCrcs));
	}
	else
	{
		OutResult.ItemsDiscovered = Request.Selection.AssetRecords.Num();
		OutResult.AddWarning(TEXT("Static object import needs Context.ScanResult to resolve .prb/.prt/.prd properties and AreaData placements."));
	}

	OutResult.bSucceeded = !OutResult.HasErrors();
	return OutResult.bSucceeded;
}
