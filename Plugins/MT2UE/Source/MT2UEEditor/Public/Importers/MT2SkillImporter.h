/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"

struct FMT2SkillImportResult
{
	int32 DefinitionsCreated = 0;
	int32 DefinitionsRefreshed = 0;
	int32 SkillSetsCreated = 0;
	int32 SkillSetsRefreshed = 0;
	int32 AnimationsResolved = 0;
	TArray<FString> Warnings;
	TArray<FString> Errors;

	FString BuildSummary() const;
};

// Generates UMT2SkillDefinition assets from the old client's skilltable.txt + skilldesc.txt and
// the 8 race/group UMT2SkillSet assets (playersettingmodule's SKILL_INDEX_DICT). Re-running
// refreshes existing assets in place.
class FMT2SkillImporter
{
public:
	static bool Import(
		const FString& SourceRoot, const FString& DestinationRoot, FMT2SkillImportResult& OutResult,
		bool bLegacyMetadataOnly = false);
};
