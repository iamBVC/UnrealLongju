/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2GameHUDWidget.h"

#include "UI/MT2CharacterWindowWidget.h"
#include "UI/MT2InventoryWidget.h"
#include "UI/MT2TaskbarWidget.h"
#include "UI/MT2MinimapWidget.h"
#include "UI/MT2FullMapWidget.h"
#include "UI/MT2FriendAddDialogWidget.h"
#include "UI/MT2MessengerWidget.h"
#include "UI/MT2TargetInfoWidget.h"
#include "UI/MT2WhisperWidget.h"
#include "UI/MT2SystemMenuWidget.h"
#include "UI/MT2NotificationsWidget.h"
#include "UI/MT2StatusEffectBarWidget.h"
#include "UI/MT2PartyPanelWidget.h"
#include "UI/MT2FriendRequestDialogWidget.h"
#include "UI/MT2PartyInviteDialogWidget.h"
#include "UI/MT2LoadingScreenWidget.h"
#include "UI/MT2TradeWidget.h"
#include "UI/MT2GuildWidget.h"
#include "UI/MT2GuildInviteDialogWidget.h"
#include "UI/MT2HUD.h"
#include "GameFramework/PlayerController.h"

void UMT2GameHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();
	TaskbarWidget->OnInventoryClicked.AddUniqueDynamic(this, &UMT2GameHUDWidget::HandleInventoryRequested);
	// The taskbar broadcast this and nobody listened, so the button did nothing.
	TaskbarWidget->OnMessengerClicked.AddUniqueDynamic(this, &UMT2GameHUDWidget::HandleMessengerRequested);
	TaskbarWidget->OnCharacterClicked.AddUniqueDynamic(this, &UMT2GameHUDWidget::HandleCharacterRequested);
	TaskbarWidget->OnSystemClicked.AddUniqueDynamic(this, &UMT2GameHUDWidget::HandleSystemRequested);
	MinimapWidget->OnFullMapRequested.AddUniqueDynamic(this, &UMT2GameHUDWidget::HandleFullMapRequested);
	if (MessengerWidget) MessengerWidget->SetVisibility(ESlateVisibility::Collapsed);
	if (FriendAddDialogWidget) FriendAddDialogWidget->SetVisibility(ESlateVisibility::Collapsed);
	if (WhisperWidget) WhisperWidget->SetVisibility(ESlateVisibility::Collapsed);
	if (SystemMenuWidget) SystemMenuWidget->SetVisibility(ESlateVisibility::Collapsed);
	if (NotificationsWidget) NotificationsWidget->SetVisibility(ESlateVisibility::Collapsed);
	if (FriendRequestDialogWidget) FriendRequestDialogWidget->SetVisibility(ESlateVisibility::Collapsed);
	if (PartyInviteDialogWidget) PartyInviteDialogWidget->SetVisibility(ESlateVisibility::Collapsed);
	if (LoadingScreenWidget) LoadingScreenWidget->SetVisibility(ESlateVisibility::Collapsed);
	if (GuildWidget) GuildWidget->SetVisibility(ESlateVisibility::Collapsed);
	if (GuildInviteDialogWidget) GuildInviteDialogWidget->SetVisibility(ESlateVisibility::Collapsed);
}

void UMT2GameHUDWidget::HandleSystemRequested()
{
	const APlayerController* Controller = GetOwningPlayer();
	if (AMT2HUD* HUD = Controller ? Cast<AMT2HUD>(Controller->GetHUD()) : nullptr)
	{
		HUD->ToggleSystemMenu();
	}
}

void UMT2GameHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (TargetInfoWidget) TargetInfoWidget->RefreshCombatBinding();
}

void UMT2GameHUDWidget::ToggleInventory() { InventoryWidget->ToggleInventory(); }
void UMT2GameHUDWidget::ActivateQuickSlot(int32 SlotIndex) { TaskbarWidget->ActivateQuickSlot(SlotIndex); }
void UMT2GameHUDWidget::ToggleCharacterWindow(EMT2CharacterWindowPage Page) { CharacterWindowWidget->ToggleWindow(Page); }
void UMT2GameHUDWidget::ToggleFullMap() { FullMapWidget->ToggleWindow(); }
void UMT2GameHUDWidget::HandleInventoryRequested() { ToggleInventory(); }
void UMT2GameHUDWidget::HandleMessengerRequested()
{
	// The messenger window belongs to the HUD actor, not to this widget.
	if (AMT2HUD* HUD = GetOwningPlayer() ? GetOwningPlayer()->GetHUD<AMT2HUD>() : nullptr)
	{
		HUD->ToggleMessengerWindow();
	}
}
void UMT2GameHUDWidget::HandleCharacterRequested() { ToggleCharacterWindow(EMT2CharacterWindowPage::Stats); }
void UMT2GameHUDWidget::HandleFullMapRequested() { ToggleFullMap(); }
