/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"
#include "MT2ImportSkillsCommandlet.generated.h"

UCLASS()
class UMT2ImportSkillsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMT2ImportSkillsCommandlet();
	virtual int32 Main(const FString& Params) override;
};
