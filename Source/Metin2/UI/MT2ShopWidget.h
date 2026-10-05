/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "UI/MT2UserWidget.h"
#include "MT2ShopWidget.generated.h"

class AMT2Npc;
class UBorder;
class UButton;
class USoundBase;
class UCanvasPanel;
class UCanvasPanelSlot;
class UMT2AtlasImage;
class UImage;
class UTextBlock;
class UVerticalBox;

UENUM()
enum class EMT2ShopMode : uint8
{
	Buy,
	Sell
};

// The old shop window: a draggable board of the NPC's stock. A Buy/Sell mode toggle drives what
// right-click does - in Buy mode right-clicking a shop item buys it; in Sell mode right-clicking
// an inventory item sells it (after a confirmation popup). Built natively.
UCLASS()
class METIN2_API UMT2ShopWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	void OpenShop(AMT2Npc* Npc);
	void CloseShop();

	// Open = anything but Collapsed (shown as SelfHitTestInvisible so the fullscreen root doesn't
	// eat input meant for the windows behind it).
	bool IsShopOpen() const { return GetVisibility() != ESlateVisibility::Collapsed; }
	EMT2ShopMode GetShopMode() const { return ShopMode; }

	// Inventory right-click while in Sell mode routes here: shows the sell-confirmation popup.
	void RequestSellFromInventory(int32 InventorySlot, int32 Vnum, int32 Count);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	// The old client's buy/sell "money" sound (uishop.py OnSellItem / OnBuyItem).
	UPROPERTY(EditDefaultsOnly, Category = "UI|Sound")
	TSoftObjectPtr<USoundBase> TransactionSound =
		TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/sound/ui/money.money")));

	void PlayTransactionSound();

	UFUNCTION() void HandleCloseClicked();
	// Loads a Widget Blueprint class by path (the board/title bar assets shared with the inventory).
	template <typename WidgetType>
	WidgetType* CreateSharedWidget(const TCHAR* BlueprintPath);

	UFUNCTION() void HandleBuyModeClicked();
	UFUNCTION() void HandleSellModeClicked();
	UFUNCTION() void HandleSellConfirmYes();
	UFUNCTION() void HandleSellConfirmNo();
	void RefreshEntries();
	void RefreshModeButtons();
	int32 FindHoveredEntry() const;
	void SetSellConfirmVisible(bool bVisible);

	static constexpr int32 Columns = 5;
	static constexpr int32 Rows = 8;

	TWeakObjectPtr<AMT2Npc> ShopNpc;
	EMT2ShopMode ShopMode = EMT2ShopMode::Buy;

	// Pending sell awaiting confirmation.
	int32 PendingSellSlot = INDEX_NONE;
	int32 PendingSellCount = 0;

	// Title-bar drag state.
	bool bDragging = false;
	FVector2D DragStartMouse = FVector2D::ZeroVector;
	FVector2D DragStartBoardPos = FVector2D::ZeroVector;

	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> RootCanvas;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanelSlot> BoardSlot;
	// Same board + title bar widgets the inventory/character windows use.
	UPROPERTY(Transient) TObjectPtr<class UMT2BoardWidget> BoardWidget;
	UPROPERTY(Transient) TObjectPtr<class UMT2TitleBarWidget> TitleBarWidget;
	UPROPERTY(Transient) TObjectPtr<UButton> BuyModeButton;
	UPROPERTY(Transient) TObjectPtr<UButton> SellModeButton;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BuyModeText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SellModeText;
	UPROPERTY(Transient) TObjectPtr<UBorder> SellConfirmPanel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SellConfirmText;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> SlotIcons;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SlotCounts;

	TArray<int32> CellToEntry;

	static constexpr float TitleBarHeight = 22.0f;
};
