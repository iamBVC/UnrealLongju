/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "API/MT2ImporterInterface.h"

class FMT2SkeletonImporter : public FMT2ImporterBase
{
public:
	FMT2SkeletonImporter();

	virtual bool Import(const FMT2ImportRequest& Request, FMT2ImportResult& OutResult) override;
};
