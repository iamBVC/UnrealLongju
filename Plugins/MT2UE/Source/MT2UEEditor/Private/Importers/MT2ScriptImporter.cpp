/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Importers/MT2ScriptImporter.h"

FMT2ScriptImporter::FMT2ScriptImporter()
	: FMT2ImporterBase(EMT2ImportDomain::Scripts, TEXT("ScriptImporter"))
{
}

bool FMT2ScriptImporter::Import(const FMT2ImportRequest& Request, FMT2ImportResult& OutResult)
{
	if (!CanImport(Request, OutResult))
	{
		return false;
	}

	OutResult.ItemsDiscovered = Request.Selection.AssetRecords.Num();
	OutResult.AddWarning(TEXT("Script import service skeleton only. This will later expose batch import recipes and project automation hooks."));
	OutResult.bSucceeded = !OutResult.HasErrors();
	return OutResult.bSucceeded;
}
