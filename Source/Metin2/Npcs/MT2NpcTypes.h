/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "MT2NpcTypes.generated.h"

// mob_proto bOnClickType (EOnClickEvents): what clicking this NPC does.
UENUM(BlueprintType)
enum class EMT2NpcOnClickType : uint8
{
	None,
	Shop,
	Talk
};

// One shop grid cell (old shop_item row). PriceOverride 0 = the item template's BuyPrice.
USTRUCT(BlueprintType)
struct METIN2_API FMT2ShopEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop", meta = (ClampMin = "0"))
	int32 ItemVnum = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop", meta = (ClampMin = "1"))
	int32 Count = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop", meta = (ClampMin = "0"))
	int64 PriceOverride = 0;
};
