/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "MT2CursorCarryWidget.generated.h"

class UImage;

// The icon glued to the cursor while a skill/item is being carried. Fully native, input-invisible.
UCLASS()
class METIN2_API UMT2CursorCarryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetIconBrush(const FSlateBrush& Brush);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UImage> IconImage;
};
