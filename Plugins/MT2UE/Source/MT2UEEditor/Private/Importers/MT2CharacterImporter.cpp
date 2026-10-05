/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Importers/MT2CharacterImporter.h"

FMT2CharacterImporter::FMT2CharacterImporter()
	: FMT2ImporterBase(EMT2ImportDomain::Characters, TEXT("CharacterImporter"))
{
}

bool FMT2CharacterImporter::Discover(const FMT2ImportContext& Context, const FMT2AssetScanResult& ScanResult, FMT2ImportDiscovery& OutDiscovery) const
{
	OutDiscovery.Domain = GetDomain();
	OutDiscovery.AssetRecords = ScanResult.GetRecordsByKind(EMT2AssetKind::ModelScript);
	OutDiscovery.ItemsDiscovered = OutDiscovery.AssetRecords.Num();
	return true;
}

bool FMT2CharacterImporter::Import(const FMT2ImportRequest& Request, FMT2ImportResult& OutResult)
{
	if (!CanImport(Request, OutResult))
	{
		return false;
	}

	OutResult.ItemsDiscovered = Request.Selection.AssetRecords.Num();
	OutResult.AddWarning(TEXT("Character import service skeleton only. This will later parse .msm, resolve skeletons, meshes, textures, and motions."));
	OutResult.bSucceeded = !OutResult.HasErrors();
	return OutResult.bSucceeded;
}
