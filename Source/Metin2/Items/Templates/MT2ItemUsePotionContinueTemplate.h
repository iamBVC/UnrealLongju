#pragma once

#include "Items/Templates/MT2ItemRecoveryTemplate.h"
#include "MT2ItemUsePotionContinueTemplate.generated.h"

/**
 * Legacy item-proto variant USE_POTION_CONTINUE (ordinal 16).
 * TODO: Implement and validate this variant's dedicated legacy behavior. Keep shared family
 * recovery quantities in UMT2ItemRecoveryTemplate; this class is the narrow subtype extension point.
 */
UCLASS(Blueprintable)
class METIN2_API UMT2ItemUsePotionContinueTemplate : public UMT2ItemRecoveryTemplate
{
	GENERATED_BODY()
};

