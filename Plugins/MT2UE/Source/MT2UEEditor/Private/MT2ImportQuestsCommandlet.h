/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"
#include "MT2ImportQuestsCommandlet.generated.h"

// Headless quest conversion, so the .quest -> Blueprint import can be run and verified without
// opening the editor:
//   UnrealEditor-Cmd.exe <project> -run=MT2ImportQuests [-Source=<quest dir>] [-Destination=/Game]
UCLASS()
class UMT2ImportQuestsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMT2ImportQuestsCommandlet();
	virtual int32 Main(const FString& Params) override;
};
