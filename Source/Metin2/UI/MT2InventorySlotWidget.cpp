/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2InventorySlotWidget.h"

#include "Blueprint/DragDropOperation.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Input/Events.h"
#include "Items/MT2ItemTemplate.h"
#include "Items/MT2ItemUtils.h"
#include "Characters/MT2PlayerCharacter.h"
#include "Items/MT2InventoryComponent.h"
#include "Player/MT2PlayerState.h"
#include "UI/MT2AtlasImage.h"
#include "UI/MT2ItemTooltipWidget.h"
#include "UI/MT2SlotEffectWidget.h"
#include "UI/MT2WidgetUtils.h"

namespace
{
	// Builds the hover tooltip text from the item template - name plus the stats that matter for the
	// item's type, matching what the old client's item tooltip surfaces.
	FString BuildItemTooltip(const UMT2ItemTemplate* Template, int32 Count)
	{
		if (!Template)
		{
			return FString();
		}
		FString Text = Template->DisplayName.IsEmpty() ? Template->InternalName : Template->DisplayName.ToString();
		if (Count > 1)
		{
			Text += FString::Printf(TEXT(" x%d"), Count);
		}

		if (const UMT2ItemWeaponTemplate* Weapon = Cast<UMT2ItemWeaponTemplate>(Template))
		{
			Text += FString::Printf(TEXT("\nAttack %d-%d"),
				Weapon->GetPhysicalDamageMin(), Weapon->GetPhysicalDamageMax());
		}
		else if (const UMT2ItemArmorTemplate* Armor = Cast<UMT2ItemArmorTemplate>(Template))
		{
			Text += FString::Printf(TEXT("\nDefense %d"),
				Armor->Defense + Armor->RefinementDefense * 2);
		}

		static const TCHAR* ApplyNames[] = {
			TEXT(""), TEXT("Max HP"), TEXT("Max SP"), TEXT("VIT"), TEXT("INT"), TEXT("STR"), TEXT("DEX"),
			TEXT("Attack Speed"), TEXT("Movement Speed")
		};
		for (const FMT2ItemApply& Apply : Template->Applies)
		{
			const int32 ApplyType = static_cast<int32>(Apply.Type);
			if (ApplyType > 0 && ApplyType < UE_ARRAY_COUNT(ApplyNames))
			{
				Text += FString::Printf(TEXT("\n%s +%d"), ApplyNames[ApplyType], Apply.Value);
			}
		}
		return Text;
	}
}

void UMT2InventorySlotWidget::InitializeSlot(int32 InSlotIndex, EMT2InventorySlotKind InSlotKind, const FText& InLabel)
{
	SlotIndex = InSlotIndex;
	InteractionSlotIndex = InSlotIndex;
	SlotKind = InSlotKind;
	Label = InLabel;
	RefreshVisuals();
	EnsureSlotEffects();
}

void UMT2InventorySlotWidget::EnsureSlotEffects()
{
	if (!WidgetTree || !IconImage)
	{
		return;
	}
	if (!ActiveEffect)
	{
		ActiveEffect = WidgetTree->ConstructWidget<UMT2SlotEffectWidget>(
			UMT2SlotEffectWidget::StaticClass(), TEXT("ActiveItemEffect"));
		if (!MT2WidgetUtils::PlaceOver(IconImage, ActiveEffect)) ActiveEffect = nullptr;
		else ActiveEffect->SetEffectVisible(false);
	}
	if (!NewItemEffect)
	{
		NewItemEffect = WidgetTree->ConstructWidget<UMT2SlotEffectWidget>(
			UMT2SlotEffectWidget::StaticClass(), TEXT("NewItemEffect"));
		if (!MT2WidgetUtils::PlaceOver(IconImage, NewItemEffect)) NewItemEffect = nullptr;
		else NewItemEffect->SetEffectVisible(false);
	}
}

void UMT2InventorySlotWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// UUserWidget defaults to SelfHitTestInvisible, which leaves hover/click detection to whichever
	// children happen to be hit-testable (in practice just the small text blocks - the "only a tiny
	// region reacts" bug). Making the widget itself Visible puts the ENTIRE cell in the hit-test grid,
	// so tooltips, clicks, and drags work anywhere on the slot regardless of its children.
	SetVisibility(ESlateVisibility::Visible);
	if (HitButton)
	{
		HitButton->SetVisibility(ESlateVisibility::Visible);
	}
	RefreshVisuals();
}

void UMT2InventorySlotWidget::SetItemVisual(UTexture2D* InIcon, int32 InCount, FName InItemId)
{
	ItemId = InItemId;
	if (IconImage)
	{
		IconImage->SetRenderTransformPivot(FVector2D::ZeroVector);
		IconImage->SetRenderScale(FVector2D(1.0f, 1.0f));
		IconImage->SetBrushFromTexture(InIcon, true);
		IconImage->SetVisibility(InIcon ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
	}
	if (CountText)
	{
		CountText->SetText(InCount > 1 ? FText::AsNumber(InCount) : FText::GetEmpty());
	}
}

void UMT2InventorySlotWidget::SetItemByVnum(
	int32 Vnum, int32 Count, const TArray<FMT2ItemBonus>& Bonuses,
	const TArray<FMT2MetinSocket>& MetinSockets, int32 SkillVnum,
	int32 AutoRecoveryRemainingAmount, bool bAutoRecoveryActive, bool bNewlyAcquired)
{
	EnsureSlotEffects();
	InteractionSlotIndex = SlotIndex;
	SetCovered(false);
	if (Vnum <= 0 || Count <= 0)
	{
		SetItemVisual(nullptr, 0, NAME_None);
		SetToolTipText(FText::GetEmpty());
		bCurrentItemNew = false;
		if (ActiveEffect) ActiveEffect->SetEffectVisible(false);
		if (NewItemEffect) NewItemEffect->SetEffectVisible(false);
		return;
	}

	const UMT2ItemTemplate* Template = MT2ItemUtils::ResolveTemplate(this, Vnum);
	UTexture2D* Icon = Template ? Template->Icon.LoadSynchronous() : nullptr;
	SetItemVisual(Icon, Count, FName(*FString::FromInt(Vnum)));
	bCurrentItemNew = bNewlyAcquired;
	if (ActiveEffect) ActiveEffect->SetEffectVisible(bAutoRecoveryActive);
	if (NewItemEffect) NewItemEffect->SetEffectVisible(
		bNewlyAcquired, FLinearColor(1.0f, 0.82f, 0.25f, 1.0f));
	if (IconImage && SlotKind == EMT2InventorySlotKind::Item && Template)
	{
		// Inventory cells are fixed at 32x32. Render-transform the icon from that cell to its original
		// 1/2/3-cell height; equipment slots already have their full authored dimensions.
		const float ItemHeight = static_cast<float>(FMath::Clamp(Template->InventorySize, 1, 3));
		IconImage->SetRenderScale(FVector2D(1.0f, ItemHeight));
		for (UMT2SlotEffectWidget* Effect : {ActiveEffect.Get(), NewItemEffect.Get()})
		{
			if (Effect)
			{
				Effect->SetRenderTransformPivot(FVector2D::ZeroVector);
				Effect->SetRenderScale(FVector2D(1.0f, ItemHeight));
			}
		}
	}
	// Rich per-type tooltip widget (weapon/armor/accessory/use styles) instead of plain text.
	if (SlotKind != EMT2InventorySlotKind::Quick)
	{
		const AMT2PlayerState* Viewer = GetOwningPlayer()
			? GetOwningPlayer()->GetPlayerState<AMT2PlayerState>() : nullptr;
		SetToolTip(UMT2ItemTooltipWidget::Create(
			GetOwningPlayer(), Template, Count, Viewer, -1, &Bonuses, &MetinSockets,
			SkillVnum, AutoRecoveryRemainingAmount));
	}
}

void UMT2InventorySlotWidget::SetCoveredByItem(
	int32 OwningSlotIndex, int32 Vnum, int32 Count, const TArray<FMT2ItemBonus>& Bonuses,
	const TArray<FMT2MetinSocket>& MetinSockets, int32 SkillVnum,
	int32 AutoRecoveryRemainingAmount, bool bAutoRecoveryActive, bool bNewlyAcquired)
{
	SetItemByVnum(Vnum, Count, Bonuses, MetinSockets, SkillVnum,
		AutoRecoveryRemainingAmount, bAutoRecoveryActive, bNewlyAcquired);
	InteractionSlotIndex = OwningSlotIndex;
	SetCovered(true);
}

void UMT2InventorySlotWidget::NativeOnMouseEnter(
	const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
	if (!bCurrentItemNew || SlotKind != EMT2InventorySlotKind::Item)
	{
		return;
	}
	bCurrentItemNew = false;
	if (NewItemEffect) NewItemEffect->SetEffectVisible(false);
	if (AMT2PlayerCharacter* Character = Cast<AMT2PlayerCharacter>(GetOwningPlayerPawn()))
	{
		if (UMT2InventoryComponent* Inventory = Character->GetInventoryComponent())
		{
			Inventory->ServerMarkItemSeen(InteractionSlotIndex);
		}
	}
}

void UMT2InventorySlotWidget::SetCovered(bool bInCovered)
{
	// Covered cells remain visually intact. MT2InventoryGrid raises the owning tall item's canvas
	// Z-order, so its icon paints over these normal slot backgrounds without corrupting the atlas brush.
	if (bInCovered)
	{
		if (IconImage) IconImage->SetVisibility(ESlateVisibility::Hidden);
		if (ActiveEffect) ActiveEffect->SetEffectVisible(false);
		if (NewItemEffect) NewItemEffect->SetEffectVisible(false);
		if (SlotBackground)
		{
			SlotBackground->SetVisibility(ESlateVisibility::Visible);
			SlotBackground->SetRenderOpacity(1.0f);
		}
		if (CountText) CountText->SetText(FText::GetEmpty());
	}
	else if (SlotBackground)
	{
		SlotBackground->SetVisibility(ESlateVisibility::Visible);
		SlotBackground->SetRenderOpacity(SlotKind == EMT2InventorySlotKind::Equipment ? 0.0f : 1.0f);
	}
}

FReply UMT2InventorySlotWidget::NativeOnPreviewMouseButtonDown(
	const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		OnSlotClicked.Broadcast(InteractionSlotIndex, SlotKind);
		// Metin2 uses click-to-carry, not a captured UMG drag. Capturing a drag here suppresses
		// hover events on every slot crossed by the cursor, so tooltips disappear while moving items.
		return FReply::Handled();
	}
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		OnSlotRightClicked.Broadcast(InteractionSlotIndex, SlotKind);
		return FReply::Handled();
	}

	return Super::NativeOnPreviewMouseButtonDown(InGeometry, InMouseEvent);
}

void UMT2InventorySlotWidget::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation)
{
	if (ItemId == NAME_None)
	{
		return; // nothing to drag from an empty slot
	}
	OnSlotDragStarted.Broadcast(InteractionSlotIndex, SlotKind);

	UDragDropOperation* Operation = NewObject<UDragDropOperation>();
	Operation->Payload = this;
	Operation->Pivot = EDragPivot::MouseDown;

	// A copy of the icon follows the cursor while dragging.
	if (IconImage)
	{
		UImage* DragIcon = NewObject<UImage>(this);
		DragIcon->SetBrush(IconImage->GetBrush());
		Operation->DefaultDragVisual = DragIcon;
	}
	OutOperation = Operation;
}

bool UMT2InventorySlotWidget::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	if (const UMT2InventorySlotWidget* SourceSlot = InOperation ? Cast<UMT2InventorySlotWidget>(InOperation->Payload) : nullptr)
	{
		OnSlotDropped.Broadcast(
			SourceSlot->GetSlotIndex(), SourceSlot->GetSlotKind(), InteractionSlotIndex, SlotKind);
		return true;
	}

	return false;
}

void UMT2InventorySlotWidget::NativeOnDragCancelled(
	const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	Super::NativeOnDragCancelled(InDragDropEvent, InOperation);
	if (ItemId != NAME_None)
	{
		OnSlotDragCancelled.Broadcast(
			InteractionSlotIndex, SlotKind, InDragDropEvent.GetScreenSpacePosition());
	}
}

void UMT2InventorySlotWidget::RefreshVisuals()
{
	if (LabelText)
	{
		LabelText->SetText(Label);
	}
	if (SlotBackground)
	{
		SlotBackground->SetVisibility(ESlateVisibility::Visible);
		SlotBackground->SetRenderOpacity(SlotKind == EMT2InventorySlotKind::Equipment ? 0.0f : 1.0f);
	}
}
