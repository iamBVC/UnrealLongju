/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "UI/MT2UserWidget.h"
#include "CoreMinimal.h"
#include "MT2BoardWidget.generated.h"

class UImage;

UCLASS()
class METIN2_API UMT2BoardWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Board")
	void InitializeBoard(const FVector2D& InSize);

protected:
	virtual void NativePreConstruct() override;

private:
	void ApplyVisuals();

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> BoardBase;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> BoardTop;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> BoardBottom;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> BoardLeft;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> BoardRight;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> BoardLT;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> BoardRT;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> BoardLB;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> BoardRB;
};
