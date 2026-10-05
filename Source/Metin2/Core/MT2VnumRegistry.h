/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "UObject/SoftObjectPtr.h"
#include "MT2VnumRegistry.generated.h"

class AMT2Mob;
class UMT2ItemTemplate;

UCLASS(BlueprintType)
class METIN2_API UMT2VnumRegistry : public UDataAsset
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "MT2|VNUM")
	const TMap<int32, TSoftClassPtr<AMT2Mob>>& GetMobClasses() const { return MobClasses; }

	UFUNCTION(BlueprintPure, Category = "MT2|VNUM")
	const TMap<int32, TSoftClassPtr<UMT2ItemTemplate>>& GetItemTemplates() const { return ItemTemplates; }

	void SetEntries(
		TMap<int32, TSoftClassPtr<AMT2Mob>>&& NewMobClasses,
		TMap<int32, TSoftClassPtr<UMT2ItemTemplate>>&& NewItemTemplates);

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

private:
	UPROPERTY(VisibleAnywhere, Category = "VNUM|Mobs")
	TMap<int32, TSoftClassPtr<AMT2Mob>> MobClasses;

	UPROPERTY(VisibleAnywhere, Category = "VNUM|Items")
	TMap<int32, TSoftClassPtr<UMT2ItemTemplate>> ItemTemplates;
};
