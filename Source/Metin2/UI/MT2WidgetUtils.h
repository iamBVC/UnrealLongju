/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"

class UWidget;

namespace MT2WidgetUtils
{
	// Adds OverlayWidget over ReferenceWidget while preserving Blueprint-authored panel geometry.
	METIN2_API bool PlaceOver(UWidget* ReferenceWidget, UWidget* OverlayWidget, int32 ZOrder = 20);
}
