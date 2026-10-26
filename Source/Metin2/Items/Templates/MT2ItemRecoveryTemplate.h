#pragma once

#include "Items/Templates/MT2ItemUseTemplate.h"
#include "MT2ItemRecoveryTemplate.generated.h"

/** Shared authored quantities for queued, instant and continuous recovery potions only.
 * Concrete classes own use policy; resource/effect components own mutable recovery state.
 */
UCLASS(Abstract, Blueprintable)
class METIN2_API UMT2ItemRecoveryTemplate : public UMT2ItemUseTemplate
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Use|Recovery")
	int32 HealthRecovery = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Use|Recovery")
	int32 ManaRecovery = 0;
};
