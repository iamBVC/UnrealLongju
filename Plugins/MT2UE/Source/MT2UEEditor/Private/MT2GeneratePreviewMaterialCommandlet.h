/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"
#include "MT2GeneratePreviewMaterialCommandlet.generated.h"

UCLASS()
class UMT2GeneratePreviewMaterialCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMT2GeneratePreviewMaterialCommandlet();
	virtual int32 Main(const FString& Params) override;
};
