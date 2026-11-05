#pragma once

#include "Items/Templates/MT2ItemUseAbilityUpTemplate.h"
#include "MT2ItemUseDewTemplate.generated.h"

/** Legacy dew family: persistent Europe-free identity, one effect per apply; no refresh. */
UCLASS(Blueprintable)
class METIN2_API UMT2ItemUseDewTemplate : public UMT2ItemUseAbilityUpTemplate
{
	GENERATED_BODY()
protected:
	virtual bool BuildEffect(FMT2StatusEffect& Effect) const override;
	virtual bool RefreshesExistingEffect() const override { return false; }
};
