/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2WidgetUtils.h"

#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/Widget.h"

bool MT2WidgetUtils::PlaceOver(UWidget* ReferenceWidget, UWidget* OverlayWidget, int32 ZOrder)
{
	if (!ReferenceWidget || !OverlayWidget)
	{
		return false;
	}
	if (UOverlay* Overlay = Cast<UOverlay>(ReferenceWidget->GetParent()))
	{
		if (UOverlaySlot* Slot = Overlay->AddChildToOverlay(OverlayWidget))
		{
			Slot->SetHorizontalAlignment(HAlign_Fill);
			Slot->SetVerticalAlignment(VAlign_Fill);
			return true;
		}
	}
	if (UCanvasPanel* Canvas = Cast<UCanvasPanel>(ReferenceWidget->GetParent()))
	{
		const UCanvasPanelSlot* ReferenceSlot = Cast<UCanvasPanelSlot>(ReferenceWidget->Slot);
		if (UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(OverlayWidget))
		{
			Slot->SetAnchors(ReferenceSlot ? ReferenceSlot->GetAnchors() : FAnchors(0.0f));
			Slot->SetPosition(ReferenceSlot ? ReferenceSlot->GetPosition() : FVector2D::ZeroVector);
			Slot->SetSize(ReferenceSlot ? ReferenceSlot->GetSize() : FVector2D(32.0f));
			Slot->SetZOrder(ZOrder);
			return true;
		}
	}
	return false;
}
