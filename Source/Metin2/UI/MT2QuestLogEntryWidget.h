/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "UI/MT2UserWidget.h"
#include "CoreMinimal.h"
#include "MT2QuestLogEntryWidget.generated.h"

class UButton;
class UTextBlock;
class UVerticalBox;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMT2QuestEntryClickedSignature, FName, QuestId);

// One clickable row of the quest log: title, optional summary and progress counter, wrapped in a
// button so clicking it opens that quest's dialog. A concrete class (not a bare UUserWidget) both
// because the widget system refuses to instantiate UUserWidget directly and because each row needs to
// remember which quest it belongs to for the click handler.
UCLASS()
class METIN2_API UMT2QuestLogEntryWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	// Fills the row. Summary/Counter are optional (Counter < 0 hides the progress line).
	void SetEntry(FName InQuestId, const FText& Title, const FText& Summary, int32 Counter);

	UPROPERTY(BlueprintAssignable, Category = "Quest")
	FMT2QuestEntryClickedSignature OnQuestClicked;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UFUNCTION()
	void HandleClicked();

	UPROPERTY(Transient) TObjectPtr<UButton> ClickButton;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> Lines;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TitleText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SummaryText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CounterText;

	// Applies the cached values to the text blocks (no-op until the tree is built).
	void ApplyEntry();

	// SetEntry is called right after CreateWidget, before RebuildWidget has built the tree, so the
	// values are cached here and applied once the text blocks exist.
	FName QuestId;
	FText PendingTitle;
	FText PendingSummary;
	int32 PendingCounter = -1;
};
