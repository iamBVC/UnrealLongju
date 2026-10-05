/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "AIController.h"
#include "CoreMinimal.h"
#include "MT2MobAIController.generated.h"

UCLASS()
class METIN2_API AMT2MobAIController : public AAIController
{
	GENERATED_BODY()

public:
	AMT2MobAIController();

protected:
	virtual void OnPossess(APawn* InPawn) override;
};
