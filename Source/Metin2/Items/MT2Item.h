/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Items/MT2ItemTemplate.h"
#include "UObject/Object.h"
#include "MT2Item.generated.h"

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class METIN2_API UMT2Item : public UObject
{
	GENERATED_BODY()

public:
	virtual void PostLoad() override;

	UFUNCTION(BlueprintCallable, Category = "Items", meta = (WorldContext = "WorldContextObject"))
	static UMT2Item* CreateItem(UObject* WorldContextObject, int32 Vnum, int32 Count = 1);

	UFUNCTION(BlueprintCallable, Category = "Items", meta = (WorldContext = "WorldContextObject"))
	static UMT2Item* CreateItemFromTemplate(
		UObject* WorldContextObject, TSubclassOf<UMT2ItemTemplate> TemplateClass, int32 Count = 1);

	UFUNCTION(BlueprintPure, Category = "Items")
	const UMT2ItemTemplate* GetTemplate() const { return Template.GetDefaultObject(); }

	UFUNCTION(BlueprintPure, Category = "Items")
	int32 GetVnum() const;

	UFUNCTION(BlueprintPure, Category = "Items")
	FMT2ItemSlot ToItemSlot() const { return FMT2ItemSlot(GetVnum(), InstanceData); }

	UFUNCTION(BlueprintPure, Category = "Items")
	bool IsValidItem() const { return Template != nullptr && InstanceData.Count > 0; }

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Item")
	FGuid InstanceId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	TSubclassOf<UMT2ItemTemplate> Template;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	FMT2ItemInstanceData InstanceData;

private:
	// One-load migration for item objects embedded in lootcrate assets created before
	// FMT2ItemInstanceData. These are not used by active gameplay code.
	UPROPERTY()
	int32 Count_DEPRECATED = 0;

	UPROPERTY()
	TArray<FMT2ItemBonus> Bonuses_DEPRECATED;

	UPROPERTY()
	TArray<int32> Sockets_DEPRECATED;
};
