/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "GameplayCueManager.h"

#include "MT2GameplayCueManager.generated.h"

UCLASS()
class METIN2_API UMT2GameplayCueManager : public UGameplayCueManager
{
	GENERATED_BODY()

public:
	virtual bool ShouldAsyncLoadObjectLibrariesAtStart() const override
	{
		return false;
	}
};
