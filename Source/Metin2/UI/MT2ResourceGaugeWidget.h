/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "UI/MT2UserWidget.h"
#include "CoreMinimal.h"
#include "MT2ResourceGaugeWidget.generated.h"

class UImage;
class UProgressBar;
class UTexture2D;

UCLASS()
class METIN2_API UMT2ResourceGaugeWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	void SetResources(float Health, float Mana, float Stamina);
	// HealthRegenPool/ManaRegenPool: amount still to be recovered by potions, drawn as a translucent
	// "target" fill behind the HP/MP bars (up to current + pool), like the old client.
	void SetResourceValues(float Health, float MaxHealth, float Mana, float MaxMana, float Stamina, float MaxStamina,
		float HealthRegenPool = 0.0f, float ManaRegenPool = 0.0f);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void LoadAnimationFrames();
	void ApplyAnimationFrame();
	// Builds a half-opacity clone of Real, placed in the same slot but one layer behind, that shows the
	// potion regen target as a ghost fill under the live bar.
	UProgressBar* CreateRegenGhost(UProgressBar* Real);
	static void SetGaugeValue(UProgressBar* Gauge, const FText& Name, float CurrentValue, float MaxValue);

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> GaugeBackground;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UProgressBar> HealthGauge;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UProgressBar> ManaGauge;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UProgressBar> StaminaGauge;
	UPROPERTY(Transient) TObjectPtr<UProgressBar> HealthRegenGhost;
	UPROPERTY(Transient) TObjectPtr<UProgressBar> ManaRegenGhost;
	UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> HealthFrames;
	UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> ManaFrames;
	UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> StaminaFrames;
	float FrameAccumulator = 0.0f;
	int32 FrameIndex = 0;
};
