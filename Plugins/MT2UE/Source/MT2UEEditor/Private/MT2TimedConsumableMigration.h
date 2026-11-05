#pragma once

#include "CoreMinimal.h"

class UBlueprint;

/** Preserves authored CDO data while upgrading a timed item's native parent. Does not save. */
bool MigrateMT2TimedConsumable(UBlueprint& Blueprint, UClass* DesiredParent, FString& OutError);
