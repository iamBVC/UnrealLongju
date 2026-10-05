/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "UI/MT2UserWidget.h"
#include "MT2StatusEffectBarWidget.generated.h"

class UHorizontalBox;
class UOverlay;
class UMT2StatusEffectComponent;

// A horizontal row of active status-effect icons for the local player (top-left of the screen). Each
// icon shows the effect's icon and, on hover, a tooltip with its value and remaining time. It is
// fully code-driven: if no Blueprint supplies an EffectsBox it builds one as its root, so the HUD can
// just spawn it from C++ without a dedicated UI asset.
UCLASS()
class METIN2_API UMT2StatusEffectBarWidget : public UMT2UserWidget
{
	GENERATED_BODY()

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	// Rebuilds/updates the icon row from the player's current effects. Runs on effect changes and once a
	// second (to refresh the remaining-time tooltips and pick up the pawn if it wasn't ready yet).
	UFUNCTION()
	void RefreshEffects();

	UMT2StatusEffectComponent* ResolveStatusEffects() const;

	// Optional Blueprint container; when absent RebuildWidget creates one as the widget root.
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UHorizontalBox> EffectsBox;

	// Live icon entries keyed by a composite affect/source identity. Skills with multiple applies keep
	// one icon, while different item buffs remain separate even if the old affect id is shared.
	UPROPERTY(Transient)
	TMap<int64, TObjectPtr<UOverlay>> EntriesByKey;

	TWeakObjectPtr<UMT2StatusEffectComponent> BoundStatusEffects;
	FTimerHandle RefreshTimer;
};
