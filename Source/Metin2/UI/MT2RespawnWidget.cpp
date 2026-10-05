/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2RespawnWidget.h"

#include "Characters/MT2PlayerCharacter.h"
#include "Components/Button.h"

void UMT2RespawnWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);

	if (RespawnHereButton)
	{
		RespawnHereButton->OnClicked.AddUniqueDynamic(this, &UMT2RespawnWidget::HandleRespawnHereClicked);
	}
	if (RespawnTownButton)
	{
		RespawnTownButton->OnClicked.AddUniqueDynamic(this, &UMT2RespawnWidget::HandleRespawnTownClicked);
	}
}

void UMT2RespawnWidget::HandleRespawnHereClicked()
{
	if (AMT2PlayerCharacter* Player = Cast<AMT2PlayerCharacter>(GetOwningPlayerPawn()))
	{
		Player->RequestRespawn(false);
	}
	RemoveFromParent();
}

void UMT2RespawnWidget::HandleRespawnTownClicked()
{
	if (AMT2PlayerCharacter* Player = Cast<AMT2PlayerCharacter>(GetOwningPlayerPawn()))
	{
		Player->RequestRespawn(true);
	}
	RemoveFromParent();
}
