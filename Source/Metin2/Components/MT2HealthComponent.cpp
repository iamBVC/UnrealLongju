/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Components/MT2HealthComponent.h"

#include "Abilities/MT2CoreAttributeSet.h"

UMT2HealthComponent::UMT2HealthComponent()
{
	ConfigureAttributes(UMT2CoreAttributeSet::GetHealthAttribute(), UMT2CoreAttributeSet::GetMaxHealthAttribute());
}

void UMT2HealthComponent::HandleCurrentValueChanged(float OldValue, float NewValue)
{
	Super::HandleCurrentValueChanged(OldValue, NewValue);

	if (OldValue > 0.0f && NewValue <= 0.0f)
	{
		OnDeath.Broadcast();
	}
	else if (OldValue <= 0.0f && NewValue > 0.0f)
	{
		OnRevived.Broadcast();
	}
}
