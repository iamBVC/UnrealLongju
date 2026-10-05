/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Items/Use/MT2ItemActionTemplate.h"
#include "MT2SkillTrainingItemTemplates.generated.h"

UCLASS(Blueprintable)
class METIN2_API UMT2GrandMasterTrainingItemTemplate : public UMT2ItemActionTemplate
{
	GENERATED_BODY()

public:
	virtual EMT2ItemUseExecution ExecuteUse(
		AMT2PlayerCharacter& Character, UMT2InventoryComponent& Inventory,
		int32 InventorySlot) const override;
};

UCLASS(Blueprintable)
class METIN2_API UMT2KarmaRecoveryItemTemplate : public UMT2ItemActionTemplate
{
	GENERATED_BODY()

public:
	virtual EMT2ItemUseExecution ExecuteUse(
		AMT2PlayerCharacter& Character, UMT2InventoryComponent& Inventory,
		int32 InventorySlot) const override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Karma", meta = (ClampMin = "0"))
	int32 KarmaRestored = 2000;
};
