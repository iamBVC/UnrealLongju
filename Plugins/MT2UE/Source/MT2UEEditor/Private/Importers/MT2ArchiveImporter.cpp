/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Importers/MT2ArchiveImporter.h"

FMT2ArchiveImporter::FMT2ArchiveImporter()
	: FMT2ImporterBase(EMT2ImportDomain::Archives, TEXT("ArchiveImporter"))
{
}

bool FMT2ArchiveImporter::Import(const FMT2ImportRequest& Request, FMT2ImportResult& OutResult)
{
	if (!CanImport(Request, OutResult))
	{
		return false;
	}

	OutResult.ItemsDiscovered = Request.Selection.AssetRecords.Num();
	OutResult.AddWarning(TEXT("Archive import service skeleton only. This will later decode .epk/.eix when extracted packs are not available."));
	OutResult.bSucceeded = !OutResult.HasErrors();
	return OutResult.bSucceeded;
}
