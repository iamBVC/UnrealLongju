/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "UI/MT2UserWidget.h"
#include "CoreMinimal.h"
#include "MT2TitleBarWidget.generated.h"

class UButton;
class UImage;
class UTextBlock;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMT2TitleBarCloseSignature);

UCLASS()
class METIN2_API UMT2TitleBarWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Title Bar")
	FMT2TitleBarCloseSignature OnCloseClicked;

	UFUNCTION(BlueprintCallable, Category = "Title Bar")
	void InitializeTitleBar(float InWidth, const FText& InTitle);

protected:
	virtual void NativeConstruct() override;
	virtual void NativePreConstruct() override;

private:
	UFUNCTION() void HandleCloseClicked();
	void RefreshVisuals();

	UPROPERTY(EditAnywhere, Category = "Title Bar") FText Title = FText::FromString(TEXT("Window"));
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> TitleLeft;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> TitleCenter;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> TitleRight;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> TitleText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> CloseButton;
};
