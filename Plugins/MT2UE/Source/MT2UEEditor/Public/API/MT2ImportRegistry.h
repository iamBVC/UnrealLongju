/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "API/MT2ImporterInterface.h"

class FMT2ImportRegistry
{
public:
	void RegisterImporter(TSharedRef<IMT2Importer> Importer);
	void UnregisterImporter(FName ImporterName);
	TSharedPtr<IMT2Importer> FindImporter(FName ImporterName) const;
	TSharedPtr<IMT2Importer> FindImporterForDomain(EMT2ImportDomain Domain) const;
	TArray<TSharedRef<IMT2Importer>> GetImporters() const;

private:
	TArray<TSharedRef<IMT2Importer>> Importers;
};

class FMT2DefaultImportRegistry
{
public:
	static TSharedRef<FMT2ImportRegistry> Create();
};
