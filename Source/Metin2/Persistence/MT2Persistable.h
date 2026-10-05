/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "MT2Persistable.generated.h"

UINTERFACE(BlueprintType)
class METIN2_API UMT2Persistable : public UInterface
{
	GENERATED_BODY()
};

class METIN2_API IMT2Persistable
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Persistence")
	FString CapturePersistentStateJson() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Persistence")
	bool ApplyPersistentStateJson(const FString& PayloadJson);
};
