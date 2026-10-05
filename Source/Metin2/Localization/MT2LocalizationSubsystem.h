/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MT2LocalizationSubsystem.generated.h"

USTRUCT(BlueprintType)
struct METIN2_API FMT2LanguageOption
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Localization")
	FString CultureCode;

	UPROPERTY(BlueprintReadOnly, Category = "Localization")
	FText DisplayName;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMT2LanguageChangedSignature, const FString&, CultureCode);

UCLASS()
class METIN2_API UMT2LocalizationSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintPure, Category = "MT2|Localization")
	TArray<FMT2LanguageOption> GetSupportedLanguages() const;

	UFUNCTION(BlueprintPure, Category = "MT2|Localization")
	FString GetCurrentCulture() const;

	// Changes language immediately and persists it for the next client launch.
	UFUNCTION(BlueprintCallable, Category = "MT2|Localization")
	bool SetCurrentCulture(const FString& CultureCode);

	// Gives runtime-created text a stable namespace/key so it can use the same localization
	// pipeline as LOCTEXT and Widget Blueprint text.
	UFUNCTION(BlueprintPure, Category = "MT2|Localization")
	static FText MakeLocalizedText(FName Namespace, FName Key, const FText& EnglishSource);

	UPROPERTY(BlueprintAssignable, Category = "MT2|Localization")
	FMT2LanguageChangedSignature OnLanguageChanged;

private:
	bool IsSupportedCulture(const FString& CultureCode) const;
	void ApplySavedCulture();
};
