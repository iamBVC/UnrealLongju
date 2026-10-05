/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2CurrencyPanelWidget.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"

void UMT2CurrencyPanelWidget::NativeConstruct()
{
	Super::NativeConstruct();
	YangButton->OnClicked.AddUniqueDynamic(this, &UMT2CurrencyPanelWidget::HandleYangClicked);
	// Yang is the only currency here; hide the cheque counter if the Blueprint still has one.
	if (ChequeIcon) ChequeIcon->SetVisibility(ESlateVisibility::Collapsed);
	if (ChequeButton) ChequeButton->SetVisibility(ESlateVisibility::Collapsed);
	if (ChequeText) ChequeText->SetVisibility(ESlateVisibility::Collapsed);
	SetYang(Yang);
}

void UMT2CurrencyPanelWidget::SetYang(int64 Value) { Yang = FMath::Max<int64>(0, Value); YangText->SetText(FText::AsNumber(Yang)); }
void UMT2CurrencyPanelWidget::SetCheques(int32 Value)
{
	Cheques = FMath::Max(0, Value);
	if (ChequeText) { ChequeText->SetText(FText::AsNumber(Cheques)); }
}
void UMT2CurrencyPanelWidget::HandleYangClicked() { OnYangClicked.Broadcast(); }
void UMT2CurrencyPanelWidget::HandleChequeClicked() { OnChequeClicked.Broadcast(); }
