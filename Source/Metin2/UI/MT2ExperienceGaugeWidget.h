/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "UI/MT2UserWidget.h"
#include "CoreMinimal.h"
#include "MT2ExperienceGaugeWidget.generated.h"

class UImage;
class UProgressBar;

UCLASS()
class METIN2_API UMT2ExperienceGaugeWidget : public UMT2UserWidget
{
	GENERATED_BODY()

public:
	void SetExperiencePercent(float Percent);
	void SetExperience(int64 CurrentExperience, int64 RequiredExperience);

protected:
	virtual void NativeConstruct() override;

private:
	void ApplyGaugeStyle();
	void SetGaugeToolTip(const FText& ToolTip);

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> ExperienceBackground;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UProgressBar> ExperienceSegment1;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UProgressBar> ExperienceSegment2;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UProgressBar> ExperienceSegment3;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UProgressBar> ExperienceSegment4;
	UPROPERTY(Transient) TArray<TObjectPtr<UProgressBar>> Segments;
};
