/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2QuestLogWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "UI/MT2QuestLogEntryWidget.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "Player/MT2PlayerState.h"
#include "Quests/MT2QuestManagerComponent.h"
#include "TimerManager.h"

TSharedRef<SWidget> UMT2QuestLogWidget::RebuildWidget()
{
	// Code-only widget: build the list container as the root.
	if (!EntriesBox && WidgetTree)
	{
		EntriesBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("EntriesBox"));
		WidgetTree->RootWidget = EntriesBox;
	}
	return Super::RebuildWidget();
}

void UMT2QuestLogWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (UMT2QuestManagerComponent* QuestManager = ResolveQuestManager())
	{
		BoundQuestManager = QuestManager;
		QuestManager->OnJournalChanged.AddUniqueDynamic(this, &UMT2QuestLogWidget::RefreshEntries);
	}
	// The PlayerState (and its replicated journal) may arrive after this widget is constructed, so poll
	// slowly as a fallback in addition to the change delegate.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(RefreshTimer, this, &UMT2QuestLogWidget::RefreshEntries, 1.0f, true);
	}
	RefreshEntries();
}

void UMT2QuestLogWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimer);
	}
	if (BoundQuestManager.IsValid())
	{
		BoundQuestManager->OnJournalChanged.RemoveDynamic(this, &UMT2QuestLogWidget::RefreshEntries);
	}
	Super::NativeDestruct();
}

UMT2QuestManagerComponent* UMT2QuestLogWidget::ResolveQuestManager() const
{
	const APlayerController* Controller = GetOwningPlayer();
	AMT2PlayerState* State = Controller ? Controller->GetPlayerState<AMT2PlayerState>() : nullptr;
	return State ? State->GetQuestManagerComponent() : nullptr;
}

void UMT2QuestLogWidget::RefreshEntries()
{
	if (!EntriesBox)
	{
		return;
	}

	UMT2QuestManagerComponent* QuestManager = BoundQuestManager.Get();
	if (!QuestManager)
	{
		QuestManager = ResolveQuestManager();
		if (QuestManager)
		{
			BoundQuestManager = QuestManager;
			QuestManager->OnJournalChanged.AddUniqueDynamic(this, &UMT2QuestLogWidget::RefreshEntries);
		}
	}

	EntriesBox->ClearChildren();

	const TArray<FMT2QuestJournalEntry>& Entries =
		QuestManager ? QuestManager->GetJournalEntries() : TArray<FMT2QuestJournalEntry>();
	if (Entries.IsEmpty())
	{
		UTextBlock* Empty = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Empty->SetText(FText::FromString(TEXT("No active quests.")));
		FSlateFontInfo Font = Empty->GetFont();
		Font.Size = 8;
		Empty->SetFont(Font);
		Empty->SetColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)));
		if (UVerticalBoxSlot* BoxSlot = EntriesBox->AddChildToVerticalBox(Empty))
		{
			BoxSlot->SetPadding(FMargin(6.0f, 8.0f, 6.0f, 0.0f));
		}
		return;
	}

	bool bIsFirstRow = true;
	for (const FMT2QuestJournalEntry& Entry : Entries)
	{
		// An entry with no title, summary or counter has nothing to tell the player. That happens when a
		// quest registered itself but its title lives in a script function the importer could not
		// convert; showing the script id ("dragon_lair_access") in its place is an identifier, not text.
		if (Entry.Title.IsEmpty() && Entry.Summary.IsEmpty() && Entry.Counter < 0)
		{
			continue;
		}

		UMT2QuestLogEntryWidget* Row = CreateWidget<UMT2QuestLogEntryWidget>(
			GetOwningPlayer(), UMT2QuestLogEntryWidget::StaticClass());
		if (!Row)
		{
			continue;
		}
		Row->SetEntry(Entry.QuestId, Entry.Title, Entry.Summary, Entry.Counter);
		Row->OnQuestClicked.AddUniqueDynamic(this, &UMT2QuestLogWidget::HandleQuestClicked);
		if (UVerticalBoxSlot* BoxSlot = EntriesBox->AddChildToVerticalBox(Row))
		{
			// The first row clears the panel's title bar, which the entries box sits under.
			BoxSlot->SetPadding(FMargin(2.0f, bIsFirstRow ? 14.0f : 3.0f, 2.0f, 0.0f));
			// Auto height: each row takes exactly the space its lines need. Sharing the box's height
			// instead lets rows overlap once one of them has more than a single line.
			FSlateChildSize Size;
			Size.SizeRule = ESlateSizeRule::Automatic;
			BoxSlot->SetSize(Size);
			BoxSlot->SetHorizontalAlignment(HAlign_Fill);
		}
		bIsFirstRow = false;
	}
}

void UMT2QuestLogWidget::HandleQuestClicked(FName QuestId)
{
	if (UMT2QuestManagerComponent* QuestManager = ResolveQuestManager())
	{
		QuestManager->ServerOpenQuestDialog(QuestId);
	}
}
