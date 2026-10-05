/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2WhisperWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Messenger/MT2MessengerComponent.h"
#include "Player/MT2PlayerState.h"
#include "UI/MT2UIStyle.h"

void UMT2WhisperWidget::NativeConstruct()
{
	Super::NativeConstruct();

	RootSizeBox->SetWidthOverride(WindowSize.X);
	RootSizeBox->SetHeightOverride(WindowSize.Y);
	// MT2GameHUD owns this as a child. Its Canvas slot controls initial placement and size.

	SendButton->OnClicked.AddUniqueDynamic(this, &UMT2WhisperWidget::HandleSendClicked);
	CloseButton->OnClicked.AddUniqueDynamic(this, &UMT2WhisperWidget::HandleCloseClicked);
	MinimizeButton->OnClicked.AddUniqueDynamic(this, &UMT2WhisperWidget::HandleMinimizeClicked);
	// The old chatline sent on Enter; a multi-line box keeps the newline, so the send is driven from
	// the text changing rather than from a commit event.
	ChatLine->OnTextChanged.AddUniqueDynamic(this, &UMT2WhisperWidget::HandleChatLineChanged);

	GameMasterMark->SetVisibility(ESlateVisibility::Collapsed);

	BindMessenger();
	RefreshWhisper();
}

void UMT2WhisperWidget::NativeDestruct()
{
	if (BoundMessenger.IsValid())
	{
		BoundMessenger->OnMessengerChanged.RemoveDynamic(this, &UMT2WhisperWidget::HandleMessengerChanged);
	}
	Super::NativeDestruct();
}

UMT2MessengerComponent* UMT2WhisperWidget::ResolveMessenger() const
{
	const APlayerController* Controller = GetOwningPlayer();
	AMT2PlayerState* State = Controller ? Controller->GetPlayerState<AMT2PlayerState>() : nullptr;
	return State ? State->GetMessengerComponent() : nullptr;
}

void UMT2WhisperWidget::BindMessenger()
{
	if (BoundMessenger.IsValid())
	{
		return;
	}
	if (UMT2MessengerComponent* Messenger = ResolveMessenger())
	{
		BoundMessenger = Messenger;
		Messenger->OnMessengerChanged.AddUniqueDynamic(this, &UMT2WhisperWidget::HandleMessengerChanged);
	}
}

void UMT2WhisperWidget::HandleMessengerChanged()
{
	RefreshWhisper();
}

void UMT2WhisperWidget::RefreshWhisper()
{
	// The PlayerState can arrive after the widget, so keep trying until the component turns up.
	BindMessenger();

	UMT2MessengerComponent* Messenger = BoundMessenger.Get();
	const FString CompanionId = Messenger ? Messenger->GetOpenConversationId() : FString();
	if (CompanionId.IsEmpty())
	{
		// Nothing is open, so there is nothing for this window to be about.
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	// The name comes from the component because a whisper target need not be a friend; the friend list
	// only fills in the level and lamp.
	const FMT2FriendEntry* Companion = Messenger->GetFriends().FindByPredicate(
		[&CompanionId](const FMT2FriendEntry& Entry) { return Entry.CharacterId == CompanionId; });
	const FString CompanionName = Companion && !Companion->CharacterName.IsEmpty()
		? Companion->CharacterName : Messenger->GetOpenConversationName();
	TitleNameText->SetText(FText::FromString(CompanionName));
	// interfacemodule.py line 1574: the mark belongs to the person whose window this is, so it shows
	// when the *companion* is a game master - not when the local player is one.
	GameMasterMark->SetVisibility(Messenger->IsOpenConversationGameMaster()
		? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

	const bool bWasVisible = GetVisibility() != ESlateVisibility::Collapsed;
	SetVisibility(ESlateVisibility::Visible);
	// The HUD Blueprint owns its initial placement. Keep that placement as the drag origin.
	if (!bPositionInitialized)
	{
		bPositionInitialized = true;
		if (const UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
		{
			WindowPosition = CanvasSlot->GetPosition();
		}
	}
	const ESlateVisibility BodyVisibility =
		bMinimized ? ESlateVisibility::Collapsed : ESlateVisibility::Visible;
	ChatLogBox->SetVisibility(BodyVisibility);
	ChatLine->SetVisibility(BodyVisibility);
	SendButton->SetVisibility(BodyVisibility);
	if (!bMinimized)
	{
		BuildChatLog();
		// The original chatline was ready immediately when a whisper opened. Only focus on opening,
		// otherwise a replicated messenger refresh would steal focus while the player is typing.
		if (!bWasVisible)
		{
			ChatLine->SetKeyboardFocus();
		}
	}
}

void UMT2WhisperWidget::BuildChatLog()
{
	UMT2MessengerComponent* Messenger = BoundMessenger.Get();
	if (!Messenger || !WidgetTree)
	{
		return;
	}
	const FString CompanionId = Messenger->GetOpenConversationId();
	const FString CompanionName = TitleNameText->GetText().ToString();

	ChatLogBox->ClearChildren();
	for (const FMT2PrivateMessage& Message : Messenger->GetConversation())
	{
		const bool bFromCompanion = Message.SenderCharacterId == CompanionId;
		// "Name : text", the old whisper log's shape.
		const FString Line = bFromCompanion
			? FString::Printf(TEXT("%s : %s"),
				Message.SenderName.IsEmpty() ? *CompanionName : *Message.SenderName, *Message.Body)
			: FString::Printf(TEXT("You : %s"), *Message.Body);

		UTextBlock* Label = FMT2UIStyle::Label(
			*WidgetTree, FText::FromString(Line), LineFontSize, ETextJustify::Left);
		Label->SetColorAndOpacity(FSlateColor(bFromCompanion ? CompanionLineColor : OwnLineColor));
		Label->SetAutoWrapText(true);
		ChatLogBox->AddChild(Label);

		// A message that waited in the store gets the note the old game had no way to show.
		if (Message.bWasOffline)
		{
			UTextBlock* Notice = FMT2UIStyle::Label(*WidgetTree,
				FText::FromString(TEXT("This message was sent while you were offline.")),
				LineFontSize - 1, ETextJustify::Left);
			Notice->SetColorAndOpacity(FSlateColor(OfflineNoticeColor));
			Notice->SetAutoWrapText(true);
			ChatLogBox->AddChild(Notice);
		}
	}
	ChatLogBox->ScrollToEnd();
}

void UMT2WhisperWidget::HandleSendClicked()
{
	UMT2MessengerComponent* Messenger = BoundMessenger.Get();
	if (!Messenger)
	{
		return;
	}
	const FString Body = ChatLine->GetText().ToString().TrimStartAndEnd();
	const FString CompanionId = Messenger->GetOpenConversationId();
	if (Body.IsEmpty() || CompanionId.IsEmpty())
	{
		return;
	}
	Messenger->SendPrivateMessage(CompanionId, Body);
	ChatLine->SetText(FText::GetEmpty());
}

void UMT2WhisperWidget::HandleChatLineChanged(const FText& Text)
{
	// Enter sends, as the old chatline did. A multi-line box swallows the key, so the newline it
	// leaves behind is the signal - and it is stripped so it never reaches the message.
	const FString Current = Text.ToString();
	if (!Current.Contains(TEXT("\n")) && !Current.Contains(TEXT("\r")))
	{
		return;
	}
	FString Stripped = Current;
	Stripped.ReplaceInline(TEXT("\r"), TEXT(""));
	Stripped.ReplaceInline(TEXT("\n"), TEXT(""));
	ChatLine->SetText(FText::FromString(Stripped));
	HandleSendClicked();
}

void UMT2WhisperWidget::HandleCloseClicked()
{
	CloseWhisper();
}

void UMT2WhisperWidget::CloseWhisper()
{
	bMinimized = false;
	if (BoundMessenger.IsValid())
	{
		// Closing the window ends the conversation, so the messenger list stops showing it as open.
		BoundMessenger->CloseConversation();
	}
	SetVisibility(ESlateVisibility::Collapsed);
}

FReply UMT2WhisperWidget::NativeOnMouseButtonDown(
	const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// Dragging starts on the title band only; the log and the edit bar keep their own clicks.
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const FVector2D Local = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		if (Local.Y >= 0.0 && Local.Y <= TitleBarHeight &&
			Local.X >= 0.0 && Local.X <= InGeometry.GetLocalSize().X)
		{
			bDragging = true;
			DragStartMouse = InMouseEvent.GetScreenSpacePosition();
			DragStartWindow = WindowPosition;
			return FReply::Handled().CaptureMouse(TakeWidget());
		}
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UMT2WhisperWidget::NativeOnMouseMove(
	const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDragging && HasMouseCapture())
	{
		// Screen-space delta is in device pixels; the viewport position is not, so divide the scale out.
		const float Scale = FMath::Max(InGeometry.Scale, UE_SMALL_NUMBER);
		WindowPosition =
			DragStartWindow + (InMouseEvent.GetScreenSpacePosition() - DragStartMouse) / Scale;

		// Keep the title bar on screen: a window dragged past the edge could never be grabbed back.
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

FReply UMT2WhisperWidget::NativeOnMouseButtonUp(
	const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDragging && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDragging = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

void UMT2WhisperWidget::HandleMinimizeClicked()
{
	bMinimized = !bMinimized;
	RefreshWhisper();
}
