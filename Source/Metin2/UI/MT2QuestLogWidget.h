/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "UI/MT2UserWidget.h"
#include "MT2QuestLogWidget.generated.h"

class UMT2QuestManagerComponent;
class UVerticalBox;
class UTextBlock;

// The quest log shown on the character window's Quests page: one entry per active quest, with its
// title, summary line and progress counter, and completed quests marked. Entirely code-driven (it
// builds its own list container), so it needs no UI asset - the character window just hosts it.
UCLASS()
class METIN2_API UMT2QuestLogWidget : public UMT2UserWidget
{
	GENERATED_BODY()

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	// Rebuilds the list from the player's replicated journal.
	UFUNCTION()
	void RefreshEntries();

	// A row was clicked: ask the server to run that quest's dialog.
	UFUNCTION()
	void HandleQuestClicked(FName QuestId);

	UMT2QuestManagerComponent* ResolveQuestManager() const;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> EntriesBox;

	TWeakObjectPtr<UMT2QuestManagerComponent> BoundQuestManager;
	FTimerHandle RefreshTimer;
};
