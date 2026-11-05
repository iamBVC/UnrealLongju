#include "MT2ItemUseDewTemplate.h"

#include "Components/MT2StatusEffectComponent.h"

bool UMT2ItemUseDewTemplate::BuildEffect(FMT2StatusEffect& Effect) const
{
	if (!Super::BuildEffect(Effect) && BonusType != EMT2ItemBonusType::MagicResistance) return false;
	Effect.Type = FMT2AffectIdentifier::FromBuiltin(EMT2BuiltinAffect::ExperienceBonusEuropeFree);
	Effect.bRemoveOnDeath = false;
	return true;
}
