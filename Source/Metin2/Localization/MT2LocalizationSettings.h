/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MT2LocalizationSettings.generated.h"

// Languages exposed to players. Add a culture code here only after its localization resources
// are ready to ship (for example en, it, de or pt-BR).
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Localization"))
class METIN2_API UMT2LocalizationSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const UMT2LocalizationSettings& Get()
	{
		return *GetDefault<UMT2LocalizationSettings>();
	}

	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	UPROPERTY(EditAnywhere, Config, Category = "Languages")
	FString DefaultCulture = TEXT("en");

	UPROPERTY(EditAnywhere, Config, Category = "Languages")
	TArray<FString> SupportedCultures = {TEXT("en"), TEXT("it")};
};
