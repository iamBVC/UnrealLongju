#include "MT2ItemUsePotionNoDelayTemplate.h"

#include "Characters/MT2PlayerCharacter.h"
#include "Components/MT2HealthComponent.h"
#include "Components/MT2ManaComponent.h"
#include "Items/MT2InventoryComponent.h"

namespace
{
	bool RestoreResource(UMT2ResourceComponent* Resource, double Amount)
	{
		if (!Resource || !Resource->IsInitialized() || Resource->GetCurrentValue() >= Resource->GetMaxValue()) return false;
		// Keep legacy integer arithmetic without overflowing intermediate int32 products.
		return Resource->SetCurrentValue(static_cast<float>(FMath::Min(
			static_cast<double>(Resource->GetCurrentValue()) + FMath::TruncToDouble(Amount),
			static_cast<double>(Resource->GetMaxValue()))));
	}
}

UMT2ItemUsePotionNoDelayTemplate::UMT2ItemUsePotionNoDelayTemplate()
{
	UseSubType = EMT2UseSubType::PotionNoDelay;
}

EMT2ItemUseExecution UMT2ItemUsePotionNoDelayTemplate::ExecuteUse(
	AMT2PlayerCharacter& Character, UMT2InventoryComponent& Inventory, int32 InventorySlot) const
{
	UMT2HealthComponent* Health = Character.GetHealthComponent();
	if (!Character.HasAuthority() || Inventory.GetOwner() != &Character || !Health || Health->IsDead())
		return EMT2ItemUseExecution::Rejected;
	const int32 Bonus = Character.GetItemApplyBonus(EMT2ItemBonusType::PotionBonus);
	UMT2ManaComponent* Mana = Character.GetManaComponent();
	bool bUsed = false;
	// Preserve the legacy operation order and keep flat and percent quantities independent.
	if (HealthRecovery != 0) bUsed |= RestoreResource(Health, static_cast<double>(HealthRecovery) * (100.0 + Bonus) / 100.0);
	if (ManaRecovery != 0) bUsed |= RestoreResource(Mana, static_cast<double>(ManaRecovery) * (100.0 + Bonus) / 100.0);
	if (HealthRecoveryPercent != 0) bUsed |= RestoreResource(Health, static_cast<double>(HealthRecoveryPercent) * Health->GetMaxHealth() / 100.0);
	if (ManaRecoveryPercent != 0 && Mana) bUsed |= RestoreResource(Mana, static_cast<double>(ManaRecoveryPercent) * Mana->GetMaxMana() / 100.0);
	return bUsed ? EMT2ItemUseExecution::Consume : EMT2ItemUseExecution::Rejected;
}

