/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "UI/MT2UserWidget.h"
#include "CoreMinimal.h"
#include "MT2GameHUDWidget.generated.h"

class UMT2InventoryWidget;
class UMT2CharacterWindowWidget;
class UMT2TaskbarWidget;
class UMT2TargetInfoWidget;
class UMT2MinimapWidget;
class UMT2FullMapWidget;
class UMT2MessengerWidget;
class UMT2FriendAddDialogWidget;
class UMT2WhisperWidget;
class UMT2SystemMenuWidget;
class UMT2NotificationsWidget;
class UMT2StatusEffectBarWidget;
class UMT2PartyPanelWidget;
class UMT2FriendRequestDialogWidget;
class UMT2PartyInviteDialogWidget;
class UMT2LoadingScreenWidget;
class UMT2TradeWidget;
class UMT2GuildWidget;
class UMT2GuildInviteDialogWidget;
enum class EMT2CharacterWindowPage : uint8;

UCLASS()
class METIN2_API UMT2GameHUDWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "HUD") void ToggleInventory();
	UMT2InventoryWidget* GetInventoryWidget() const { return InventoryWidget; }
	UFUNCTION(BlueprintCallable, Category = "HUD") void ActivateQuickSlot(int32 SlotIndex);
	void ToggleCharacterWindow(EMT2CharacterWindowPage Page);
	void ToggleFullMap();
	UMT2MessengerWidget* GetMessengerWidget() const { return MessengerWidget; }
	UMT2FriendAddDialogWidget* GetFriendAddDialogWidget() const { return FriendAddDialogWidget; }
	UMT2WhisperWidget* GetWhisperWidget() const { return WhisperWidget; }
	UMT2SystemMenuWidget* GetSystemMenuWidget() const { return SystemMenuWidget; }
	UMT2NotificationsWidget* GetNotificationsWidget() const { return NotificationsWidget; }
	UMT2StatusEffectBarWidget* GetStatusEffectBarWidget() const { return StatusEffectBarWidget; }
	UMT2PartyPanelWidget* GetPartyPanelWidget() const { return PartyPanelWidget; }
	UMT2FriendRequestDialogWidget* GetFriendRequestDialogWidget() const { return FriendRequestDialogWidget; }
	UMT2PartyInviteDialogWidget* GetPartyInviteDialogWidget() const { return PartyInviteDialogWidget; }
	UMT2LoadingScreenWidget* GetLoadingScreenWidget() const { return LoadingScreenWidget; }
	UMT2TradeWidget* GetTradeWidget() const { return TradeWidget; }
	UMT2GuildWidget* GetGuildWidget() const { return GuildWidget; }
	UMT2GuildInviteDialogWidget* GetGuildInviteDialogWidget() const { return GuildInviteDialogWidget; }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UFUNCTION() void HandleInventoryRequested();
	UFUNCTION() void HandleMessengerRequested();
	UFUNCTION() void HandleSystemRequested();
	UFUNCTION() void HandleCharacterRequested();
	UFUNCTION() void HandleFullMapRequested();
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2TaskbarWidget> TaskbarWidget;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2InventoryWidget> InventoryWidget;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2CharacterWindowWidget> CharacterWindowWidget;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2TargetInfoWidget> TargetInfoWidget;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2MinimapWidget> MinimapWidget;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2FullMapWidget> FullMapWidget;
	// These are existing child Widget Blueprints placed in MT2GameHUD by the UI author. Keeping them
	// here avoids code-created floating widgets and gives the HUD Blueprint full layout control.
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2MessengerWidget> MessengerWidget;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2FriendAddDialogWidget> FriendAddDialogWidget;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2WhisperWidget> WhisperWidget;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2SystemMenuWidget> SystemMenuWidget;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2NotificationsWidget> NotificationsWidget;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2StatusEffectBarWidget> StatusEffectBarWidget;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2PartyPanelWidget> PartyPanelWidget;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2FriendRequestDialogWidget> FriendRequestDialogWidget;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2PartyInviteDialogWidget> PartyInviteDialogWidget;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2LoadingScreenWidget> LoadingScreenWidget;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2TradeWidget> TradeWidget;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2GuildWidget> GuildWidget;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UMT2GuildInviteDialogWidget> GuildInviteDialogWidget;
};
