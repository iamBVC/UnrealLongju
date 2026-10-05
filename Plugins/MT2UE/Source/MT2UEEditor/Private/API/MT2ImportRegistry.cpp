/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "API/MT2ImportRegistry.h"

#include "Importers/MT2AnimationImporter.h"
#include "Importers/MT2ArchiveImporter.h"
#include "Importers/MT2AudioImporter.h"
#include "Importers/MT2CharacterImporter.h"
#include "Importers/MT2EffectImporter.h"
#include "Importers/MT2LandscapeMaterialImporter.h"
#include "Importers/MT2MapObjectImporter.h"
#include "Importers/MT2MapTerrainImporter.h"
#include "Importers/MT2ScriptImporter.h"
#include "Importers/MT2SkeletalMeshImporter.h"
#include "Importers/MT2SkeletonImporter.h"
#include "Importers/MT2StaticObjectImporter.h"
#include "Importers/MT2StaticMeshImporter.h"
#include "Importers/MT2TextureImporter.h"

void FMT2ImportRegistry::RegisterImporter(TSharedRef<IMT2Importer> Importer)
{
	UnregisterImporter(Importer->GetImporterName());
	Importers.Add(Importer);
}

void FMT2ImportRegistry::UnregisterImporter(FName ImporterName)
{
	Importers.RemoveAll([ImporterName](const TSharedRef<IMT2Importer>& Importer)
	{
		return Importer->GetImporterName() == ImporterName;
	});
}

TSharedPtr<IMT2Importer> FMT2ImportRegistry::FindImporter(FName ImporterName) const
{
	for (const TSharedRef<IMT2Importer>& Importer : Importers)
	{
		if (Importer->GetImporterName() == ImporterName)
		{
			return Importer;
		}
	}
	return nullptr;
}

TSharedPtr<IMT2Importer> FMT2ImportRegistry::FindImporterForDomain(EMT2ImportDomain Domain) const
{
	for (const TSharedRef<IMT2Importer>& Importer : Importers)
	{
		if (Importer->GetDomain() == Domain)
		{
			return Importer;
		}
	}
	return nullptr;
}

TArray<TSharedRef<IMT2Importer>> FMT2ImportRegistry::GetImporters() const
{
	return Importers;
}

TSharedRef<FMT2ImportRegistry> FMT2DefaultImportRegistry::Create()
{
	TSharedRef<FMT2ImportRegistry> Registry = MakeShared<FMT2ImportRegistry>();
	Registry->RegisterImporter(MakeShared<FMT2TextureImporter>());
	Registry->RegisterImporter(MakeShared<FMT2StaticMeshImporter>());
	Registry->RegisterImporter(MakeShared<FMT2SkeletalMeshImporter>());
	Registry->RegisterImporter(MakeShared<FMT2StaticObjectImporter>());
	Registry->RegisterImporter(MakeShared<FMT2MapTerrainImporter>());
	Registry->RegisterImporter(MakeShared<FMT2MapObjectImporter>());
	Registry->RegisterImporter(MakeShared<FMT2LandscapeMaterialImporter>());
	Registry->RegisterImporter(MakeShared<FMT2CharacterImporter>());
	Registry->RegisterImporter(MakeShared<FMT2SkeletonImporter>());
	Registry->RegisterImporter(MakeShared<FMT2AnimationImporter>());
	Registry->RegisterImporter(MakeShared<FMT2EffectImporter>());
	Registry->RegisterImporter(MakeShared<FMT2AudioImporter>());
	Registry->RegisterImporter(MakeShared<FMT2ScriptImporter>());
	Registry->RegisterImporter(MakeShared<FMT2ArchiveImporter>());
	return Registry;
}
