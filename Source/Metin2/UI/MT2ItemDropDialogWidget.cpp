/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2ItemDropDialogWidget.h"

#include "Characters/MT2PlayerCharacter.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Items/MT2ItemTemplate.h"
#include "Items/MT2ItemUtils.h"
#include "UI/MT2BoardWidget.h"

void UMT2ItemDropDialogWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BoardWidget->InitializeBoard(FVector2D(340.0f, 105.0f));
	ConfirmButton->OnClicked.AddUniqueDynamic(this, &UMT2ItemDropDialogWidget::HandleConfirmClicked);
	CancelButton->OnClicked.AddUniqueDynamic(this, &UMT2ItemDropDialogWidget::HandleCancelClicked);
}

void UMT2ItemDropDialogWidget::OpenDropDialog(
	int32 InventorySlot, int32 Vnum, int32 Count)
{
	bPendingMetinAttach = false;
	bPendingTargetEquipped = false;
	PendingTargetSlot = INDEX_NONE;
	PendingInventorySlot = InventorySlot;
	PendingVnum = Vnum;
	PendingCount = FMath::Max(Count, 1);

	const UMT2ItemTemplate* Template = MT2ItemUtils::ResolveTemplate(this, Vnum);
	const FText ItemName = MT2ItemUtils::GetDisplayName(Template, Vnum);
	MessageText->SetText(FText::Format(
		NSLOCTEXT("MT2", "DropItemQuestion", "Drop {0} x{1} on the ground?"),
		ItemName, FText::AsNumber(PendingCount)));
	SetVisibility(ESlateVisibility::Visible);
}

void UMT2ItemDropDialogWidget::OpenMetinAttachDialog(
	int32 StoneSlot, int32 TargetSlot, bool bTargetEquipped,
	const FText& StoneName, const FText& TargetName)
{
	bPendingMetinAttach = true;
	bPendingTargetEquipped = bTargetEquipped;
	PendingInventorySlot = StoneSlot;
	PendingTargetSlot = TargetSlot;
	PendingVnum = 0;
	PendingCount = 1;
	MessageText->SetText(FText::Format(
		NSLOCTEXT("MT2", "AttachMetinQuestion",
			"Attach {0} to {1}?\nThe stone is consumed even if attachment fails."),
		StoneName, TargetName));
	SetVisibility(ESlateVisibility::Visible);
}

void UMT2ItemDropDialogWidget::HandleConfirmClicked()
{
	if (AMT2PlayerCharacter* Character = Cast<AMT2PlayerCharacter>(GetOwningPlayerPawn()))
	{
		if (bPendingMetinAttach)
		{
			Character->ServerApplyMetinStone(
				PendingInventorySlot, PendingTargetSlot, bPendingTargetEquipped);
		}
		else
		{
			Character->ServerDropInventoryItem(PendingInventorySlot, PendingCount);
		}
	}
	CloseDialog();
}

void UMT2ItemDropDialogWidget::HandleCancelClicked()
{
	CloseDialog();
}

void UMT2ItemDropDialogWidget::CloseDialog()
{
	PendingInventorySlot = INDEX_NONE;
	PendingVnum = 0;
	PendingCount = 0;
	PendingTargetSlot = INDEX_NONE;
	bPendingMetinAttach = false;
	bPendingTargetEquipped = false;
	RemoveFromParent();
}
