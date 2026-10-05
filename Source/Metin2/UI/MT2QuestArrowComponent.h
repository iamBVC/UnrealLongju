/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Blueprint/UserWidget.h"
#include "Components/WidgetComponent.h"
#include "CoreMinimal.h"
#include "MT2QuestArrowComponent.generated.h"

// Concrete widget for the arrow. UUserWidget itself cannot be passed to CreateWidget (it is one of the
// "Abstract, Deprecated or Replaced" classes the widget system rejects), so the arrow needs its own
// class even though it only ever contains one image.
UCLASS()
class METIN2_API UMT2QuestArrowWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
};

// The bobbing arrow the old game floats over an NPC an active quest wants you to talk to. Purely
// cosmetic and client-side: it checks the local player's replicated quest target markers for this
// actor's vnum and shows itself only while that NPC is a target.
UCLASS(ClassGroup = "MT2", meta = (BlueprintSpawnableComponent))
class METIN2_API UMT2QuestArrowComponent : public UWidgetComponent
{
	GENERATED_BODY()

public:
	UMT2QuestArrowComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(
		float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Height above the actor's origin the arrow floats at.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest Arrow", meta = (Units = "cm"))
	float HoverHeight = 220.0f;

	// Vertical bob, so the arrow reads as a marker rather than part of the model.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest Arrow", meta = (Units = "cm"))
	float BobAmplitude = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest Arrow")
	float BobSpeed = 2.5f;

private:
	// Whether the local player currently has a quest pointing at this NPC.
	bool IsQuestTargetForLocalPlayer() const;

	// Re-evaluated on a slow timer rather than every frame; quest targets change rarely.
	void RefreshTargetState();

	bool bIsQuestTarget = false;
	float BobTime = 0.0f;
	FTimerHandle RefreshTimer;

	// A screen-space UWidgetComponent only pulls its on-screen Slate widget off inside TickComponent
	// (via UpdateWidgetOnScreen), so disabling the tick at the same moment we hide leaves the arrow
	// frozen on screen. Hiding therefore keeps ticking for one more frame, then disables.
	bool bPendingTickDisable = false;
};
