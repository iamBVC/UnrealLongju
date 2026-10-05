/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MT2VnumRegistrySubsystem.generated.h"

class AMT2Mob;
class UMT2ItemTemplate;
class UMT2VnumRegistry;

UCLASS()
class METIN2_API UMT2VnumRegistrySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintCallable, Category = "MT2|VNUM")
	bool ReloadRegistryAsset();

	UFUNCTION(BlueprintPure, Category = "MT2|VNUM")
	bool HasMob(int32 Vnum) const;

	UFUNCTION(BlueprintPure, Category = "MT2|VNUM")
	bool HasItem(int32 Vnum) const;

	UFUNCTION(BlueprintCallable, Category = "MT2|VNUM")
	TSubclassOf<AMT2Mob> ResolveMobClass(int32 Vnum);

	UFUNCTION(BlueprintCallable, Category = "MT2|VNUM")
	TSubclassOf<UMT2ItemTemplate> ResolveItemTemplateClass(int32 Vnum);

	UFUNCTION(BlueprintPure, Category = "MT2|VNUM")
	FSoftClassPath GetMobClassPath(int32 Vnum) const;

	UFUNCTION(BlueprintPure, Category = "MT2|VNUM")
	FSoftClassPath GetItemTemplatePath(int32 Vnum) const;

	UFUNCTION(BlueprintPure, Category = "MT2|VNUM")
	UMT2VnumRegistry* GetRegistry() const { return Registry; }

private:
	friend class FMT2QuestItemMetadataTest;
	const TSoftClassPtr<UMT2ItemTemplate>* FindItemTemplate(int32 Vnum) const;

	UPROPERTY()
	TObjectPtr<UMT2VnumRegistry> Registry;

	TArray<int32> SortedItemVnums;
	mutable TMap<int32, int32> ResolvedItemAliases;
};
