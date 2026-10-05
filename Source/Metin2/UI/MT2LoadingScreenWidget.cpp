/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2LoadingScreenWidget.h"

#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/Throbber.h"

void UMT2LoadingScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();
	LoadingProgress->SetIsMarquee(true);
	SetLoadingText(TEXT("Loading..."));
}

void UMT2LoadingScreenWidget::SetLoadingText(const FString& Message)
{
	StatusText->SetText(FText::FromString(Message));
}
