/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2CursorCarrySubsystem.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Engine/LocalPlayer.h"
#include "UI/MT2CursorCarryWidget.h"

void UMT2CursorCarrySubsystem::BeginCarry(
	EMT2CarryKind Kind, int32 Vnum, int32 Payload, const FSlateBrush& IconBrush, int32 InSourceQuickSlot)
{
	if (Kind == EMT2CarryKind::None)
	{
		CancelCarry();
		return;
	}

	CarryKind = Kind;
	CarryVnum = Vnum;
	CarryPayload = Payload;
	SourceQuickSlot = InSourceQuickSlot;

	if (!CarryWidget)
	{
		APlayerController* Controller = GetLocalPlayer()
			? GetLocalPlayer()->GetPlayerController(GetLocalPlayer()->GetWorld()) : nullptr;
		if (!Controller)
		{
			return;
		}
		CarryWidget = CreateWidget<UMT2CursorCarryWidget>(Controller, UMT2CursorCarryWidget::StaticClass());
	}
	CarryWidget->SetIconBrush(IconBrush);
	if (!CarryWidget->IsInViewport())
	{
		// Above every window so the carried icon is never hidden behind boards.
		CarryWidget->AddToViewport(1000);
	}
	CarryWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UMT2CursorCarrySubsystem::CancelCarry()
{
	EndCarry();
}

void UMT2CursorCarrySubsystem::EndCarry()
{
	CarryKind = EMT2CarryKind::None;
	CarryVnum = 0;
	CarryPayload = INDEX_NONE;
	SourceQuickSlot = INDEX_NONE;
	if (CarryWidget)
	{
		CarryWidget->SetVisibility(ESlateVisibility::Collapsed);
	}
}
