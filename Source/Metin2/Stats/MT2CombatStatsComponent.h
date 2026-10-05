/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Stats/MT2StatTypes.h"
#include "MT2CombatStatsComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FMT2CombatStatsChangedSignature, FMT2CombatStats, OldStats, FMT2CombatStats, NewStats);

UCLASS(BlueprintType, ClassGroup = "MT2", meta = (BlueprintSpawnableComponent))
class METIN2_API UMT2CombatStatsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMT2CombatStatsComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Stats|Combat")
	const FMT2CombatStats& GetBaseStats() const { return BaseStats; }

	UFUNCTION(BlueprintPure, Category = "Stats|Combat")
	const FMT2CombatStatBonuses& GetBonuses() const { return Bonuses; }

	UFUNCTION(BlueprintPure, Category = "Stats|Combat")
	FMT2CombatStats GetCalculatedStats() const;

	UFUNCTION(BlueprintPure, Category = "Stats|Combat")
	float GetAverageDamage() const;

	UFUNCTION(BlueprintPure, Category = "Stats|Combat")
	float GetAttackInterval() const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Stats|Combat")
	bool SetBaseStats(const FMT2CombatStats& NewStats);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Stats|Combat")
	bool SetBonuses(const FMT2CombatStatBonuses& NewBonuses);

	void ConfigureBaseStats(const FMT2CombatStats& NewStats);

	UPROPERTY(BlueprintAssignable, Category = "Stats|Combat")
	FMT2CombatStatsChangedSignature OnCombatStatsChanged;

private:
	static FMT2CombatStats ClampStats(const FMT2CombatStats& Stats);
	void BroadcastChange(const FMT2CombatStats& OldCalculatedStats);

	UFUNCTION()
	void OnRep_BaseStats(FMT2CombatStats OldStats);

	UFUNCTION()
	void OnRep_Bonuses(FMT2CombatStatBonuses OldBonuses);

	UPROPERTY(EditAnywhere, ReplicatedUsing = OnRep_BaseStats, Category = "Stats|Combat")
	FMT2CombatStats BaseStats;

	UPROPERTY(VisibleInstanceOnly, ReplicatedUsing = OnRep_Bonuses, Category = "Stats|Combat")
	FMT2CombatStatBonuses Bonuses;
};
