/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "UI/MT2UserWidget.h"
#include "CoreMinimal.h"
#include "MT2CurrencyPanelWidget.generated.h"

class UButton;
class UImage;
class UTextBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMT2CurrencyClickedSignature);

UCLASS()
class METIN2_API UMT2CurrencyPanelWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Currency") FMT2CurrencyClickedSignature OnYangClicked;
	UPROPERTY(BlueprintAssignable, Category = "Currency") FMT2CurrencyClickedSignature OnChequeClicked;
	UFUNCTION(BlueprintCallable, Category = "Currency") void SetYang(int64 Value);
	UFUNCTION(BlueprintCallable, Category = "Currency") void SetCheques(int32 Value);

protected:
	virtual void NativeConstruct() override;

private:
	UFUNCTION() void HandleYangClicked();
	UFUNCTION() void HandleChequeClicked();
	int64 Yang = 0;
	int32 Cheques = 0;

	// The old game's second (cheque/"won") counter is not part of this recreation: the panel shows
	// yang only. Optional bindings so the widgets can be deleted from the Blueprint at any time.
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UImage> ChequeIcon;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> ChequeButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ChequeText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> YangIcon;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> YangButton;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> YangText;
};
