/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Commandlets/Commandlet.h"
#include "MT2RepairMapActorsCommandlet.generated.h"

UCLASS()
class UMT2RepairMapActorsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMT2RepairMapActorsCommandlet();
	virtual int32 Main(const FString& Params) override;
};
