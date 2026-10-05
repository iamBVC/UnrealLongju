/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "UI/MT2UserWidget.h"
#include "MT2LoadingScreenWidget.generated.h"

class UProgressBar;
class UTextBlock;
class UThrobber;

// Reusable gateway/map-travel loading overlay. Its Blueprint owns the visual presentation.
UCLASS()
class METIN2_API UMT2LoadingScreenWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Loading")
	void SetLoadingText(const FString& Message);

protected:
	virtual void NativeConstruct() override;

private:
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> StatusText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UProgressBar> LoadingProgress;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UThrobber> LoadingThrobber;
};
