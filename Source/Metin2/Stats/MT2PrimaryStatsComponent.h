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
#include "MT2PrimaryStatsComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FMT2PrimaryStatsChangedSignature, FMT2PrimaryStats, OldStats, FMT2PrimaryStats, NewStats);

UCLASS(BlueprintType, ClassGroup = "MT2", meta = (BlueprintSpawnableComponent))
class METIN2_API UMT2PrimaryStatsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMT2PrimaryStatsComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Stats|Primary")
	const FMT2PrimaryStats& GetBaseStats() const { return BaseStats; }

	UFUNCTION(BlueprintPure, Category = "Stats|Primary")
	const FMT2PrimaryStats& GetBonusStats() const { return BonusStats; }

	UFUNCTION(BlueprintPure, Category = "Stats|Primary")
	FMT2PrimaryStats GetCalculatedStats() const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Stats|Primary")
	bool SetBaseStats(const FMT2PrimaryStats& NewStats);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Stats|Primary")
	bool SetBonusStats(const FMT2PrimaryStats& NewBonuses);

	void ConfigureBaseStats(const FMT2PrimaryStats& NewStats);

	UPROPERTY(BlueprintAssignable, Category = "Stats|Primary")
	FMT2PrimaryStatsChangedSignature OnPrimaryStatsChanged;

private:
	static FMT2PrimaryStats ClampStats(const FMT2PrimaryStats& Stats);
	void BroadcastChange(const FMT2PrimaryStats& OldCalculatedStats);

	UFUNCTION()
	void OnRep_BaseStats(FMT2PrimaryStats OldStats);

	UFUNCTION()
	void OnRep_BonusStats(FMT2PrimaryStats OldStats);

	UPROPERTY(EditAnywhere, ReplicatedUsing = OnRep_BaseStats, Category = "Stats|Primary")
	FMT2PrimaryStats BaseStats;

	UPROPERTY(VisibleInstanceOnly, ReplicatedUsing = OnRep_BonusStats, Category = "Stats|Primary")
	FMT2PrimaryStats BonusStats;
};
