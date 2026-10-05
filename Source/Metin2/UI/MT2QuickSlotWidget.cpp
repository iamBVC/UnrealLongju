/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2QuickSlotWidget.h"

#include "Blueprint/DragDropOperation.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "InputCoreTypes.h"
#include "Skills/MT2SkillDefinition.h"
#include "Skills/MT2SkillTypes.h"
#include "UI/MT2SkillSlotWidget.h"
#include "UI/MT2SlotEffectWidget.h"
#include "UI/MT2WidgetUtils.h"

void UMT2QuickSlotWidget::InitializeQuickSlot(int32 InSlotIndex, const FText& InHotkeyLabel, UTexture2D* InSlotTexture)
{
	SlotIndex = InSlotIndex;
	HotkeyLabel = InHotkeyLabel;
	SlotTexture = InSlotTexture;
	if (HotkeyText) HotkeyText->SetText(HotkeyLabel);
	if (SlotBackground && SlotTexture)
	{
		FSlateBrush Brush;
		Brush.SetResourceObject(SlotTexture);
		Brush.ImageSize = FVector2D(32.0f);
		Brush.SetUVRegion(FBox2f(FVector2f(0.0f, 348.0f / SlotTexture->GetSizeY()), FVector2f(32.0f / SlotTexture->GetSizeX(), 380.0f / SlotTexture->GetSizeY())));
		SlotBackground->SetBrush(Brush);
	}
}

void UMT2QuickSlotWidget::NativeConstruct()
{
	Super::NativeConstruct();
	HotkeyText->SetText(HotkeyLabel);
}

void UMT2QuickSlotWidget::Activate()
{
	OnActivated.Broadcast(SlotIndex);
}

void UMT2QuickSlotWidget::SetIcon(UTexture2D* InIcon)
{
	if (IconImage)
	{
		IconImage->SetBrushFromTexture(InIcon, true);
		IconImage->SetVisibility(InIcon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
}

void UMT2QuickSlotWidget::EnsureSkillSlot()
{
	if (SkillSlot || !IconImage || !WidgetTree)
	{
		return;
	}
	SkillSlot = WidgetTree->ConstructWidget<UMT2SkillSlotWidget>(
		UMT2SkillSlotWidget::StaticClass(), TEXT("QuickSkillSlot"));
	// Display-only: this widget keeps owning the clicks (use, carry, clear), the cell only draws
	// and raises its tooltip.
	SkillSlot->SetDisplayOnly(true);
	if (!MT2WidgetUtils::PlaceOver(IconImage, SkillSlot))
	{
		SkillSlot = nullptr;
	}
}

void UMT2QuickSlotWidget::SetSkillBinding(const UMT2SkillDefinition* Definition, int32 SkillLevel)
{
	SkillBindingVnum = Definition ? Definition->Vnum : 0;
	if (!Definition)
	{
		if (SkillSlot)
		{
			SkillSlot->SetEmpty();
			SkillSlot->SetVisibility(ESlateVisibility::Collapsed);
		}
		return;
	}

	EnsureSkillSlot();
	if (!SkillSlot)
	{
		return;
	}
	// The bar shows the grade the skill actually reached, i.e. the same lit cell as the skill window.
	const int32 Grade = FMath::Min(static_cast<int32>(MT2SkillMastery::FromLevel(SkillLevel)), 2);
	SkillSlot->SetSkill(Definition, Grade);
	SkillSlot->SetLevel(SkillLevel, true);
	SkillSlot->SetVisibility(ESlateVisibility::Visible);
	// The cell draws the icon now; the Blueprint's plain image would double it up.
	if (IconImage)
	{
		IconImage->SetVisibility(ESlateVisibility::Hidden);
	}
}

void UMT2QuickSlotWidget::SetIconBrush(const FSlateBrush& Brush)
{
	if (IconImage)
	{
		IconImage->SetBrush(Brush);
		IconImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void UMT2QuickSlotWidget::ClearIcon()
{
	if (IconImage)
	{
		IconImage->SetVisibility(ESlateVisibility::Hidden);
	}
	SetStackCount(0);
	SetSkillBinding(nullptr, 0);
}

void UMT2QuickSlotWidget::SetStackCount(int32 InCount)
{
	if (CountText)
	{
		CountText->SetText(InCount > 1 ? FText::AsNumber(InCount) : FText::GetEmpty());
	}
}

void UMT2QuickSlotWidget::SetItemActive(bool bActive)
{
	if (!ItemActiveEffect && IconImage && WidgetTree)
	{
		ItemActiveEffect = WidgetTree->ConstructWidget<UMT2SlotEffectWidget>(
			UMT2SlotEffectWidget::StaticClass(), TEXT("ActiveItemEffect"));
		if (!MT2WidgetUtils::PlaceOver(IconImage, ItemActiveEffect))
		{
			ItemActiveEffect = nullptr;
		}
	}
	if (ItemActiveEffect)
	{
		ItemActiveEffect->SetEffectVisible(bActive);
	}
}

FReply UMT2QuickSlotWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		Activate();
		return UWidgetBlueprintLibrary::DetectDragIfPressed(InMouseEvent, this, EKeys::LeftMouseButton).NativeReply;
	}
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		OnRightClicked.Broadcast(SlotIndex);
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

void UMT2QuickSlotWidget::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation)
{
	UDragDropOperation* Operation = NewObject<UDragDropOperation>();
	Operation->Payload = this;
	Operation->Pivot = EDragPivot::MouseDown;
	OutOperation = Operation;
}

bool UMT2QuickSlotWidget::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	if (const UMT2QuickSlotWidget* Source = InOperation ? Cast<UMT2QuickSlotWidget>(InOperation->Payload) : nullptr)
	{
		OnSlotDropped.Broadcast(Source->GetSlotIndex(), SlotIndex);
		return true;
	}
	return false;
}
