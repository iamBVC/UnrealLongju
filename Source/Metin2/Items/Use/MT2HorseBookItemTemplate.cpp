/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Items/Use/MT2HorseBookItemTemplate.h"

#include "Characters/MT2PlayerCharacter.h"
#include "Components/MT2ManaComponent.h"
#include "Mounts/MT2MountComponent.h"
#include "Mounts/MT2MountDefinition.h"
#include "Player/MT2PlayerController.h"
#include "Player/MT2PlayerState.h"
#include "Skills/MT2SkillComponent.h"

UMT2HorseBookItemTemplate::UMT2HorseBookItemTemplate()
	: SuccessPercentBySkillLevel({10, 15, 20, 30, 40, 50, 60, 70, 80, 90, 100})
{
}

EMT2ItemUseExecution UMT2HorseBookItemTemplate::ExecuteUse(
	AMT2PlayerCharacter& Character, UMT2InventoryComponent& Inventory,
	int32 InventorySlot) const
{
	if (!Character.HasAuthority())
	{
		return EMT2ItemUseExecution::Rejected;
	}

	UMT2MountComponent* Mounts = Character.GetMountComponent();
	UMT2MountDefinition* Definition = MountDefinition.LoadSynchronous();
	UMT2ManaComponent* Mana = Character.GetManaComponent();
	AMT2PlayerState* PlayerState = Character.GetPlayerState<AMT2PlayerState>();
	UMT2SkillComponent* Skills = PlayerState ? PlayerState->GetSkillComponent() : nullptr;
	AMT2PlayerController* Controller = Cast<AMT2PlayerController>(Character.GetController());
	if (!Mounts || !Definition || Definition->MountKind != EMT2MountKind::Horse || !Mana || !Skills)
	{
		if (Controller)
		{
			Controller->SendSystemChatMessage(TEXT("This horse licence is not configured correctly."));
		}
		return EMT2ItemUseExecution::Rejected;
	}
	if (Mounts->IsMounted())
	{
		if (Controller)
		{
			Controller->SendSystemChatMessage(TEXT("You are already mounted."));
		}
		return EMT2ItemUseExecution::Rejected;
	}
	if (Mana->GetMana() < ManaCost)
	{
		if (Controller)
		{
			Controller->SendSystemChatMessage(TEXT("You do not have enough MP to call your horse."));
		}
		return EMT2ItemUseExecution::Rejected;
	}

	Mana->SetMana(Mana->GetMana() - ManaCost);
	const int32 SkillLevel = FMath::Max(Skills->GetSkillLevel(SummonSkillVnum), 0);
	const int32 ChanceIndex = FMath::Min(SkillLevel, SuccessPercentBySkillLevel.Num() - 1);
	const int32 SuccessPercent = SuccessPercentBySkillLevel.IsValidIndex(ChanceIndex)
		? FMath::Clamp(SuccessPercentBySkillLevel[ChanceIndex], 0, 100) : 10;
	if (FMath::RandRange(1, 100) > SuccessPercent)
	{
		if (Controller)
		{
			Controller->SendSystemChatMessage(TEXT("Your horse did not answer the call."));
		}
		return EMT2ItemUseExecution::Accepted;
	}

	Mounts->SetCalledHorse(Definition);
	if (!Mounts->Mount(Definition))
	{
		return EMT2ItemUseExecution::Rejected;
	}
	if (Controller)
	{
		Controller->SendSystemChatMessage(TEXT("Your horse answered the call."));
	}
	return EMT2ItemUseExecution::Accepted;
}
