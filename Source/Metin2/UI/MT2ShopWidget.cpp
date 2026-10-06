/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2ShopWidget.h"
#include "Config/MT2PathSettings.h"
#include "Audio/MT2SoundPlaybackSubsystem.h"

#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

#include "Blueprint/WidgetTree.h"
#include "Characters/MT2PlayerCharacter.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Core/MT2VnumRegistrySubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "InputCoreTypes.h"
#include "Items/MT2ItemTemplate.h"
#include "Npcs/MT2Npc.h"
#include "Npcs/MT2NpcInteractionComponent.h"
#include "Npcs/MT2NpcShopComponent.h"
#include "Player/MT2PlayerState.h"
#include "UI/MT2AtlasImage.h"
#include "UI/MT2BoardWidget.h"
#include "UI/MT2ItemTooltipWidget.h"
#include "UI/MT2TitleBarWidget.h"
#include "UI/MT2UIStyle.h"

namespace
{
	const FMT2AtlasRegion ShopSlotBaseRegion(0.0f, 348.0f, 32.0f, 380.0f);
	constexpr float ShopCellSize = 34.0f;
	constexpr float ShopCellStride = 35.0f;
	const FLinearColor ModeSelectedColor(1.0f, 0.89f, 0.42f);
	const FLinearColor ModeIdleColor(0.7f, 0.7f, 0.7f);

	// Public.dds middle_button_01/02/03 - the old shop dialog's Buy/Sell buttons (61x21).
	const FMT2AtlasRegion MiddleButtonUp(194.0f, 142.0f, 255.0f, 163.0f);
	const FMT2AtlasRegion MiddleButtonOver(88.0f, 181.0f, 149.0f, 202.0f);
	const FMT2AtlasRegion MiddleButtonDown(149.0f, 181.0f, 210.0f, 202.0f);

	UButton* MakeTextButton(UWidgetTree& Tree, const FText& Label, TObjectPtr<UTextBlock>& OutLabel)
	{
		UTexture2D* PublicTexture = FMT2UIStyle::LoadTexture(UMT2PathSettings::Path(TEXT("UI_PublicAtlas")));
		UButton* Button = FMT2UIStyle::AtlasButton(
			Tree, PublicTexture, MiddleButtonUp, MiddleButtonOver, MiddleButtonDown);
		OutLabel = FMT2UIStyle::Label(Tree, Label, 10);
		Button->AddChild(OutLabel);
		if (UButtonSlot* LabelSlot = Cast<UButtonSlot>(OutLabel->Slot))
		{
			LabelSlot->SetHorizontalAlignment(HAlign_Center);
			LabelSlot->SetVerticalAlignment(VAlign_Center);
		}
		return Button;
	}
}

template <typename WidgetType>
WidgetType* UMT2ShopWidget::CreateSharedWidget(const TCHAR* BlueprintPath)
{
	UClass* WidgetClass = LoadClass<WidgetType>(nullptr, BlueprintPath);
	if (!WidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MT2Shop] Missing shared widget Blueprint %s"), BlueprintPath);
		return nullptr;
	}
	return WidgetTree->ConstructWidget<WidgetType>(WidgetClass);
}

TSharedRef<SWidget> UMT2ShopWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ShopRoot"));
		// Only the board (and its children) is interactive; empty screen area lets clicks fall
		// through to the inventory and other windows behind the shop.
		RootCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		WidgetTree->RootWidget = RootCanvas;

		UTexture2D* PublicTexture = FMT2UIStyle::LoadTexture(UMT2PathSettings::Path(TEXT("UI_PublicAtlas")));

		// The grid's real drawn width is (Columns-1) strides plus one full cell; the board adds an
		// even margin on both sides so the grid and the buttons sit centered.
		const float GridWidth = (Columns - 1) * ShopCellStride + ShopCellSize;
		const float GridHeight = (Rows - 1) * ShopCellStride + ShopCellSize;
		const float BoardWidth = GridWidth + 24.0f;
		const float GridLeft = (BoardWidth - GridWidth) * 0.5f;
		const float GridTop = TitleBarHeight + 12.0f;
		// Buy/Sell row below the grid; the board ends just under it.
		constexpr float ModeButtonWidth = 78.0f;
		constexpr float ModeButtonHeight = 21.0f;
		constexpr float ModeButtonGap = 10.0f;
		const float ButtonsY = GridTop + GridHeight + 8.0f;
		const float ButtonsLeft = (BoardWidth - (ModeButtonWidth * 2.0f + ModeButtonGap)) * 0.5f;
		const float BoardHeight = ButtonsY + ModeButtonHeight + 12.0f;

		// Same carved board + title bar the inventory and character windows use.
		UCanvasPanel* Inner = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ShopInner"));
		BoardSlot = RootCanvas->AddChildToCanvas(Inner);
		BoardSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		BoardSlot->SetAlignment(FVector2D(0.5, 0.5));
		BoardSlot->SetSize(FVector2D(BoardWidth, BoardHeight));

		BoardWidget = CreateSharedWidget<UMT2BoardWidget>(UMT2PathSettings::Path(TEXT("UI_MT2Board")));
		if (BoardWidget)
		{
			if (UCanvasPanelSlot* BoardBackSlot = FMT2UIStyle::Place(
				Inner, BoardWidget, FVector2D::ZeroVector, FVector2D(BoardWidth, BoardHeight)))
			{
				BoardBackSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
				BoardBackSlot->SetOffsets(FMargin(0.0f));
				BoardBackSlot->SetZOrder(-10);
			}
		}

		TitleBarWidget = CreateSharedWidget<UMT2TitleBarWidget>(UMT2PathSettings::Path(TEXT("UI_MT2TitleBar")));
		if (TitleBarWidget)
		{
			TitleBarWidget->InitializeTitleBar(BoardWidth - 16.0f, FText::FromString(TEXT("Shop")));
			TitleBarWidget->OnCloseClicked.AddUniqueDynamic(this, &UMT2ShopWidget::HandleCloseClicked);
			FMT2UIStyle::Place(Inner, TitleBarWidget, FVector2D(8.0, 6.0),
				FVector2D(BoardWidth - 16.0f, TitleBarHeight));
		}

		// Item grid.
		for (int32 Index = 0; Index < Columns * Rows; ++Index)
		{
			const int32 Column = Index % Columns;
			const int32 Row = Index / Columns;
			const FVector2D CellPos(GridLeft + Column * ShopCellStride, GridTop + Row * ShopCellStride);

			UMT2AtlasImage* Background = WidgetTree->ConstructWidget<UMT2AtlasImage>(UMT2AtlasImage::StaticClass());
			Background->SetAtlas(PublicTexture, FMT2AtlasRect(
				ShopSlotBaseRegion.Left, ShopSlotBaseRegion.Top,
				ShopSlotBaseRegion.Size().X, ShopSlotBaseRegion.Size().Y));
			// Never hit-testable: only the item icons carry hover/tooltips, so a tall item's icon is
			// hoverable over its whole height instead of just its top cell.
			Background->SetVisibility(ESlateVisibility::HitTestInvisible);
			FMT2UIStyle::Place(Inner, Background, CellPos, FVector2D(ShopCellSize, ShopCellSize));

			// Visible (hit-testable) so hovering the ICON - not the empty slot - shows the tooltip.
			// UImage doesn't handle mouse buttons, so clicks still bubble up to this widget.
			UImage* Icon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
			Icon->SetVisibility(ESlateVisibility::Hidden);
			if (UCanvasPanelSlot* IconSlot = FMT2UIStyle::Place(Inner, Icon,
				CellPos + FVector2D(1.0, 1.0), FVector2D(ShopCellSize - 2.0, ShopCellSize - 2.0)))
			{
				IconSlot->SetZOrder(10);
			}
			SlotIcons.Add(Icon);

			UTextBlock* Count = FMT2UIStyle::Label(*WidgetTree, FText::GetEmpty(), 8, ETextJustify::Right);
			Count->SetShadowOffset(FVector2D(1.0));
			if (UCanvasPanelSlot* CountSlot = FMT2UIStyle::Place(Inner, Count,
				CellPos + FVector2D(0.0, ShopCellSize - 12.0), FVector2D(ShopCellSize - 2.0, 10.0)))
			{
				CountSlot->SetZOrder(11);
			}
			SlotCounts.Add(Count);

		}

		// Buy / Sell mode buttons, centered under the grid.
		BuyModeButton = MakeTextButton(*WidgetTree, FText::FromString(TEXT("Buy")), BuyModeText);
		BuyModeButton->OnClicked.AddUniqueDynamic(this, &UMT2ShopWidget::HandleBuyModeClicked);
		FMT2UIStyle::Place(Inner, BuyModeButton, FVector2D(ButtonsLeft, ButtonsY),
			FVector2D(ModeButtonWidth, ModeButtonHeight));
		SellModeButton = MakeTextButton(*WidgetTree, FText::FromString(TEXT("Sell")), SellModeText);
		SellModeButton->OnClicked.AddUniqueDynamic(this, &UMT2ShopWidget::HandleSellModeClicked);
		FMT2UIStyle::Place(Inner, SellModeButton,
			FVector2D(ButtonsLeft + ModeButtonWidth + ModeButtonGap, ButtonsY),
			FVector2D(ModeButtonWidth, ModeButtonHeight));

		// Sell-confirmation popup (hidden until a sell is requested).
		SellConfirmPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SellConfirm"));
		SellConfirmPanel->SetBrushColor(FLinearColor(0.05f, 0.05f, 0.06f, 0.98f));
		SellConfirmPanel->SetPadding(FMargin(10.0f));
		SellConfirmPanel->SetVisibility(ESlateVisibility::Collapsed);
		if (UCanvasPanelSlot* ConfirmSlot = RootCanvas->AddChildToCanvas(SellConfirmPanel))
		{
			ConfirmSlot->SetAnchors(FAnchors(0.5f, 0.5f));
			ConfirmSlot->SetAlignment(FVector2D(0.5, 0.5));
			ConfirmSlot->SetAutoSize(true);
			ConfirmSlot->SetZOrder(100);
		}
		UCanvasPanel* ConfirmInner = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
		SellConfirmPanel->SetContent(ConfirmInner);
		SellConfirmText = FMT2UIStyle::Label(*WidgetTree, FText::GetEmpty(), 10, ETextJustify::Center);
		FMT2UIStyle::Place(ConfirmInner, SellConfirmText, FVector2D(0.0, 0.0), FVector2D(220.0, 34.0));
		TObjectPtr<UTextBlock> YesLabel, NoLabel;
		UButton* YesButton = MakeTextButton(*WidgetTree, FText::FromString(TEXT("Yes")), YesLabel);
		YesButton->OnClicked.AddUniqueDynamic(this, &UMT2ShopWidget::HandleSellConfirmYes);
		FMT2UIStyle::Place(ConfirmInner, YesButton, FVector2D(20.0, 40.0), FVector2D(80.0, 24.0));
		UButton* NoButton = MakeTextButton(*WidgetTree, FText::FromString(TEXT("No")), NoLabel);
		NoButton->OnClicked.AddUniqueDynamic(this, &UMT2ShopWidget::HandleSellConfirmNo);
		FMT2UIStyle::Place(ConfirmInner, NoButton, FVector2D(120.0, 40.0), FVector2D(80.0, 24.0));

		SetVisibility(ESlateVisibility::Collapsed);
		SetIsFocusable(true);
	}
	return Super::RebuildWidget();
}

void UMT2ShopWidget::OpenShop(AMT2Npc* Npc)
{
	TakeWidget();
	const bool bWasOpen = IsShopOpen();
	ShopNpc = Npc;
	ShopMode = EMT2ShopMode::Buy;
	SetSellConfirmVisible(false);
	RefreshModeButtons();
	RefreshEntries();
	// SelfHitTestInvisible, not Visible: this widget spans the whole viewport, so making it
	// hit-testable would swallow every click before it reached the inventory/HUD behind it. Only
	// its children (the board and its contents) take input; clicks elsewhere fall through.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (!bWasOpen)
	{
		if (AMT2PlayerCharacter* Player = Cast<AMT2PlayerCharacter>(GetOwningPlayerPawn()))
		{
			Player->SetInteractionUIOpen(true);
		}
	}
}

void UMT2ShopWidget::CloseShop()
{
	const bool bWasOpen = IsShopOpen();
	ShopNpc.Reset();
	SetSellConfirmVisible(false);
	SetVisibility(ESlateVisibility::Collapsed);
	if (bWasOpen)
	{
		if (AMT2PlayerCharacter* Player = Cast<AMT2PlayerCharacter>(GetOwningPlayerPawn()))
		{
			Player->SetInteractionUIOpen(false);
		}
	}
}

void UMT2ShopWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!IsShopOpen())
	{
		return;
	}
	const AMT2Npc* Npc = ShopNpc.Get();
	const APawn* Player = GetOwningPlayerPawn();
	if (!Npc || !Player)
	{
		CloseShop();
		return;
	}
	const float MaxRange = (Npc->GetInteractionComponent()
		? Npc->GetInteractionComponent()->GetInteractionRange() : 250.0f) * 2.0f;
	if (FVector::Dist2D(Npc->GetActorLocation(), Player->GetActorLocation()) > MaxRange)
	{
		CloseShop();
	}
}

void UMT2ShopWidget::RefreshModeButtons()
{
	if (BuyModeText)
	{
		BuyModeText->SetColorAndOpacity(FSlateColor(ShopMode == EMT2ShopMode::Buy ? ModeSelectedColor : ModeIdleColor));
	}
	if (SellModeText)
	{
		SellModeText->SetColorAndOpacity(FSlateColor(ShopMode == EMT2ShopMode::Sell ? ModeSelectedColor : ModeIdleColor));
	}
}

void UMT2ShopWidget::RefreshEntries()
{
	const AMT2Npc* Npc = ShopNpc.Get();
	const UMT2NpcShopComponent* Shop = Npc ? Npc->GetShopComponent() : nullptr;
	if (!Shop || SlotIcons.IsEmpty())
	{
		return;
	}
	if (TitleBarWidget)
	{
		TitleBarWidget->InitializeTitleBar(
			Columns * ShopCellStride + 8.0f, FText::FromString(Npc->GetMobDisplayName()));
	}

	UGameInstance* GameInstance = GetGameInstance();
	UMT2VnumRegistrySubsystem* Registry = GameInstance
		? GameInstance->GetSubsystem<UMT2VnumRegistrySubsystem>() : nullptr;
	const TArray<FMT2ShopEntry>& Entries = Shop->GetShopEntries();
	const AMT2PlayerState* Viewer = GetOwningPlayer()
		? GetOwningPlayer()->GetPlayerState<AMT2PlayerState>() : nullptr;
	const int32 CellCount = SlotIcons.Num();

	CellToEntry.Init(INDEX_NONE, CellCount);
	TArray<bool> Occupied;
	Occupied.Init(false, CellCount);
	for (int32 Cell = 0; Cell < CellCount; ++Cell)
	{
		SlotIcons[Cell]->SetVisibility(ESlateVisibility::Hidden);
		SlotIcons[Cell]->SetToolTip(nullptr);
		SlotCounts[Cell]->SetText(FText::GetEmpty());
		// Reset to a single cell; a taller item below would otherwise leave a stale hover area.
		if (UCanvasPanelSlot* IconSlot = Cast<UCanvasPanelSlot>(SlotIcons[Cell]->Slot))
		{
			IconSlot->SetSize(FVector2D(ShopCellSize - 2.0, ShopCellSize - 2.0));
		}
	}

	for (int32 EntryIndex = 0; EntryIndex < Entries.Num(); ++EntryIndex)
	{
		const FMT2ShopEntry& Entry = Entries[EntryIndex];
		const TSubclassOf<UMT2ItemTemplate> TemplateClass = Registry
			? Registry->ResolveItemTemplateClass(Entry.ItemVnum) : nullptr;
		const UMT2ItemTemplate* Template = TemplateClass.GetDefaultObject();
		const int32 SizeCells = Template ? FMath::Clamp(Template->InventorySize, 1, Rows) : 1;

		int32 TopCell = INDEX_NONE;
		for (int32 Cell = 0; Cell < CellCount; ++Cell)
		{
			if (Occupied[Cell] || Cell / Columns + SizeCells > Rows)
			{
				continue;
			}
			bool bFits = true;
			for (int32 Below = 1; Below < SizeCells; ++Below)
			{
				if (Occupied[Cell + Below * Columns]) { bFits = false; break; }
			}
			if (bFits) { TopCell = Cell; break; }
		}
		if (TopCell == INDEX_NONE)
		{
			break;
		}

		for (int32 Below = 0; Below < SizeCells; ++Below)
		{
			const int32 Cell = TopCell + Below * Columns;
			Occupied[Cell] = true;
			CellToEntry[Cell] = EntryIndex;
		}

		UTexture2D* Icon = Template ? Template->Icon.LoadSynchronous() : nullptr;
		SlotIcons[TopCell]->SetBrushFromTexture(Icon, true);
		// Hovering the icon itself (which spans the item's real height) shows the tooltip.
		SlotIcons[TopCell]->SetVisibility(Icon ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
		if (Icon && Template && Viewer)
		{
			SlotIcons[TopCell]->SetToolTip(UMT2ItemTooltipWidget::Create(
				GetOwningPlayer(), Template, Entry.Count, Viewer, Shop->GetEntryPrice(EntryIndex)));
		}
		if (UCanvasPanelSlot* IconSlot = Cast<UCanvasPanelSlot>(SlotIcons[TopCell]->Slot))
		{
			const float IconHeight = (ShopCellSize - 2.0f) + ShopCellStride * (SizeCells - 1);
			IconSlot->SetSize(FVector2D(ShopCellSize - 2.0, IconHeight));
		}
		SlotCounts[TopCell]->SetText(Entry.Count > 1 ? FText::AsNumber(Entry.Count) : FText::GetEmpty());
	}
}

int32 UMT2ShopWidget::FindHoveredEntry() const
{
	// Only the item icons are hit-testable; a multi-cell icon covers its whole item.
	for (int32 Cell = 0; Cell < SlotIcons.Num(); ++Cell)
	{
		if (SlotIcons[Cell] && SlotIcons[Cell]->IsHovered() && CellToEntry.IsValidIndex(Cell))
		{
			return CellToEntry[Cell];
		}
	}
	return INDEX_NONE;
}

void UMT2ShopWidget::RequestSellFromInventory(int32 InventorySlot, int32 Vnum, int32 Count)
{
	if (!IsShopOpen() || ShopMode != EMT2ShopMode::Sell)
	{
		return;
	}
	UGameInstance* GameInstance = GetGameInstance();
	UMT2VnumRegistrySubsystem* Registry = GameInstance
		? GameInstance->GetSubsystem<UMT2VnumRegistrySubsystem>() : nullptr;
	const TSubclassOf<UMT2ItemTemplate> TemplateClass = Registry
		? Registry->ResolveItemTemplateClass(Vnum) : nullptr;
	const UMT2ItemTemplate* Template = TemplateClass.GetDefaultObject();
	const FString Name = Template ? (Template->DisplayName.IsEmpty()
		? Template->InternalName : Template->DisplayName.ToString()) : FString::FromInt(Vnum);
	// Same formula the server pays out with, so the confirmation never lies.
	const int64 Payout = UMT2NpcShopComponent::CalculateSellPayout(Template, Count);

	PendingSellSlot = InventorySlot;
	PendingSellCount = FMath::Max(Count, 1);
	if (SellConfirmText)
	{
		SellConfirmText->SetText(FText::FromString(
			FString::Printf(TEXT("Sell %s for %lld Yang?"), *Name, Payout)));
	}
	SetSellConfirmVisible(true);
}

void UMT2ShopWidget::SetSellConfirmVisible(bool bVisible)
{
	if (SellConfirmPanel)
	{
		SellConfirmPanel->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (!bVisible)
	{
		PendingSellSlot = INDEX_NONE;
		PendingSellCount = 0;
	}
}

void UMT2ShopWidget::HandleSellConfirmYes()
{
	AMT2Npc* Npc = ShopNpc.Get();
	AMT2PlayerCharacter* Player = Cast<AMT2PlayerCharacter>(GetOwningPlayerPawn());
	if (Npc && Player && PendingSellSlot != INDEX_NONE)
	{
		Player->ServerShopSell(Npc, PendingSellSlot, PendingSellCount);
		PlayTransactionSound();
	}
	SetSellConfirmVisible(false);
}

void UMT2ShopWidget::HandleSellConfirmNo()
{
	SetSellConfirmVisible(false);
}

void UMT2ShopWidget::PlayTransactionSound()
{
	// uishop.py plays sound/ui/money.wav on both buy and sell.
	if (USoundBase* Sound = TransactionSound.LoadSynchronous())
	{
		UMT2SoundPlaybackSubsystem::PlayExclusive2D(this, Sound);
	}
}

void UMT2ShopWidget::HandleBuyModeClicked()
{
	ShopMode = EMT2ShopMode::Buy;
	SetSellConfirmVisible(false);
	RefreshModeButtons();
}

void UMT2ShopWidget::HandleSellModeClicked()
{
	ShopMode = EMT2ShopMode::Sell;
	RefreshModeButtons();
}

void UMT2ShopWidget::HandleCloseClicked()
{
	CloseShop();
}

FReply UMT2ShopWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// Right-click buys the hovered item, but only in Buy mode (Sell mode acts on the inventory).
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton && ShopMode == EMT2ShopMode::Buy)
	{
		AMT2Npc* Npc = ShopNpc.Get();
		AMT2PlayerCharacter* Player = Cast<AMT2PlayerCharacter>(GetOwningPlayerPawn());
		const int32 Entry = FindHoveredEntry();
		if (Npc && Player && Entry != INDEX_NONE)
		{
			Player->ServerShopBuy(Npc, Entry, 1);
			PlayTransactionSound();
			return FReply::Handled();
		}
	}

	// Left-click on the title bar starts dragging the window.
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && BoardSlot)
	{
		const FVector2D BoardLocal = BoardSlot->GetPosition();
		// Board is centered; convert the mouse to viewport space and test the title band.
		const FVector2D Local = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		const FVector2D BoardTopLeft = InGeometry.GetLocalSize() * 0.5f + BoardLocal - BoardSlot->GetSize() * 0.5f;
		const FVector2D InBoard = Local - BoardTopLeft;
		if (InBoard.X >= 0 && InBoard.X <= BoardSlot->GetSize().X && InBoard.Y >= 0 && InBoard.Y <= TitleBarHeight)
		{
			bDragging = true;
			DragStartMouse = InMouseEvent.GetScreenSpacePosition();
			DragStartBoardPos = BoardSlot->GetPosition();
			return FReply::Handled().CaptureMouse(TakeWidget());
		}
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UMT2ShopWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDragging && HasMouseCapture() && BoardSlot)
	{
		const float Scale = FMath::Max(InGeometry.Scale, UE_SMALL_NUMBER);
		BoardSlot->SetPosition(DragStartBoardPos +
			(InMouseEvent.GetScreenSpacePosition() - DragStartMouse) / Scale);
		return FReply::Handled();
	}
	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

FReply UMT2ShopWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDragging && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDragging = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

FReply UMT2ShopWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape && IsShopOpen())
	{
		if (SellConfirmPanel && SellConfirmPanel->GetVisibility() == ESlateVisibility::Visible)
		{
			SetSellConfirmVisible(false);
		}
		else
		{
			CloseShop();
		}
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}
