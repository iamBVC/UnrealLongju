/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2MessengerWidget.h"

#include "Blueprint/WidgetTree.h"
#include "UI/MT2TitleBarWidget.h"
#include "UI/MT2AtlasImage.h"
#include "UI/MT2AtlasTypes.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ButtonSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Engine/Texture2D.h"
#include "Components/Image.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/PanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Messenger/MT2MessengerComponent.h"
#include "Player/MT2PlayerState.h"
#include "Player/MT2PlayerController.h"
#include "UI/MT2HUD.h"
#include "UI/MT2UIStyle.h"

namespace
{
	// A row is the click target itself, as in the old list where the whole entry picked up the mouse.
	// The selected row gets the blue bar the original drew in OnRender.
	FButtonStyle MakeRowStyle(const FLinearColor& SelectionColor, bool bSelected)
	{
		FButtonStyle Style;
		Style.Normal.DrawAs = bSelected ? ESlateBrushDrawType::Box : ESlateBrushDrawType::NoDrawType;
		Style.Normal.TintColor = FSlateColor(SelectionColor);
		Style.Hovered.DrawAs = ESlateBrushDrawType::Box;
		Style.Hovered.TintColor = FSlateColor(bSelected
			? SelectionColor : FLinearColor(1.0f, 1.0f, 1.0f, 0.08f));
		Style.Pressed = Style.Hovered;
		return Style;
	}

	FString FormatTimestamp(int64 UnixSeconds)
	{
		if (UnixSeconds <= 0)
		{
			return FString();
		}
		return FDateTime::FromUnixTimestamp(UnixSeconds).ToString(TEXT("%H:%M"));
	}
}

void UMT2MessengerWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (RootSizeBox)
	{
		// The old window was a fixed 170x300 board; this keeps the same idea of a small floating
		// window rather than something that stretches to fill the viewport.
		RootSizeBox->SetWidthOverride(WindowSize.X);
		RootSizeBox->SetHeightOverride(WindowSize.Y);
	}
	// MT2GameHUD owns this as a child. Its Canvas slot controls the initial placement and size.
	if (AddFriendButton)
	{
		AddFriendButton->OnClicked.AddUniqueDynamic(this, &UMT2MessengerWidget::HandleAddFriendClicked);
	}
	if (WhisperButton)
	{
		WhisperButton->OnClicked.AddUniqueDynamic(this, &UMT2MessengerWidget::HandleWhisperButtonClicked);
	}
	if (RemoveButton)
	{
		RemoveButton->OnClicked.AddUniqueDynamic(this, &UMT2MessengerWidget::HandleRemoveButtonClicked);
	}
	
	// The title bar owns the X; nothing was listening to it, so the button did nothing.
	if (TitleBarWidget)
	{
		TitleBarWidget->OnCloseClicked.AddUniqueDynamic(this, &UMT2MessengerWidget::HandleTitleBarClose);
	}

	BindMessenger();
	RefreshMessenger();
}

void UMT2MessengerWidget::HandleTitleBarClose()
{
	CloseMessenger();
}

void UMT2MessengerWidget::CloseMessenger()
{
	SetVisibility(ESlateVisibility::Collapsed);
}

FReply UMT2MessengerWidget::NativeOnMouseButtonDown(
	const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// Dragging starts on the title band only, so the list and the buttons keep their own clicks.
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const FVector2D Local = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		if (Local.Y >= 0.0 && Local.Y <= TitleBarHeight &&
			Local.X >= 0.0 && Local.X <= InGeometry.GetLocalSize().X)
		{
			if (!bPositionInitialized)
			{
				// First drag: start from wherever the window currently sits on screen.
				bPositionInitialized = true;
				WindowPosition = InGeometry.LocalToAbsolute(FVector2D::ZeroVector) /
					FMath::Max(InGeometry.Scale, UE_SMALL_NUMBER);
			}
			bDragging = true;
			DragStartMouse = InMouseEvent.GetScreenSpacePosition();
			DragStartWindow = WindowPosition;
			return FReply::Handled().CaptureMouse(TakeWidget());
		}
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UMT2MessengerWidget::NativeOnMouseMove(
	const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDragging && HasMouseCapture())
	{
		const float Scale = FMath::Max(InGeometry.Scale, UE_SMALL_NUMBER);
		WindowPosition =
			DragStartWindow + (InMouseEvent.GetScreenSpacePosition() - DragStartMouse) / Scale;

		// Keep the title bar reachable: a window dragged off screen could never be grabbed back.
		const FVector2D ViewportSize = UWidgetLayoutLibrary::GetViewportSize(this);
		WindowPosition.X = FMath::Clamp(WindowPosition.X, -WindowSize.X * 0.5, ViewportSize.X - 40.0);
		WindowPosition.Y = FMath::Clamp(WindowPosition.Y, 0.0, ViewportSize.Y - TitleBarHeight);

		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
		{
			CanvasSlot->SetPosition(WindowPosition);
		}
		return FReply::Handled();
	}
	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

FReply UMT2MessengerWidget::NativeOnMouseButtonUp(
	const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDragging && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDragging = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

void UMT2MessengerWidget::NativeDestruct()
{
	if (BoundMessenger.IsValid())
	{
		BoundMessenger->OnMessengerChanged.RemoveDynamic(this, &UMT2MessengerWidget::HandleMessengerChanged);
		BoundMessenger->OnMessengerActionResult.RemoveDynamic(this, &UMT2MessengerWidget::HandleActionResult);
	}
	Super::NativeDestruct();
}

UMT2MessengerComponent* UMT2MessengerWidget::ResolveMessenger() const
{
	const APlayerController* Controller = GetOwningPlayer();
	AMT2PlayerState* State = Controller ? Controller->GetPlayerState<AMT2PlayerState>() : nullptr;
	return State ? State->GetMessengerComponent() : nullptr;
}

void UMT2MessengerWidget::BindMessenger()
{
	if (BoundMessenger.IsValid())
	{
		return;
	}
	if (UMT2MessengerComponent* Messenger = ResolveMessenger())
	{
		BoundMessenger = Messenger;
		Messenger->OnMessengerChanged.AddUniqueDynamic(this, &UMT2MessengerWidget::HandleMessengerChanged);
		Messenger->OnMessengerActionResult.AddUniqueDynamic(this, &UMT2MessengerWidget::HandleActionResult);
	}
}

void UMT2MessengerWidget::HandleMessengerChanged()
{
	RefreshMessenger();
}

void UMT2MessengerWidget::RefreshMessenger()
{
	// The PlayerState can arrive after the widget, so keep trying until the component turns up.
	BindMessenger();

	BuildFriendRows();
}

void UMT2MessengerWidget::BuildFriendRows()
{
	if (!FriendsBox)
	{
		return;
	}
	FriendsBox->ClearChildren();
	FriendButtons.Reset();
	FriendButtonIds.Reset();
	GroupButtons.Reset();

	UMT2MessengerComponent* Messenger = BoundMessenger.Get();
	if (!Messenger || !WidgetTree)
	{
		return;
	}

	// uimessenger.py splits each group into GetLoginMemberList() then GetLogoutMemberList(), so the
	// people you can actually talk to sit at the top.
	TArray<FMT2FriendEntry> Sorted = Messenger->GetFriends();
	Sorted.Sort([](const FMT2FriendEntry& A, const FMT2FriendEntry& B)
	{
		if (A.bOnline != B.bOnline)
		{
			return A.bOnline;
		}
		return A.CharacterName.Compare(B.CharacterName, ESearchCase::IgnoreCase) < 0;
	});

	// The old window's three groups. Guild and Ignored have no source of members yet - there is no
	// guild roster in the messenger and no ignore list - so they show "Empty" rather than being hidden,
	// which is what the original does for an empty group.
	BuildGroup(0, TEXT("Friends"), Sorted);
	BuildGroup(1, TEXT("Guild"), {});
	BuildGroup(2, TEXT("Ignored"), {});
	UpdateActionButtons();
}

void UMT2MessengerWidget::BuildGroup(
	int32 GroupIndex, const FString& Title, const TArray<FMT2FriendEntry>& Members)
{
	const bool bOpen = GroupOpen.IsValidIndex(GroupIndex) ? GroupOpen[GroupIndex] : true;

	// Header: messenger_list_open/close.sub from the old T_windows atlas, plus the group's name.
	UButton* Header = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	Header->SetStyle(MakeRowStyle(SelectionColor, false));
	Header->OnClicked.AddUniqueDynamic(this, &UMT2MessengerWidget::HandleGroupHeaderClicked);
	ApplyWidgetSound(Header);

	UHorizontalBox* HeaderBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Header->AddChild(HeaderBox);
	UMT2AtlasImage* Chevron = WidgetTree->ConstructWidget<UMT2AtlasImage>(UMT2AtlasImage::StaticClass());
	if (UTexture2D* Atlas = MessengerAtlas.LoadSynchronous())
	{
		Chevron->SetAtlas(Atlas, bOpen ? FMT2AtlasRect(389, 135, 15, 15) : FMT2AtlasRect(374, 135, 15, 15));
	}
	if (GuildButton)
	{
		GuildButton->OnClicked.AddUniqueDynamic(this, &UMT2MessengerWidget::HandleGuildButtonClicked);
	}
	if (UHorizontalBoxSlot* ChevronSlot = HeaderBox->AddChildToHorizontalBox(Chevron))
	{
		ChevronSlot->SetPadding(FMargin(2.0f, 0.0f, 6.0f, 0.0f));
		ChevronSlot->SetVerticalAlignment(VAlign_Center);
	}
	UTextBlock* HeaderLabel = FMT2UIStyle::Label(
		*WidgetTree, FText::FromString(Title), RowFontSize, ETextJustify::Left);
	HeaderLabel->SetColorAndOpacity(FSlateColor(GroupHeaderColor));
	if (UHorizontalBoxSlot* LabelSlot = HeaderBox->AddChildToHorizontalBox(HeaderLabel))
	{
		LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		LabelSlot->SetVerticalAlignment(VAlign_Center);
	}
	FriendsBox->AddChild(Header);
	GroupButtons.Add(Header);

	if (!bOpen)
	{
		return;
	}

	// MESSENGER_EMPTY_LIST: an open group with nobody in it still says so.
	if (Members.IsEmpty())
	{
		UTextBlock* Empty = FMT2UIStyle::Label(
			*WidgetTree, FText::FromString(TEXT("Empty")), RowFontSize, ETextJustify::Left);
		Empty->SetColorAndOpacity(FSlateColor(OfflineColor));
		// Indented under its group like a member row would be. The panel FriendsBox is bound to may be
		// any type, so the padding goes on whichever slot it hands back.
		UWidget* EmptyRow = Empty;
		if (UPanelSlot* EmptySlot = FriendsBox->AddChild(EmptyRow))
		{
			if (UVerticalBoxSlot* BoxSlot = Cast<UVerticalBoxSlot>(EmptySlot))
			{
				BoxSlot->SetPadding(FMargin(MemberIndent + 8.0f, 0.0f, 0.0f, 0.0f));
			}
			else if (UScrollBoxSlot* ScrollSlot = Cast<UScrollBoxSlot>(EmptySlot))
			{
				ScrollSlot->SetPadding(FMargin(MemberIndent + 8.0f, 0.0f, 0.0f, 0.0f));
			}
		}
		return;
	}

	for (const FMT2FriendEntry& Friend : Members)
	{
		UButton* Row = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		Row->SetStyle(MakeRowStyle(SelectionColor, Friend.CharacterId == SelectedFriendId));
		Row->OnClicked.AddUniqueDynamic(this, &UMT2MessengerWidget::HandleFriendRowClicked);
		ApplyWidgetSound(Row);

		// The old row was a lamp at x=0 with the name at x=20, indented under its group.
		UHorizontalBox* RowBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		Row->AddChild(RowBox);

		UMT2AtlasImage* Lamp = WidgetTree->ConstructWidget<UMT2AtlasImage>(UMT2AtlasImage::StaticClass());
		if (UTexture2D* LampTexture = Friend.bOnline
			? OnlineIcon.LoadSynchronous() : OfflineIcon.LoadSynchronous())
		{
			Lamp->SetBrushFromTexture(LampTexture);
			Lamp->SetDesiredSizeOverride(LampSize);
		}
		else if (UTexture2D* Atlas = MessengerAtlas.LoadSynchronous())
		{
			Lamp->SetAtlas(Atlas, Friend.bOnline ? FMT2AtlasRect(359, 135, 15, 16) : FMT2AtlasRect(344, 135, 15, 16));
		}
		Lamp->SetDesiredSizeOverride(LampSize);
		if (UHorizontalBoxSlot* LampSlot = RowBox->AddChildToHorizontalBox(Lamp))
		{
			LampSlot->SetPadding(FMargin(MemberIndent, 0.0f, 6.0f, 0.0f));
			LampSlot->SetVerticalAlignment(VAlign_Center);
		}

		FString RowText = Friend.CharacterName;
		if (Friend.UnreadCount > 0)
		{
			RowText += FString::Printf(TEXT("  [%d]"), Friend.UnreadCount);
		}
		UTextBlock* Label = FMT2UIStyle::Label(
			*WidgetTree, FText::FromString(RowText), RowFontSize, ETextJustify::Left);
		Label->SetColorAndOpacity(FSlateColor(Friend.bOnline ? OnlineColor : OfflineColor));
		if (UHorizontalBoxSlot* LabelSlot = RowBox->AddChildToHorizontalBox(Label))
		{
			LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			LabelSlot->SetVerticalAlignment(VAlign_Center);
		}

		FriendButtons.Add(Row);
		FriendButtonIds.Add(Friend.CharacterId);
		FriendsBox->AddChild(Row);
	}
}

void UMT2MessengerWidget::HandleGroupHeaderClicked()
{
	for (int32 Index = 0; Index < GroupButtons.Num(); ++Index)
	{
		if (GroupButtons[Index] && GroupButtons[Index]->IsHovered() && GroupOpen.IsValidIndex(Index))
		{
			GroupOpen[Index] = !GroupOpen[Index];
			BuildFriendRows();
			return;
		}
	}
}

FString UMT2MessengerWidget::FindIdForButton(
	const TArray<TObjectPtr<UButton>>& Buttons, const TArray<FString>& Ids) const
{
	for (int32 Index = 0; Index < Buttons.Num(); ++Index)
	{
		if (Buttons[Index] && Buttons[Index]->IsHovered() && Ids.IsValidIndex(Index))
		{
			return Ids[Index];
		}
	}
	return FString();
}

void UMT2MessengerWidget::HandleFriendRowClicked()
{
	const FString CompanionId = FindIdForButton(FriendButtons, FriendButtonIds);
	if (CompanionId.IsEmpty())
	{
		return;
	}
	// uimessenger.py: OnMouseLeftButtonDown selects, OnMouseLeftButtonDoubleClick whispers. UMG buttons
	// raise no double-click event, so a second click on the same row inside DoubleClickSeconds is one.
	const double Now = FPlatformTime::Seconds();
	const bool bDoubleClick = CompanionId == LastClickedFriendId &&
		(Now - LastClickTime) <= DoubleClickSeconds;
	LastClickedFriendId = CompanionId;
	LastClickTime = Now;

	SelectedFriendId = CompanionId;
	BuildFriendRows();
	if (bDoubleClick)
	{
		WhisperSelected();
	}
}

void UMT2MessengerWidget::WhisperSelected()
{
	if (!SelectedFriendId.IsEmpty() && BoundMessenger.IsValid())
	{
		// Opening the conversation is what makes the whisper dialog show itself, since that window
		// follows whatever the component has open.
		BoundMessenger->OpenConversation(SelectedFriendId);
		if (AMT2HUD* HUD = GetOwningPlayer() ? GetOwningPlayer()->GetHUD<AMT2HUD>() : nullptr)
		{
			HUD->OpenWhisperWindow();
		}
	}
}

void UMT2MessengerWidget::UpdateActionButtons()
{
	bool bCanWhisper = false;
	if (const UMT2MessengerComponent* Messenger = BoundMessenger.Get())
	{
		if (const FMT2FriendEntry* Friend = Messenger->GetFriends().FindByPredicate(
			[this](const FMT2FriendEntry& Entry) { return Entry.CharacterId == SelectedFriendId; }))
		{
			bCanWhisper = Friend->bOnline;
		}
	}
	if (WhisperButton) WhisperButton->SetIsEnabled(bCanWhisper);
	if (RemoveButton) RemoveButton->SetIsEnabled(!SelectedFriendId.IsEmpty());
	if (MobileButton) MobileButton->SetIsEnabled(false);
	if (GuildButton) GuildButton->SetIsEnabled(true);
}

void UMT2MessengerWidget::RemoveSelected()
{
	if (!SelectedFriendId.IsEmpty() && BoundMessenger.IsValid())
	{
		BoundMessenger->RemoveFriend(SelectedFriendId);
		SelectedFriendId.Reset();
	}
}

void UMT2MessengerWidget::HandleWhisperButtonClicked() { WhisperSelected(); }
void UMT2MessengerWidget::HandleRemoveButtonClicked() { RemoveSelected(); }
void UMT2MessengerWidget::HandleGuildButtonClicked()
{
	if (AMT2HUD* HUD = GetOwningPlayer() ? GetOwningPlayer()->GetHUD<AMT2HUD>() : nullptr)
		HUD->ToggleGuildWindow();
}

void UMT2MessengerWidget::HandleAcceptRequestClicked()
{
	const FString RequesterId = FindIdForButton(AcceptButtons, RequestButtonIds);
	if (!RequesterId.IsEmpty() && BoundMessenger.IsValid())
	{
		BoundMessenger->AnswerFriendRequest(RequesterId, true);
	}
}

void UMT2MessengerWidget::HandleDeclineRequestClicked()
{
	const FString RequesterId = FindIdForButton(DeclineButtons, RequestButtonIds);
	if (!RequesterId.IsEmpty() && BoundMessenger.IsValid())
	{
		BoundMessenger->AnswerFriendRequest(RequesterId, false);
	}
}

void UMT2MessengerWidget::HandleAddFriendClicked()
{
	if (AMT2HUD* HUD = GetOwningPlayer() ? GetOwningPlayer()->GetHUD<AMT2HUD>() : nullptr)
	{
		HUD->OpenFriendAddDialog();
	}
}




void UMT2MessengerWidget::HandleActionResult(FName Action, EMT2MessengerResult Result)
{
	FString Notice;
	switch (Result)
	{
	case EMT2MessengerResult::Success:          Notice = TEXT("Request sent."); break;
	case EMT2MessengerResult::UnknownCharacter: Notice = TEXT("No such character is online."); break;
	case EMT2MessengerResult::AlreadyFriends:   Notice = TEXT("Already on your list."); break;
	case EMT2MessengerResult::CannotAddSelf:    Notice = TEXT("You cannot add yourself."); break;
	case EMT2MessengerResult::ListFull:         Notice = TEXT("Your friend list is full."); break;
	case EMT2MessengerResult::NotFriends:       Notice = TEXT("That player is offline."); break;
	case EMT2MessengerResult::MessageTooLong:   Notice = TEXT("That message is too long."); break;
	case EMT2MessengerResult::Unavailable:      Notice = TEXT("Messenger is unavailable right now."); break;
	default: break;
	}
	if (!Notice.IsEmpty())
	{
		if (AMT2PlayerController* Controller = Cast<AMT2PlayerController>(GetOwningPlayer()))
		{
			Controller->AddInfoChatLine(Notice);
		}
	}
}
