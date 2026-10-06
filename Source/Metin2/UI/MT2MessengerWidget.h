/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Config/MT2PathSettings.h"
#include "Messenger/MT2MessengerTypes.h"
#include "UI/MT2UserWidget.h"
#include "MT2MessengerWidget.generated.h"

class UButton;
class UEditableTextBox;
class UMT2MessengerComponent;
class UMT2TitleBarWidget;
class UPanelWidget;
class UScrollBox;
class USizeBox;
class UTextBlock;
class UWidget;

// The old MessengerWindow (uimessenger.py / UIScript/MessengerWindow.py): a titled board holding a
// scrollable list of companions - a lamp plus the name per row - with an icon button row along the
// bottom (add friend, whisper, remove). This is the list only; whispering opens the separate
// WhisperDialog (UMT2WhisperWidget), exactly as it did in the old client.
//
// Every visual is bound from the Widget Blueprint rather than built in code, so the layout and look
// are yours to change. The bindings are required: WBP /Game/UI/MT2Messenger must carry a widget of
// the right type for each name below, and its compile fails loudly if one goes missing.
UCLASS()
class METIN2_API UMT2MessengerWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	// Rebuilds the list from the component. Bound to the component's change delegate, so calling it
	// by hand is only needed after the widget is created.
	UFUNCTION(BlueprintCallable, Category = "Messenger")
	void RefreshMessenger();

	// Opens the whisper panel on the selected companion, the old window's Whisper button.
	UFUNCTION(BlueprintCallable, Category = "Messenger")
	void WhisperSelected();

	UFUNCTION(BlueprintCallable, Category = "Messenger")
	void RemoveSelected();

	UFUNCTION(BlueprintCallable, Category = "Messenger")
	void CloseMessenger();

	// UIScript/MessengerWindow.py: a compact 170x300 board. Add Friend opens its own prompt, leaving
	// this board for the three companion groups and its five icon commands.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Messenger|Layout")
	FVector2D WindowSize = FVector2D(170.0, 300.0);

	// The lamp beside each name (the old messenger_list_online/offline.sub). Left unset by default,
	// because those images are not extracted yet: a plain dot in the colours below is drawn instead,
	// and assigning a texture here replaces it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Messenger|Style")
	TSoftObjectPtr<class UTexture2D> OnlineIcon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Messenger|Style")
	TSoftObjectPtr<class UTexture2D> OfflineIcon;

	// The original messenger sprites live in T_windows. This is exposed so a themed UI can replace
	// the atlas without changing row logic.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Messenger|Style")
	TSoftObjectPtr<class UTexture2D> MessengerAtlas =
		TSoftObjectPtr<class UTexture2D>(FSoftObjectPath(UMT2PathSettings::Path(TEXT("UI_WindowsAtlas"))));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Messenger|Style")
	FVector2D LampSize = FVector2D(12.0, 12.0);

	// Row colours, matching messenger_list_online/offline.sub.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Messenger|Style")
	FLinearColor OnlineColor = FLinearColor(0.55f, 0.9f, 0.55f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Messenger|Style")
	FLinearColor OfflineColor = FLinearColor(0.55f, 0.55f, 0.58f);

	// The old list drew a blue bar behind the selected row (uimessenger.py OnRender).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Messenger|Style")
	FLinearColor SelectionColor = FLinearColor(0.0f, 0.0f, 0.7f, 0.7f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Messenger|Style")
	int32 RowFontSize = 10;

	// The old list grouped its rows under collapsible headers (uimessenger.py MessengerGroupItem) and
	// indented members by GetStepWidth() = 15.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Messenger|Style")
	float MemberIndent = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Messenger|Style")
	FLinearColor GroupHeaderColor = FLinearColor(0.75f, 0.85f, 1.0f);

	// The band at the top that drags the window, matching the old board's movable title bar.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Messenger|Layout")
	float TitleBarHeight = 30.0f;

	// Seconds within which a second click on the same row counts as a double click, which is what
	// opens the whisper. UMG buttons have no double-click event of their own.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Messenger|Layout")
	float DoubleClickSeconds = 0.35f;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	// messengerwindow.py's window style is ("movable", "float"): the board drags by its title bar.
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	// ---- bound from the Widget Blueprint ----
	// Sizes the window. Without it the widget fills whatever slot it is placed in.
	UPROPERTY(meta = (BindWidget)) TObjectPtr<USizeBox> RootSizeBox;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2TitleBarWidget> TitleBarWidget;

	// List side. Rows are generated into FriendsBox, so it must be a panel that accepts children
	// (a VerticalBox inside a ScrollBox is the closest match to the old scrollbar list).
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UPanelWidget> FriendsBox;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> AddFriendButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> WhisperButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> MobileButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> RemoveButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> GuildButton;


private:
	UFUNCTION() void HandleAddFriendClicked();
	UFUNCTION() void HandleWhisperButtonClicked();
	UFUNCTION() void HandleRemoveButtonClicked();
	UFUNCTION() void HandleGuildButtonClicked();
	UFUNCTION() void HandleMessengerChanged();
	UFUNCTION() void HandleActionResult(FName Action, EMT2MessengerResult Result);

	// Rows are rebuilt on every refresh, so each button finds its own row by hover rather than
	// capturing an id the widget system cannot serialise.
	UFUNCTION() void HandleFriendRowClicked();
	UFUNCTION() void HandleGroupHeaderClicked();
	UFUNCTION() void HandleTitleBarClose();
	UFUNCTION() void HandleAcceptRequestClicked();
	UFUNCTION() void HandleDeclineRequestClicked();

	UMT2MessengerComponent* ResolveMessenger() const;
	void BindMessenger();
	void BuildFriendRows();
	// One collapsible group: the header, then its members, or "Empty".
	void BuildGroup(int32 GroupIndex, const FString& Title, const TArray<FMT2FriendEntry>& Members);
	FString FindIdForButton(const TArray<TObjectPtr<UButton>>& Buttons, const TArray<FString>& Ids) const;
	void UpdateActionButtons();

	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> FriendButtons;
	UPROPERTY(Transient) TArray<FString> FriendButtonIds;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> AcceptButtons;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> DeclineButtons;
	UPROPERTY(Transient) TArray<FString> RequestButtonIds;

	// The old list selected on single click and whispered on double click; the bottom buttons act on
	// whatever is selected.
	UPROPERTY(Transient) FString SelectedFriendId;

	// Group headers, in the order they are built: Friends, Guild, Ignored.
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> GroupButtons;
	UPROPERTY(Transient) TArray<bool> GroupOpen = {true, true, true};

	// Window dragging, as in the whisper dialog: a viewport widget has no slot to read a position from.
	UPROPERTY(Transient) FVector2D WindowPosition = FVector2D::ZeroVector;
	UPROPERTY(Transient) bool bPositionInitialized = false;
	UPROPERTY(Transient) bool bDragging = false;
	FVector2D DragStartMouse = FVector2D::ZeroVector;
	FVector2D DragStartWindow = FVector2D::ZeroVector;

	// Double-click bookkeeping for the member rows.
	UPROPERTY(Transient) FString LastClickedFriendId;
	UPROPERTY(Transient) double LastClickTime = 0.0;

	UPROPERTY(Transient) TWeakObjectPtr<UMT2MessengerComponent> BoundMessenger;
};
