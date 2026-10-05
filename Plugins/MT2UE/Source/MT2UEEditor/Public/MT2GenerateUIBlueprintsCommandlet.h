/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Commandlets/Commandlet.h"
#include "MT2GenerateUIBlueprintsCommandlet.generated.h"

UCLASS()
class UMT2GenerateUIBlueprintsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMT2GenerateUIBlueprintsCommandlet();
	virtual int32 Main(const FString& Params) override;
};
