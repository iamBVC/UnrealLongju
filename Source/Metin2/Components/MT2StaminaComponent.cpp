/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Components/MT2StaminaComponent.h"

#include "Abilities/MT2CoreAttributeSet.h"

UMT2StaminaComponent::UMT2StaminaComponent()
{
	ConfigureAttributes(UMT2CoreAttributeSet::GetStaminaAttribute(), UMT2CoreAttributeSet::GetMaxStaminaAttribute());
}
