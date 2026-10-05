/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Importers/MT2SkeletonImporter.h"

FMT2SkeletonImporter::FMT2SkeletonImporter()
	: FMT2ImporterBase(EMT2ImportDomain::Skeletons, TEXT("SkeletonImporter"))
{
}

bool FMT2SkeletonImporter::Import(const FMT2ImportRequest& Request, FMT2ImportResult& OutResult)
{
	if (!CanImport(Request, OutResult))
	{
		return false;
	}

	OutResult.ItemsDiscovered = Request.Selection.AssetRecords.Num();
	OutResult.AddWarning(TEXT("Skeleton import service skeleton only. This will later convert Granny skeletons into UE skeleton assets."));
	OutResult.bSucceeded = !OutResult.HasErrors();
	return OutResult.bSucceeded;
}
