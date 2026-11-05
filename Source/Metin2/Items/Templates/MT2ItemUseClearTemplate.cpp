#include "MT2ItemUseClearTemplate.h"

#include "Characters/MT2PlayerCharacter.h"
#include "Components/MT2StatusEffectComponent.h"
#include "Items/MT2InventoryComponent.h"

UMT2ItemUseClearTemplate::UMT2ItemUseClearTemplate()
{
	UseSubType = EMT2UseSubType::Clear;
}

EMT2ItemUseExecution UMT2ItemUseClearTemplate::ExecuteUse(
	AMT2PlayerCharacter& Character, UMT2InventoryComponent& Inventory, int32 InventorySlot) const
{
	UMT2StatusEffectComponent* Effects = Character.GetStatusEffectComponent();
	if (!Character.HasAuthority() || Inventory.GetOwner() != &Character || !Effects)
		return EMT2ItemUseExecution::Rejected;

	// Legacy USE_CLEAR consumes even when RemoveBadAffect finds nothing to remove.
	Effects->RemoveBadEffects();
	return EMT2ItemUseExecution::Consume;
}

