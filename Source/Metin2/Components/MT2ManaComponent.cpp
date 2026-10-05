/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Components/MT2ManaComponent.h"

#include "Abilities/MT2CoreAttributeSet.h"

UMT2ManaComponent::UMT2ManaComponent()
{
	ConfigureAttributes(UMT2CoreAttributeSet::GetManaAttribute(), UMT2CoreAttributeSet::GetMaxManaAttribute());
}
