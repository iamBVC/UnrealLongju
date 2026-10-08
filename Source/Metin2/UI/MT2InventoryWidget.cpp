/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2InventoryWidget.h"
#include "Config/MT2PathSettings.h"
#include "Audio/MT2SoundPlaybackSubsystem.h"

#include "Characters/MT2PlayerCharacter.h"
#include "Components/Button.h"
#include "Core/MT2VnumRegistrySubsystem.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Items/MT2InventoryComponent.h"
#include "Items/MT2ItemBonusSettings.h"
#include "Items/MT2ItemTemplate.h"
#include "Items/MT2ItemUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Player/MT2PlayerState.h"
#include "Sound/SoundBase.h"
#include "Engine/LocalPlayer.h"
#include "UI/MT2CurrencyPanelWidget.h"
#include "UI/MT2CursorCarrySubsystem.h"
#include "UI/MT2ShopWidget.h"
#include "UI/MT2UIStyle.h"
#include "UI/MT2EquipmentPanelWidget.h"
#include "UI/MT2InventoryGridWidget.h"
#include "UI/MT2ItemDropDialogWidget.h"
#include "UI/MT2TitleBarWidget.h"

UMT2InventoryWidget::UMT2InventoryWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	auto Sound = [](const TCHAR* Path)
	{
		return TSoftObjectPtr<USoundBase>(FSoftObjectPath(Path));
	};
	PickupItemSound = Sound(UMT2PathSettings::Path(TEXT("sound_ui_pickup_item_in_inventory")));
	DefaultItemSound = Sound(UMT2PathSettings::Path(TEXT("sound_ui_drop")));
	ArmorItemSound = Sound(UMT2PathSettings::Path(TEXT("sound_ui_equip_metal_armor")));
	WeaponItemSound = Sound(UMT2PathSettings::Path(TEXT("sound_ui_equip_metal_weapon")));
	BowItemSound = Sound(UMT2PathSettings::Path(TEXT("sound_ui_equip_bow")));
	AccessoryItemSound = Sound(UMT2PathSettings::Path(TEXT("sound_ui_equip_ring_amulet")));
	PotionItemSound = Sound(UMT2PathSettings::Path(TEXT("sound_ui_eat_potion")));
	PortalItemSound = Sound(UMT2PathSettings::Path(TEXT("sound_ui_potal_scroll")));
}

void UMT2InventoryWidget::ToggleInventory()
{
	const bool bShow = GetVisibility() != ESlateVisibility::Visible;
	SetVisibility(bShow ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (bShow)
	{
		// The pawn/inventory may not have existed at construct time; (re)bind and refresh on open.
		RefreshPageSlots();
	}
}

void UMT2InventoryWidget::OpenInventory()
{
	if (GetVisibility() != ESlateVisibility::Visible)
	{
		SetVisibility(ESlateVisibility::Visible);
		RefreshPageSlots();
	}
}

void UMT2InventoryWidget::SetInventoryPage(int32 PageIndex)
{
	ActivePage = FMath::Clamp(PageIndex, 0, UMT2InventoryComponent::PageCount - 1);
	RefreshPageSlots();
	OnInventoryPageChanged.Broadcast(ActivePage);
}

void UMT2InventoryWidget::NativeConstruct()
{
	Super::NativeConstruct();
	TitleBarWidget->InitializeTitleBar(161.0f, FText::FromString(TEXT("Inventory")));
	TitleBarWidget->OnCloseClicked.AddUniqueDynamic(this, &UMT2InventoryWidget::HandleCloseClicked);
	EquipmentPanelWidget->OnSlotClicked.AddUniqueDynamic(this, &UMT2InventoryWidget::HandleSlotClicked);
	EquipmentPanelWidget->OnSlotRightClicked.AddUniqueDynamic(this, &UMT2InventoryWidget::HandleEquipSlotRightClicked);
	EquipmentPanelWidget->OnSlotDragStarted.AddUniqueDynamic(this, &UMT2InventoryWidget::HandleSlotDragStarted);
	EquipmentPanelWidget->OnSlotDropped.AddUniqueDynamic(this, &UMT2InventoryWidget::HandleSlotDropped);
	InventoryGridWidget->InitializeGrid(5, 9, 0);
	InventoryGridWidget->OnSlotClicked.AddUniqueDynamic(this, &UMT2InventoryWidget::HandleSlotClicked);
	InventoryGridWidget->OnSlotRightClicked.AddUniqueDynamic(this, &UMT2InventoryWidget::HandleGridSlotRightClicked);
	InventoryGridWidget->OnSlotDragStarted.AddUniqueDynamic(this, &UMT2InventoryWidget::HandleSlotDragStarted);
	InventoryGridWidget->OnSlotDropped.AddUniqueDynamic(this, &UMT2InventoryWidget::HandleSlotDropped);
	PageButtons = {InventoryPageOneButton, InventoryPageTwoButton, InventoryPageThreeButton, InventoryPageFourButton};
	InventoryPageOneButton->OnClicked.AddUniqueDynamic(this, &UMT2InventoryWidget::HandlePageOneClicked);
	InventoryPageTwoButton->OnClicked.AddUniqueDynamic(this, &UMT2InventoryWidget::HandlePageTwoClicked);
	InventoryPageThreeButton->OnClicked.AddUniqueDynamic(this, &UMT2InventoryWidget::HandlePageThreeClicked);
	InventoryPageFourButton->OnClicked.AddUniqueDynamic(this, &UMT2InventoryWidget::HandlePageFourClicked);
	RefreshPageSlots();
	if (AMT2PlayerCharacter* Player = GetOwningMT2Character())
	{
		Player->OnInventoryActionConfirmed.AddUniqueDynamic(
			this, &UMT2InventoryWidget::HandleInventoryActionConfirmed);
	}
	SetVisibility(ESlateVisibility::Collapsed);
}

void UMT2InventoryWidget::NativeDestruct()
{
	if (AMT2PlayerState* State = BoundPlayerState.Get())
	{
		State->OnYangChanged.RemoveDynamic(this, &UMT2InventoryWidget::HandleYangChanged);
	}
	BoundPlayerState.Reset();
	Super::NativeDestruct();
}

void UMT2InventoryWidget::RefreshPageSlots()
{
	InventoryGridWidget->SetStartIndex(ActivePage * 45);
	// Old client tab_button_small_01/02/03 (Windows.dds): the selected page shows the "down" art.
	static const FMT2AtlasRegion TabUp(324.0f, 227.0f, 356.0f, 246.0f);
	static const FMT2AtlasRegion TabOver(356.0f, 227.0f, 388.0f, 246.0f);
	static const FMT2AtlasRegion TabDown(388.0f, 227.0f, 420.0f, 246.0f);
	UTexture2D* WindowsTexture = FMT2UIStyle::LoadTexture(UMT2PathSettings::Path(TEXT("UI_WindowsAtlas")));
	for (int32 Index = 0; Index < PageButtons.Num(); ++Index)
	{
		const bool bSelected = Index == ActivePage;
		FButtonStyle Style = PageButtons[Index]->GetStyle();
		Style.SetNormal(FMT2UIStyle::AtlasBrush(WindowsTexture, bSelected ? TabDown : TabUp));
		Style.SetHovered(FMT2UIStyle::AtlasBrush(WindowsTexture, bSelected ? TabDown : TabOver));
		Style.SetPressed(FMT2UIStyle::AtlasBrush(WindowsTexture, TabDown));
		PageButtons[Index]->SetStyle(Style);
		PageButtons[Index]->SetBackgroundColor(FLinearColor::White);
	}

	if (UMT2InventoryComponent* Inventory = ResolveInventoryComponent())
	{
		InventoryGridWidget->RefreshSlots(Inventory->GetSlots(), ActivePage * UMT2InventoryComponent::PageSlotCount);
	}
	RefreshEquipment();
	RefreshCurrency();
}

void UMT2InventoryWidget::RefreshEquipment()
{
	if (UMT2InventoryComponent* Inventory = ResolveInventoryComponent())
	{
		EquipmentPanelWidget->RefreshEquipment(Inventory->GetEquipment());
	}
}

void UMT2InventoryWidget::RefreshCurrency()
{
	APlayerController* PlayerController = GetOwningPlayer();
	AMT2PlayerState* State = PlayerController
		? PlayerController->GetPlayerState<AMT2PlayerState>() : nullptr;
	if (BoundPlayerState.Get() != State)
	{
		if (AMT2PlayerState* OldState = BoundPlayerState.Get())
		{
			OldState->OnYangChanged.RemoveDynamic(this, &UMT2InventoryWidget::HandleYangChanged);
		}
		BoundPlayerState = State;
		if (State)
		{
			State->OnYangChanged.AddUniqueDynamic(this, &UMT2InventoryWidget::HandleYangChanged);
		}
	}
	CurrencyPanelWidget->SetYang(State ? State->GetYang() : 0);
}

UMT2InventoryComponent* UMT2InventoryWidget::ResolveInventoryComponent()
{
	UMT2InventoryComponent* Inventory = BoundInventory.Get();
	if (!Inventory)
	{
		if (AMT2PlayerCharacter* Player = GetOwningMT2Character())
		{
			Inventory = Player->GetInventoryComponent();
			Player->OnInventoryActionConfirmed.AddUniqueDynamic(
				this, &UMT2InventoryWidget::HandleInventoryActionConfirmed);
		}
		if (Inventory && !BoundInventory.IsValid())
		{
			BoundInventory = Inventory;
			Inventory->OnInventoryChanged.AddUniqueDynamic(this, &UMT2InventoryWidget::HandleInventoryChanged);
			Inventory->OnEquipmentChanged.AddUniqueDynamic(this, &UMT2InventoryWidget::HandleEquipmentChanged);
		}
	}
	return Inventory;
}

AMT2PlayerCharacter* UMT2InventoryWidget::GetOwningMT2Character() const
{
	return Cast<AMT2PlayerCharacter>(GetOwningPlayerPawn());
}

void UMT2InventoryWidget::HandleInventoryChanged()
{
	if (UMT2InventoryComponent* Inventory = ResolveInventoryComponent())
	{
		InventoryGridWidget->RefreshSlots(Inventory->GetSlots(), ActivePage * UMT2InventoryComponent::PageSlotCount);
	}
}

void UMT2InventoryWidget::HandleEquipmentChanged()
{
	RefreshEquipment();
}

void UMT2InventoryWidget::HandleYangChanged(int64 OldYang, int64 NewYang)
{
	(void)OldYang;
	CurrencyPanelWidget->SetYang(NewYang);
}

void UMT2InventoryWidget::HandleCloseClicked() { SetVisibility(ESlateVisibility::Collapsed); }
void UMT2InventoryWidget::HandlePageOneClicked() { SetInventoryPage(0); }
void UMT2InventoryWidget::HandlePageTwoClicked() { SetInventoryPage(1); }
void UMT2InventoryWidget::HandlePageThreeClicked() { SetInventoryPage(2); }
void UMT2InventoryWidget::HandlePageFourClicked() { SetInventoryPage(3); }
void UMT2InventoryWidget::HandleSlotClicked(int32 SlotIndex, EMT2InventorySlotKind SlotKind)
{
	OnSlotClicked.Broadcast(SlotIndex, SlotKind);

	// Old-client item moving: first left click picks the item onto the cursor, the next left click
	// places it (move within the grid, or equip when dropped on an equipment cell).
	UMT2CursorCarrySubsystem* Carry = GetOwningLocalPlayer()
		? GetOwningLocalPlayer()->GetSubsystem<UMT2CursorCarrySubsystem>() : nullptr;
	AMT2PlayerCharacter* Player = GetOwningMT2Character();
	UMT2InventoryComponent* Inventory = ResolveInventoryComponent();
	if (!Carry || !Player || !Inventory)
	{
		return;
	}

	if (Carry->IsCarrying())
	{
		// A binding lifted off the quickbar dropped into the inventory just unbinds it - the item
		// itself never left the inventory.
		if (Carry->GetSourceQuickSlot() != INDEX_NONE)
		{
			if (AMT2PlayerState* State = GetOwningPlayer()
				? GetOwningPlayer()->GetPlayerState<AMT2PlayerState>() : nullptr)
			{
				State->ServerSetQuickSlot(Carry->GetSourceQuickSlot(), EMT2QuickSlotType::None, 0);
			}
			Carry->EndCarry();
			return;
		}
		switch (Carry->GetCarryKind())
		{
		case EMT2CarryKind::InventoryItem:
			if (SlotKind == EMT2InventorySlotKind::Item)
			{
				if (Carry->GetCarryPayload() != SlotIndex)
				{
					const int32 SourceSlot = Carry->GetCarryPayload();
					const int32 SourceVnum = Inventory->GetSlots().IsValidIndex(SourceSlot)
						? Inventory->GetSlots()[SourceSlot].Vnum : 0;
					const UMT2ItemBonusSettings* BonusSettings = GetDefault<UMT2ItemBonusSettings>();
					const bool bBonusConsumable = BonusSettings &&
						(SourceVnum == BonusSettings->AddNormalBonusItemVnum ||
						 SourceVnum == BonusSettings->ChangeNormalBonusesItemVnum ||
						 SourceVnum == BonusSettings->AddFifthBonusItemVnum ||
						 SourceVnum == BonusSettings->AddRareBonusItemVnum ||
						 SourceVnum == BonusSettings->ChangeRareBonusesItemVnum);
					const bool bOccupiedTarget = Inventory->GetSlots().IsValidIndex(SlotIndex) &&
						!Inventory->GetSlots()[SlotIndex].IsEmpty();
					const bool bMetinStone =
						Cast<UMT2ItemMetinStoneTemplate>(
							ResolveItemTemplateByVnum(SourceVnum)) != nullptr;
					const bool bRefinementScroll =
						Cast<UMT2ItemRefinementScrollTemplate>(
							ResolveItemTemplateByVnum(SourceVnum)) != nullptr;
					const UMT2ItemLootCrateTemplate* LootCrate = bOccupiedTarget
						? Cast<UMT2ItemLootCrateTemplate>(ResolveItemTemplate(SlotIndex, SlotKind))
						: nullptr;
					if (bRefinementScroll && bOccupiedTarget)
					{
						Player->OpenRefinementDialogLocal(
							SlotIndex, SourceSlot, nullptr);
					}
					else if (bMetinStone && bOccupiedTarget)
					{
						OpenMetinAttachDialog(SourceSlot, SlotIndex, false);
					}
					else if (LootCrate && LootCrate->RequiredKeyTemplate)
					{
						Player->ServerOpenInventoryLootCrate(SlotIndex, SourceSlot);
					}
					else if (bBonusConsumable && bOccupiedTarget)
					{
						Player->ServerApplyInventoryBonusItem(SourceSlot, SlotIndex, false);
					}
					else
					{
						Player->ServerMoveInventoryItem(SourceSlot, SlotIndex);
					}
				}
			}
			else if (SlotKind == EMT2InventorySlotKind::Equipment)
			{
				const int32 SourceSlot = Carry->GetCarryPayload();
				const int32 SourceVnum = Inventory->GetSlots().IsValidIndex(SourceSlot)
					? Inventory->GetSlots()[SourceSlot].Vnum : 0;
				const UMT2ItemBonusSettings* BonusSettings = GetDefault<UMT2ItemBonusSettings>();
				const bool bBonusConsumable = BonusSettings &&
					(SourceVnum == BonusSettings->AddNormalBonusItemVnum ||
					 SourceVnum == BonusSettings->ChangeNormalBonusesItemVnum ||
					 SourceVnum == BonusSettings->AddFifthBonusItemVnum ||
					 SourceVnum == BonusSettings->AddRareBonusItemVnum ||
					 SourceVnum == BonusSettings->ChangeRareBonusesItemVnum);
				const bool bMetinStone =
					Cast<UMT2ItemMetinStoneTemplate>(
						ResolveItemTemplateByVnum(SourceVnum)) != nullptr;
				if (bMetinStone)
				{
					OpenMetinAttachDialog(SourceSlot, SlotIndex, true);
				}
				else if (bBonusConsumable)
				{
					Player->ServerApplyInventoryBonusItem(SourceSlot, SlotIndex, true);
				}
				else
				{
					Player->ServerEquipInventoryItem(SourceSlot);
				}
			}
			break;
		case EMT2CarryKind::EquippedItem:
			if (SlotKind == EMT2InventorySlotKind::Item)
			{
				Player->ServerUnequipItem(Carry->GetCarryPayload());
			}
			break;
		default:
			break;
		}
		Carry->EndCarry();
		return;
	}

	// Pick up whatever sits in the clicked cell.
	const TArray<FMT2ItemSlot>& Source = SlotKind == EMT2InventorySlotKind::Equipment
		? Inventory->GetEquipment() : Inventory->GetSlots();
	if (!Source.IsValidIndex(SlotIndex) || Source[SlotIndex].IsEmpty())
	{
		return;
	}
	const UMT2ItemTemplate* Template = ResolveItemTemplateByVnum(Source[SlotIndex].Vnum);
	UTexture2D* Icon = Template ? Template->Icon.LoadSynchronous() : nullptr;
	const FSlateBrush CarryBrush = Icon ? FMT2UIStyle::TextureBrush(Icon) : FSlateBrush();
	Carry->BeginCarry(
		SlotKind == EMT2InventorySlotKind::Equipment
			? EMT2CarryKind::EquippedItem : EMT2CarryKind::InventoryItem,
		Source[SlotIndex].Vnum, SlotIndex, CarryBrush);
	PlaySoundAsset(PickupItemSound);
}

void UMT2InventoryWidget::HandleGridSlotRightClicked(int32 SlotIndex, EMT2InventorySlotKind SlotKind)
{
	if (AMT2PlayerCharacter* Player = GetOwningMT2Character())
	{
		// While a shop is open in Sell mode, right-click sells (with confirmation) instead of
		// equipping/using.
		if (UMT2ShopWidget* Shop = Player->GetShopWidget();
			Shop && Shop->IsShopOpen() && Shop->GetShopMode() == EMT2ShopMode::Sell &&
			SlotKind == EMT2InventorySlotKind::Item)
		{
			if (UMT2InventoryComponent* Inventory = ResolveInventoryComponent();
				Inventory && Inventory->GetSlots().IsValidIndex(SlotIndex) &&
				!Inventory->GetSlots()[SlotIndex].IsEmpty())
			{
				const FMT2ItemSlot& ItemSlot = Inventory->GetSlots()[SlotIndex];
				Shop->RequestSellFromInventory(SlotIndex, ItemSlot.Vnum, ItemSlot.Count);
			}
			return;
		}

		const UMT2ItemTemplate* Template = ResolveItemTemplate(SlotIndex, SlotKind);
		if (Cast<UMT2ItemLootCrateTemplate>(Template))
		{
			Player->ServerOpenInventoryLootCrate(SlotIndex, INDEX_NONE);
		}
		else if (Cast<UMT2ItemEquipmentTemplate>(Template) || Cast<UMT2ItemRodTemplate>(Template))
		{
			Player->ServerEquipInventoryItem(SlotIndex);
		}
		else if (Cast<UMT2ItemSkillBookTemplate>(Template))
		{
			// A skill book targets its own skill (book Value0): right-click reads it directly.
			Player->ServerReadSkillBook(SlotIndex);
		}
		else if (Cast<UMT2ItemUseTemplate>(Template))
		{
			Player->ServerUseInventoryItem(SlotIndex);
		}
	}
}

void UMT2InventoryWidget::HandleEquipSlotRightClicked(int32 WearPosition, EMT2InventorySlotKind SlotKind)
{
	// Right-clicking an equipped item takes it off, back into the inventory.
	if (AMT2PlayerCharacter* Player = GetOwningMT2Character())
	{
		Player->ServerUnequipItem(WearPosition);
	}
}

void UMT2InventoryWidget::HandleSlotDropped(int32 SourceSlotIndex, EMT2InventorySlotKind SourceKind, int32 TargetSlotIndex, EMT2InventorySlotKind TargetKind)
{
	OnSlotDropped.Broadcast(SourceSlotIndex, SourceKind, TargetSlotIndex, TargetKind);

	AMT2PlayerCharacter* Player = GetOwningMT2Character();
	if (!Player)
	{
		return;
	}
	if (SourceKind == EMT2InventorySlotKind::Item && TargetKind == EMT2InventorySlotKind::Item)
	{
		const UMT2ItemBonusSettings* BonusSettings = GetDefault<UMT2ItemBonusSettings>();
		const UMT2InventoryComponent* Inventory = ResolveInventoryComponent();
		const int32 SourceVnum = Inventory && Inventory->GetSlots().IsValidIndex(SourceSlotIndex)
			? Inventory->GetSlots()[SourceSlotIndex].Vnum : 0;
		const bool bBonusConsumable = BonusSettings &&
			(SourceVnum == BonusSettings->AddNormalBonusItemVnum ||
			 SourceVnum == BonusSettings->ChangeNormalBonusesItemVnum ||
			 SourceVnum == BonusSettings->AddFifthBonusItemVnum ||
			 SourceVnum == BonusSettings->AddRareBonusItemVnum ||
			 SourceVnum == BonusSettings->ChangeRareBonusesItemVnum);
		const bool bOccupiedTarget = Inventory &&
			Inventory->GetSlots().IsValidIndex(TargetSlotIndex) &&
			!Inventory->GetSlots()[TargetSlotIndex].IsEmpty();
		const bool bMetinStone =
			Cast<UMT2ItemMetinStoneTemplate>(
				ResolveItemTemplateByVnum(SourceVnum)) != nullptr;
		const bool bRefinementScroll =
			Cast<UMT2ItemRefinementScrollTemplate>(
				ResolveItemTemplateByVnum(SourceVnum)) != nullptr;
		const UMT2ItemLootCrateTemplate* LootCrate = bOccupiedTarget
			? Cast<UMT2ItemLootCrateTemplate>(
				ResolveItemTemplate(TargetSlotIndex, EMT2InventorySlotKind::Item))
			: nullptr;
		if (bRefinementScroll && bOccupiedTarget)
		{
			Player->OpenRefinementDialogLocal(
				TargetSlotIndex, SourceSlotIndex, nullptr);
		}
		else if (bMetinStone && bOccupiedTarget)
		{
			OpenMetinAttachDialog(SourceSlotIndex, TargetSlotIndex, false);
		}
		else if (LootCrate && LootCrate->RequiredKeyTemplate)
		{
			Player->ServerOpenInventoryLootCrate(TargetSlotIndex, SourceSlotIndex);
		}
		else if (bBonusConsumable && bOccupiedTarget)
		{
			Player->ServerApplyInventoryBonusItem(SourceSlotIndex, TargetSlotIndex, false);
		}
		else
		{
			Player->ServerMoveInventoryItem(SourceSlotIndex, TargetSlotIndex);
		}
	}
	else if (SourceKind == EMT2InventorySlotKind::Item && TargetKind == EMT2InventorySlotKind::Equipment)
	{
		const UMT2ItemBonusSettings* BonusSettings = GetDefault<UMT2ItemBonusSettings>();
		const UMT2InventoryComponent* Inventory = ResolveInventoryComponent();
		const int32 SourceVnum = Inventory && Inventory->GetSlots().IsValidIndex(SourceSlotIndex)
			? Inventory->GetSlots()[SourceSlotIndex].Vnum : 0;
		const bool bBonusConsumable = BonusSettings &&
			(SourceVnum == BonusSettings->AddNormalBonusItemVnum ||
			 SourceVnum == BonusSettings->ChangeNormalBonusesItemVnum ||
			 SourceVnum == BonusSettings->AddFifthBonusItemVnum ||
			 SourceVnum == BonusSettings->AddRareBonusItemVnum ||
			 SourceVnum == BonusSettings->ChangeRareBonusesItemVnum);
		const bool bMetinStone =
			Cast<UMT2ItemMetinStoneTemplate>(
				ResolveItemTemplateByVnum(SourceVnum)) != nullptr;
		if (bMetinStone)
		{
			OpenMetinAttachDialog(SourceSlotIndex, TargetSlotIndex, true);
		}
		else if (bBonusConsumable)
		{
			Player->ServerApplyInventoryBonusItem(SourceSlotIndex, TargetSlotIndex, true);
		}
		else
		{
			Player->ServerEquipInventoryItem(SourceSlotIndex);
		}
	}
	else if (SourceKind == EMT2InventorySlotKind::Equipment && TargetKind == EMT2InventorySlotKind::Item)
	{
		Player->ServerUnequipItem(SourceSlotIndex); // SourceSlotIndex is the equipment slot's wear position
	}
}

void UMT2InventoryWidget::HandleSlotDragStarted(int32 SlotIndex, EMT2InventorySlotKind SlotKind)
{
	if (ResolveItemTemplate(SlotIndex, SlotKind))
	{
		PlaySoundAsset(PickupItemSound);
	}
}

void UMT2InventoryWidget::OpenDropDialogForSlot(int32 SlotIndex)
{
	UMT2InventoryComponent* Inventory = ResolveInventoryComponent();
	APlayerController* PlayerController = GetOwningPlayer();
	if (!Inventory || !PlayerController || !Inventory->GetSlots().IsValidIndex(SlotIndex))
	{
		return;
	}
	const FMT2ItemSlot& ItemSlotData = Inventory->GetSlots()[SlotIndex];
	if (ItemSlotData.IsEmpty())
	{
		return;
	}

	if (!DropDialogWidget)
	{
		UClass* DialogClass = LoadClass<UMT2ItemDropDialogWidget>(
			nullptr, UMT2PathSettings::Path(TEXT("UI_MT2ItemDropDialog")));
		if (!DialogClass)
		{
			UE_LOG(LogTemp, Error, TEXT("Required UI asset /Game/UI/MT2ItemDropDialog is missing or invalid."));
			return;
		}
		DropDialogWidget = CreateWidget<UMT2ItemDropDialogWidget>(PlayerController, DialogClass);
	}
	if (!DropDialogWidget->IsInViewport())
	{
		DropDialogWidget->AddToViewport(100);
	}
	DropDialogWidget->OpenDropDialog(SlotIndex, ItemSlotData.Vnum, ItemSlotData.Count);
}

void UMT2InventoryWidget::OpenMetinAttachDialog(
	int32 StoneSlot, int32 TargetSlot, bool bTargetEquipped)
{
	UMT2InventoryComponent* Inventory = ResolveInventoryComponent();
	APlayerController* PlayerController = GetOwningPlayer();
	if (!Inventory || !PlayerController)
	{
		return;
	}
	const TArray<FMT2ItemSlot>& Targets = bTargetEquipped
		? Inventory->GetEquipment() : Inventory->GetSlots();
	if (!Inventory->GetSlots().IsValidIndex(StoneSlot) || !Targets.IsValidIndex(TargetSlot))
	{
		return;
	}
	const UMT2ItemTemplate* Stone =
		ResolveItemTemplateByVnum(Inventory->GetSlots()[StoneSlot].Vnum);
	const UMT2ItemTemplate* Target =
		ResolveItemTemplateByVnum(Targets[TargetSlot].Vnum);
	if (!Cast<UMT2ItemMetinStoneTemplate>(Stone) || !Target)
	{
		return;
	}

	if (!DropDialogWidget)
	{
		UClass* DialogClass = LoadClass<UMT2ItemDropDialogWidget>(
			nullptr, UMT2PathSettings::Path(TEXT("UI_MT2ItemDropDialog")));
		if (!DialogClass)
		{
			UE_LOG(LogTemp, Error,
				TEXT("Required UI asset /Game/UI/MT2ItemDropDialog is missing or invalid."));
			return;
		}
		DropDialogWidget = CreateWidget<UMT2ItemDropDialogWidget>(
			PlayerController, DialogClass);
	}
	if (!DropDialogWidget->IsInViewport())
	{
		DropDialogWidget->AddToViewport(100);
	}
	const auto DisplayName = [](const UMT2ItemTemplate* Template)
	{
		return Template->DisplayName.IsEmpty()
			? FText::FromString(Template->InternalName) : Template->DisplayName;
	};
	DropDialogWidget->OpenMetinAttachDialog(
		StoneSlot, TargetSlot, bTargetEquipped,
		DisplayName(Stone), DisplayName(Target));
}

const UMT2ItemTemplate* UMT2InventoryWidget::ResolveItemTemplate(
	int32 SlotIndex, EMT2InventorySlotKind SlotKind)
{
	const UMT2InventoryComponent* Inventory = ResolveInventoryComponent();
	if (!Inventory)
	{
		return nullptr;
	}
	const TArray<FMT2ItemSlot>& Source = SlotKind == EMT2InventorySlotKind::Equipment
		? Inventory->GetEquipment() : Inventory->GetSlots();
	return Source.IsValidIndex(SlotIndex) ? ResolveItemTemplateByVnum(Source[SlotIndex].Vnum) : nullptr;
}

const UMT2ItemTemplate* UMT2InventoryWidget::ResolveItemTemplateByVnum(int32 Vnum) const
{
	return MT2ItemUtils::ResolveTemplate(this, Vnum);
}

void UMT2InventoryWidget::HandleInventoryActionConfirmed(int32 Vnum, bool bUseSound)
{
	PlayItemSound(ResolveItemTemplateByVnum(Vnum), bUseSound);
}

void UMT2InventoryWidget::PlayItemSound(const UMT2ItemTemplate* Template, bool bUseSound) const
{
	// A vnum with no registered template still makes the generic drop sound. It used to bail out
	// here and go silent, which is the same condition that makes the world item render as the
	// fallback loot bag - so exactly the items that fell back visually were also the mute ones.
	// Every Cast below is null-safe, and DefaultItemSound is already the fallback for any type
	// that isn't specifically recognised.
	const TSoftObjectPtr<USoundBase>* SelectedSound = &DefaultItemSound;
	if (const UMT2ItemWeaponTemplate* Weapon = Cast<UMT2ItemWeaponTemplate>(Template))
	{
		// Old EWeaponSubTypes: bow=2, arrow=6. Arrows use the generic sound.
		SelectedSound = Weapon->WeaponSubType == 2 ? &BowItemSound
			: Weapon->WeaponSubType == 6 ? &DefaultItemSound : &WeaponItemSound;
	}
	else if (const UMT2ItemArmorTemplate* Armor = Cast<UMT2ItemArmorTemplate>(Template))
	{
		// Old EArmorSubTypes: body=0, neck=5, ear=6.
		SelectedSound = Armor->ArmorSubType == 0 ? &ArmorItemSound
			: (Armor->ArmorSubType == 5 || Armor->ArmorSubType == 6)
				? &AccessoryItemSound : &DefaultItemSound;
	}
	else if (bUseSound)
	{
		if (const UMT2ItemUseTemplate* UseItem = Cast<UMT2ItemUseTemplate>(Template))
		{
			// Potion, ability-up, no-delay potion and continuous potion all use the potion cue.
			if (UseItem->UseSubType == 0 || UseItem->UseSubType == 7 ||
				UseItem->UseSubType == 11 || UseItem->UseSubType == 16)
			{
				SelectedSound = &PotionItemSound;
			}
			else if (UseItem->UseSubType == 1)
			{
				SelectedSound = &PortalItemSound;
			}
		}
	}
	PlaySoundAsset(*SelectedSound);
}

void UMT2InventoryWidget::PlaySoundAsset(const TSoftObjectPtr<USoundBase>& Sound) const
{
	if (USoundBase* LoadedSound = Sound.LoadSynchronous())
	{
		UMT2SoundPlaybackSubsystem::PlayExclusive2D(GetWorld(), LoadedSound);
	}
}
