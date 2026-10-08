/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"

struct FMT2CharacterAnimationGenerationResult
{
	int32 AnimationsImported = 0;
	int32 AnimationBindings = 0;
	int32 BlueprintsCreated = 0;
	int32 BlueprintsUpdated = 0;
	TArray<FString> Errors;

	FString BuildSummary() const;
};

class FMT2CharacterAnimationGenerator
{
public:
	static bool BindFishingAnimations(FMT2CharacterAnimationGenerationResult& OutResult);
	static bool Generate(
		const FString& SourceRoot,
		bool bImportMissingAnimations,
		FMT2CharacterAnimationGenerationResult& OutResult);
};
