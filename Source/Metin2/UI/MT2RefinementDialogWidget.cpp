/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2RefinementDialogWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Characters/MT2PlayerCharacter.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/MT2VnumRegistrySubsystem.h"
#include "Engine/GameInstance.h"
#include "InputCoreTypes.h"
#include "Items/MT2InventoryComponent.h"
#include "Items/MT2ItemTemplate.h"
#include "Npcs/MT2Npc.h"
#include "Player/MT2PlayerState.h"
#include "UI/MT2ItemTooltipWidget.h"
#include "UI/MT2UIStyle.h"

namespace
{
	const FLinearColor RefinementBodyColor(0.82f, 0.82f, 0.78f);
	const FLinearColor RefinementGoodColor(0.55f, 0.85f, 0.55f);
	const FLinearColor RefinementBadColor(0.95f, 0.45f, 0.40f);

	UTextBlock* AddLabel(
		UWidgetTree& Tree, UVerticalBox* Parent, const FText& Text,
		const FLinearColor& Color = RefinementBodyColor, int32 FontSize = 10)
	{
		UTextBlock* Label = FMT2UIStyle::Label(Tree, Text, FontSize);
		Label->SetColorAndOpacity(FSlateColor(Color));
		Label->SetShadowOffset(FVector2D(1.0f));
		Parent->AddChildToVerticalBox(Label);
		return Label;
	}

	FText ItemName(const UMT2ItemTemplate* Template)
	{
		if (!Template)
		{
			return FText::GetEmpty();
		}
		return Template->DisplayName.IsEmpty()
			? FText::FromString(Template->InternalName) : Template->DisplayName;
	}
}

TSharedRef<SWidget> UMT2RefinementDialogWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
			UCanvasPanel::StaticClass(), TEXT("RefinementRoot"));
		WidgetTree->RootWidget = Root;

		UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(
			UVerticalBox::StaticClass(), TEXT("RefinementContent"));
		UWidget* Board = FMT2UIStyle::ThinBoard(
			*WidgetTree, Content, FMargin(12.0f, 9.0f));
		if (UCanvasPanelSlot* BoardPanelSlot = Root->AddChildToCanvas(Board))
		{
			BoardPanelSlot->SetAnchors(FAnchors(0.5f, 0.5f));
			BoardPanelSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			BoardPanelSlot->SetAutoSize(true);
		}

		AddLabel(*WidgetTree, Content, NSLOCTEXT("MT2", "RefinementTitle", "Upgrades"),
			UMT2ItemTooltipWidget::TitleColor, 13);

		ComparisonBox = WidgetTree->ConstructWidget<UHorizontalBox>(
			UHorizontalBox::StaticClass(), TEXT("RefinementComparison"));
		if (UVerticalBoxSlot* ComparisonSlot =
			Content->AddChildToVerticalBox(ComparisonBox))
		{
			ComparisonSlot->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 8.0f));
			ComparisonSlot->SetHorizontalAlignment(HAlign_Center);
		}

		RequirementsBox = WidgetTree->ConstructWidget<UVerticalBox>(
			UVerticalBox::StaticClass(), TEXT("RefinementRequirements"));
		Content->AddChildToVerticalBox(RequirementsBox);

		ChanceText = AddLabel(
			*WidgetTree, Content, FText::GetEmpty(), RefinementGoodColor, 11);
		FailureText = AddLabel(
			*WidgetTree, Content, FText::GetEmpty(), RefinementBadColor, 10);

		UHorizontalBox* Buttons = WidgetTree->ConstructWidget<UHorizontalBox>(
			UHorizontalBox::StaticClass(), TEXT("RefinementButtons"));
		if (UVerticalBoxSlot* ButtonsSlot = Content->AddChildToVerticalBox(Buttons))
		{
			ButtonsSlot->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 0.0f));
			ButtonsSlot->SetHorizontalAlignment(HAlign_Center);
		}

		const auto AddButton = [this, Buttons](const TCHAR* Name, const FText& Text)
		{
			UButton* Button = WidgetTree->ConstructWidget<UButton>(
				UButton::StaticClass(), Name);
			UTextBlock* Label = FMT2UIStyle::Label(*WidgetTree, Text, 10);
			Button->AddChild(Label);
			if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(Label->Slot))
			{
				ButtonSlot->SetPadding(FMargin(18.0f, 4.0f));
			}
			if (UHorizontalBoxSlot* HorizontalSlot =
				Buttons->AddChildToHorizontalBox(Button))
			{
				HorizontalSlot->SetPadding(FMargin(4.0f, 0.0f));
			}
			return Button;
		};

		ConfirmButton = AddButton(
			TEXT("ConfirmRefinement"), NSLOCTEXT("MT2", "RefineOk", "OK"));
		UButton* CancelButton = AddButton(
			TEXT("CancelRefinement"), NSLOCTEXT("MT2", "RefineCancel", "Cancel"));
		ConfirmButton->OnClicked.AddUniqueDynamic(
			this, &UMT2RefinementDialogWidget::HandleConfirmClicked);
		CancelButton->OnClicked.AddUniqueDynamic(
			this, &UMT2RefinementDialogWidget::HandleCancelClicked);

		SetIsFocusable(true);
		SetVisibility(ESlateVisibility::Collapsed);
	}
	return Super::RebuildWidget();
}

void UMT2RefinementDialogWidget::OpenRefinement(
	int32 TargetSlot, int32 ScrollSlot, AMT2Npc* Blacksmith)
{
	TakeWidget();
	PendingTargetSlot = TargetSlot;
	PendingScrollSlot = ScrollSlot;
	PendingBlacksmith = Blacksmith;
	RebuildPreview();
	SetVisibility(ESlateVisibility::Visible);

	if (!bInteractionLocked)
	{
		if (AMT2PlayerCharacter* Player =
			Cast<AMT2PlayerCharacter>(GetOwningPlayerPawn()))
		{
			Player->SetInteractionUIOpen(true);
			bInteractionLocked = true;
		}
	}
	SetKeyboardFocus();
}

void UMT2RefinementDialogWidget::RebuildPreview()
{
	ComparisonBox->ClearChildren();
	RequirementsBox->ClearChildren();

	AMT2PlayerCharacter* Player = Cast<AMT2PlayerCharacter>(GetOwningPlayerPawn());
	UMT2InventoryComponent* Inventory = Player ? Player->GetInventoryComponent() : nullptr;
	if (!Inventory)
	{
		ConfirmButton->SetIsEnabled(false);
		return;
	}
	const TArray<FMT2ItemSlot>& Slots = Inventory->GetSlots();
	if (!Slots.IsValidIndex(PendingTargetSlot) || Slots[PendingTargetSlot].IsEmpty())
	{
		ConfirmButton->SetIsEnabled(false);
		return;
	}

	const FMT2ItemSlot& CurrentItem = Slots[PendingTargetSlot];
	const UMT2ItemTemplate* CurrentTemplate =
		Inventory->GetTemplateAtSlot(PendingTargetSlot);
	UGameInstance* GameInstance = GetGameInstance();
	UMT2VnumRegistrySubsystem* Registry = GameInstance
		? GameInstance->GetSubsystem<UMT2VnumRegistrySubsystem>() : nullptr;
	const UMT2ItemTemplate* FutureTemplate =
		CurrentTemplate && Registry
			? Registry->ResolveItemTemplateClass(
				CurrentTemplate->RefinedVnum).GetDefaultObject() : nullptr;
	if (!CurrentTemplate || !FutureTemplate ||
		!CurrentTemplate->RefinementRecipe.IsValid())
	{
		ConfirmButton->SetIsEnabled(false);
		return;
	}

	const AMT2PlayerState* State = Player->GetPlayerState<AMT2PlayerState>();
	const auto AddTooltip = [this, State](const FText& Heading,
		const UMT2ItemTemplate* Template, const FMT2ItemSlot& Item)
	{
		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(
			UVerticalBox::StaticClass());
		AddLabel(*WidgetTree, Column, Heading, UMT2ItemTooltipWidget::TitleColor, 10);
		if (UMT2ItemTooltipWidget* Tooltip = UMT2ItemTooltipWidget::Create(
			GetOwningPlayer(), Template, Item.Count, State, -1,
			&Item.Bonuses, &Item.MetinSockets, Item.SkillVnum))
		{
			Column->AddChildToVerticalBox(Tooltip);
		}
		if (UHorizontalBoxSlot* ColumnSlot =
			ComparisonBox->AddChildToHorizontalBox(Column))
		{
			ColumnSlot->SetPadding(FMargin(5.0f, 0.0f));
			ColumnSlot->SetVerticalAlignment(VAlign_Top);
		}
	};

	AddTooltip(NSLOCTEXT("MT2", "RefinementCurrent", "Current"),
		CurrentTemplate, CurrentItem);
	FMT2ItemSlot FutureItem = CurrentItem;
	FutureItem.Vnum = FutureTemplate->Vnum;
	AddTooltip(NSLOCTEXT("MT2", "RefinementFuture", "After upgrade"),
		FutureTemplate, FutureItem);

	const FMT2RefinementRecipe& Recipe = CurrentTemplate->RefinementRecipe;
	const int64 CurrentYang = State ? State->GetYang() : 0;
	AddLabel(*WidgetTree, RequirementsBox,
		FText::Format(NSLOCTEXT("MT2", "RefinementYang",
			"Cost: {0} Yang ({1} available)"),
			FText::AsNumber(Recipe.YangCost), FText::AsNumber(CurrentYang)),
		CurrentYang >= Recipe.YangCost ? RefinementBodyColor : RefinementBadColor);

	bool bHasMaterials = true;
	const auto CountAvailableMaterial =
		[&Slots, this](int32 Vnum)
	{
		int32 Total = 0;
		for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
		{
			if (SlotIndex != PendingTargetSlot && SlotIndex != PendingScrollSlot &&
				!Slots[SlotIndex].IsEmpty() && Slots[SlotIndex].Vnum == Vnum)
			{
				Total += Slots[SlotIndex].Count;
			}
		}
		return Total;
	};
	for (const FMT2RefinementMaterial& Material : Recipe.Materials)
	{
		const int32 Owned = CountAvailableMaterial(Material.ItemVnum);
		const UMT2ItemTemplate* MaterialTemplate = Registry
			? Registry->ResolveItemTemplateClass(Material.ItemVnum).GetDefaultObject() : nullptr;
		AddLabel(*WidgetTree, RequirementsBox,
			FText::Format(NSLOCTEXT("MT2", "RefinementMaterial", "{0}: {1}/{2}"),
				ItemName(MaterialTemplate), FText::AsNumber(Owned),
				FText::AsNumber(Material.Count)),
			Owned >= Material.Count ? RefinementBodyColor : RefinementBadColor);
		bHasMaterials &= Owned >= Material.Count;
	}

	const UMT2ItemRefinementScrollTemplate* Scroll = Registry &&
		PendingScrollSlot != INDEX_NONE && Slots.IsValidIndex(PendingScrollSlot)
			? Cast<UMT2ItemRefinementScrollTemplate>(
				Registry->ResolveItemTemplateClass(
					Slots[PendingScrollSlot].Vnum).GetDefaultObject()) : nullptr;
	const int32 Chance = FMath::Clamp(
		Recipe.SuccessPercent + (Scroll ? Scroll->AdditionalSuccessPercent : 0),
		0, 100);
	ChanceText->SetText(FText::Format(
		NSLOCTEXT("MT2", "RefinementChance", "Success chance: {0}%"),
		FText::AsNumber(Chance)));
	FailureText->SetText(Scroll
		? NSLOCTEXT("MT2", "RefinementScrollFailure",
			"Failure lowers refinement by one level (never below +0).")
		: NSLOCTEXT("MT2", "RefinementBlacksmithFailure",
			"Failure destroys the item."));
	ConfirmButton->SetIsEnabled(
		State && CurrentYang >= Recipe.YangCost && bHasMaterials);
}

void UMT2RefinementDialogWidget::HandleConfirmClicked()
{
	if (AMT2PlayerCharacter* Player =
		Cast<AMT2PlayerCharacter>(GetOwningPlayerPawn()))
	{
		Player->ServerRefineInventoryItem(
			PendingTargetSlot, PendingScrollSlot, PendingBlacksmith.Get());
	}
	CloseDialog();
}

void UMT2RefinementDialogWidget::HandleCancelClicked()
{
	CloseDialog();
}

void UMT2RefinementDialogWidget::CloseDialog()
{
	if (bInteractionLocked)
	{
		if (AMT2PlayerCharacter* Player =
			Cast<AMT2PlayerCharacter>(GetOwningPlayerPawn()))
		{
			Player->SetInteractionUIOpen(false);
		}
		bInteractionLocked = false;
	}
	PendingTargetSlot = INDEX_NONE;
	PendingScrollSlot = INDEX_NONE;
	PendingBlacksmith.Reset();
	SetVisibility(ESlateVisibility::Collapsed);
}

void UMT2RefinementDialogWidget::NativeDestruct()
{
	if (bInteractionLocked)
	{
		if (AMT2PlayerCharacter* Player =
			Cast<AMT2PlayerCharacter>(GetOwningPlayerPawn()))
		{
			Player->SetInteractionUIOpen(false);
		}
		bInteractionLocked = false;
	}
	Super::NativeDestruct();
}

FReply UMT2RefinementDialogWidget::NativeOnPreviewKeyDown(
	const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		CloseDialog();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}
