/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Components/WidgetComponent.h"
#include "CoreMinimal.h"
#include "MT2NameplateComponent.generated.h"

class UMT2NameplateWidget;
struct FCollisionQueryParams;

UCLASS(ClassGroup = "MT2", meta = (BlueprintSpawnableComponent))
class METIN2_API UMT2NameplateComponent : public UWidgetComponent
{
	GENERATED_BODY()

public:
	UMT2NameplateComponent();

	UFUNCTION(BlueprintCallable, Category = "Nameplate")
	void SetMaxRenderDistance(float NewDistance);

	UFUNCTION(BlueprintPure, Category = "Nameplate")
	float GetMaxRenderDistance() const { return MaxRenderDistance; }
	void RefreshFromSimulationManager();
	void RefreshFromSimulationManager(const FVector& ViewLocation, bool bHasViewLocation);
	void RefreshMobOcclusionFromSimulationManager(
		const FVector& ViewLocation,
		const FCollisionQueryParams& QueryParams);
	void ShowChatMessage(const FString& Message, bool bWorldBroadcast);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	bool EnsureWidgetInitialized();
	void RefreshNameplate();
	UFUNCTION() void HandleItemNameplateClicked();

	UPROPERTY(EditDefaultsOnly, Category = "Nameplate")
	TSoftClassPtr<UMT2NameplateWidget> NameplateWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Nameplate", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", Units = "cm"))
	float MaxRenderDistance = 5000.0f;

	// A screen-space UWidgetComponent only removes its on-screen Slate widget from TickComponent, so
	// the tick must run for one more cycle after we hide it. This defers disabling the tick until the
	// widget has actually been pulled off screen. See RefreshFromSimulationManager.
	bool bPendingTickDisable = false;
	bool bVisibleThroughOcclusion = true;
};
