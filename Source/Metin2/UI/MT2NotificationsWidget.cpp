/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2NotificationsWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Messenger/MT2MessengerComponent.h"
#include "Player/MT2PlayerState.h"
#include "Quests/MT2QuestManagerComponent.h"
#include "UI/MT2HUD.h"
#include "UI/MT2UIStyle.h"

namespace
{
	FButtonStyle MakeEntryStyle()
	{
		FButtonStyle Style;
		Style.Normal.DrawAs = ESlateBrushDrawType::NoDrawType;
		Style.Hovered.DrawAs = ESlateBrushDrawType::Box;
		Style.Hovered.TintColor = FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.10f));
		Style.Pressed = Style.Hovered;
		return Style;
	}
}

void UMT2NotificationsWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// Guards the frames before the first refresh decides what to show.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	NotificationsBox->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	BindSources();
	RefreshNotifications();
}

void UMT2NotificationsWidget::NativeDestruct()
{
	if (BoundMessenger.IsValid())
	{
		BoundMessenger->OnMessengerChanged.RemoveDynamic(this, &UMT2NotificationsWidget::HandleSourcesChanged);
	}
	if (BoundQuestManager.IsValid())
	{
		BoundQuestManager->OnJournalChanged.RemoveDynamic(this, &UMT2NotificationsWidget::HandleSourcesChanged);
	}
	Super::NativeDestruct();
}

UMT2MessengerComponent* UMT2NotificationsWidget::ResolveMessenger() const
{
	const APlayerController* Controller = GetOwningPlayer();
	AMT2PlayerState* State = Controller ? Controller->GetPlayerState<AMT2PlayerState>() : nullptr;
	return State ? State->GetMessengerComponent() : nullptr;
}

UMT2QuestManagerComponent* UMT2NotificationsWidget::ResolveQuestManager() const
{
	const APlayerController* Controller = GetOwningPlayer();
	AMT2PlayerState* State = Controller ? Controller->GetPlayerState<AMT2PlayerState>() : nullptr;
	return State ? State->GetQuestManagerComponent() : nullptr;
}

void UMT2NotificationsWidget::BindSources()
{
	if (!BoundMessenger.IsValid())
	{
		if (UMT2MessengerComponent* Messenger = ResolveMessenger())
		{
			BoundMessenger = Messenger;
			Messenger->OnMessengerChanged.AddUniqueDynamic(this, &UMT2NotificationsWidget::HandleSourcesChanged);
		}
	}
	if (!BoundQuestManager.IsValid())
	{
		if (UMT2QuestManagerComponent* QuestManager = ResolveQuestManager())
		{
			BoundQuestManager = QuestManager;
			QuestManager->OnJournalChanged.AddUniqueDynamic(this, &UMT2NotificationsWidget::HandleSourcesChanged);
		}
	}
}

void UMT2NotificationsWidget::HandleSourcesChanged()
{
	RefreshNotifications();
}

void UMT2NotificationsWidget::ToggleNotifications()
{
	bUserHidden = !bUserHidden;
	RefreshNotifications();
}

void UMT2NotificationsWidget::RefreshNotifications()
{
	// The PlayerState can arrive after the widget, so keep trying until both components turn up.
	BindSources();
	if (!WidgetTree)
	{
		return;
	}

	NotificationsBox->ClearChildren();
	EntryButtons.Reset();
	EntryTargets.Reset();

	UTexture2D* LoadedQuestIcon = QuestIcon.LoadSynchronous();
	UTexture2D* LoadedMessageIcon = MessageIcon.LoadSynchronous();

	// One entry: the icon, with its name underneath.
	auto AddEntry = [&](UTexture2D* Icon, const FString& Label, const FLinearColor& LabelColor,
		const FNotificationTarget& Target)
	{
		UButton* Entry = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		Entry->SetStyle(MakeEntryStyle());
		Entry->OnClicked.AddUniqueDynamic(this, &UMT2NotificationsWidget::HandleEntryClicked);
		ApplyWidgetSound(Entry);

		UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		Entry->AddChild(Stack);

		UImage* IconImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		if (Icon)
		{
			IconImage->SetBrushFromTexture(Icon);
			IconImage->SetDesiredSizeOverride(IconSize);
		}
		if (UVerticalBoxSlot* IconSlot = Stack->AddChildToVerticalBox(IconImage))
		{
			IconSlot->SetHorizontalAlignment(HAlign_Center);
		}

		UTextBlock* Text = FMT2UIStyle::Label(
			*WidgetTree, FText::FromString(Label), LabelFontSize, ETextJustify::Center);
		Text->SetColorAndOpacity(FSlateColor(LabelColor));
		Text->SetAutoWrapText(true);
		if (UVerticalBoxSlot* TextSlot = Stack->AddChildToVerticalBox(Text))
		{
			TextSlot->SetHorizontalAlignment(HAlign_Center);
			TextSlot->SetPadding(FMargin(2.0f, 2.0f, 2.0f, 0.0f));
		}

		if (UButtonSlot* StackSlot = Cast<UButtonSlot>(Stack->Slot))
		{
			StackSlot->SetPadding(FMargin(4.0f, 2.0f));
		}

		NotificationsBox->AddChild(Entry);
		EntryButtons.Add(Entry);
		EntryTargets.Add(Target);
	};

	// Active quests first: they are the standing objective, messages are transient.
	if (const UMT2QuestManagerComponent* QuestManager = BoundQuestManager.Get())
	{
		for (const FMT2QuestJournalEntry& Journal : QuestManager->GetJournalEntries())
		{
			// An entry with nothing to show is one whose title never converted; the quest log skips
			// those too, and a nameless icon would say nothing.
			if (Journal.Title.IsEmpty())
			{
				continue;
			}
			FNotificationTarget Target;
			Target.QuestId = Journal.QuestId;
			AddEntry(LoadedQuestIcon, Journal.Title.ToString(), QuestLabelColor, Target);
		}
	}

	if (const UMT2MessengerComponent* Messenger = BoundMessenger.Get())
	{
		// Driven by the unread map rather than the friend list: a whisper can come from anyone, and a
		// notification from a stranger has to appear just the same. The map is already empty for a
		// conversation the player has open, so an open whisper window never raises one.
		for (const TPair<FString, int32>& Unread : Messenger->GetUnreadCounts())
		{
			if (Unread.Value <= 0)
			{
				continue;
			}
			FNotificationTarget Target;
			Target.CompanionId = Unread.Key;

			// Prefer the friend list's name, fall back to the one the message carried.
			const FMT2FriendEntry* Friend = Messenger->GetFriends().FindByPredicate(
				[&Unread](const FMT2FriendEntry& Entry) { return Entry.CharacterId == Unread.Key; });
			FString Name = Friend ? Friend->CharacterName : Messenger->GetUnreadSenderName(Unread.Key);
			if (Name.IsEmpty())
			{
				Name = TEXT("Message");
			}
			const FString Label = Unread.Value > 1
				? FString::Printf(TEXT("%s (%d)"), *Name, Unread.Value) : Name;
			AddEntry(LoadedMessageIcon, Label, MessageLabelColor, Target);
		}
	}

	// Nothing pending: the strip disappears rather than sitting empty on the HUD.
	//
	// SelfHitTestInvisible, never Visible: this widget's root canvas covers the whole viewport, so a
	// hit-testable root would swallow every click meant for the world or another window - camera,
	// movement and the other UIs all stop working. Only the entry buttons take the mouse.
	SetVisibility(bUserHidden || EntryButtons.IsEmpty()
		? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
}

void UMT2NotificationsWidget::HandleEntryClicked()
{
	for (int32 Index = 0; Index < EntryButtons.Num(); ++Index)
	{
		if (!EntryButtons[Index] || !EntryButtons[Index]->IsHovered() ||
			!EntryTargets.IsValidIndex(Index))
		{
			continue;
		}
		const FNotificationTarget& Target = EntryTargets[Index];
		if (!Target.CompanionId.IsEmpty() && BoundMessenger.IsValid())
		{
			// Opening the conversation is what makes the whisper dialog show itself.
			BoundMessenger->OpenConversation(Target.CompanionId);
			if (AMT2HUD* HUD = GetOwningPlayer() ? GetOwningPlayer()->GetHUD<AMT2HUD>() : nullptr)
			{
				HUD->OpenWhisperWindow();
			}
		}
		else if (!Target.QuestId.IsNone() && BoundQuestManager.IsValid())
		{
			BoundQuestManager->ServerOpenQuestDialog(Target.QuestId);
		}
		return;
	}
}
