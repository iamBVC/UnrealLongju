/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Localization/MT2LocalizationSubsystem.h"

#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Text.h"
#include "Internationalization/TextLocalizationManager.h"
#include "Localization/MT2LocalizationSettings.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
	const TCHAR* LocalizationConfigSection = TEXT("MT2Localization");
	const TCHAR* CultureConfigKey = TEXT("Culture");
}

void UMT2LocalizationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ApplySavedCulture();
}

TArray<FMT2LanguageOption> UMT2LocalizationSubsystem::GetSupportedLanguages() const
{
	TArray<FMT2LanguageOption> Result;
	TSet<FString> AddedCultures;
	for (const FString& CultureCode : UMT2LocalizationSettings::Get().SupportedCultures)
	{
		const FString CanonicalCode = CultureCode.TrimStartAndEnd();
		if (CanonicalCode.IsEmpty() || AddedCultures.Contains(CanonicalCode))
		{
			continue;
		}
		const FCulturePtr Culture = FInternationalization::Get().GetCulture(CanonicalCode);
		if (!Culture.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("Unsupported culture '%s' in MT2 localization settings."), *CanonicalCode);
			continue;
		}

		FMT2LanguageOption& Option = Result.AddDefaulted_GetRef();
		Option.CultureCode = CanonicalCode;
		Option.DisplayName = FText::FromString(Culture->GetNativeName());
		AddedCultures.Add(CanonicalCode);
	}
	return Result;
}

FString UMT2LocalizationSubsystem::GetCurrentCulture() const
{
	const FCulturePtr Culture = FInternationalization::Get().GetCurrentCulture();
	return Culture.IsValid() ? Culture->GetName() : UMT2LocalizationSettings::Get().DefaultCulture;
}

bool UMT2LocalizationSubsystem::SetCurrentCulture(const FString& CultureCode)
{
	const FString RequestedCulture = CultureCode.TrimStartAndEnd();
	if (!IsSupportedCulture(RequestedCulture) ||
		!FInternationalization::Get().SetCurrentCulture(RequestedCulture))
	{
		return false;
	}

	if (GConfig)
	{
		GConfig->SetString(LocalizationConfigSection, CultureConfigKey, *RequestedCulture, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	FTextLocalizationManager::Get().RefreshResources();
	OnLanguageChanged.Broadcast(RequestedCulture);
	return true;
}

FText UMT2LocalizationSubsystem::MakeLocalizedText(FName Namespace, FName Key, const FText& EnglishSource)
{
	return FText::AsLocalizable_Advanced(
		FTextKey(Namespace.ToString()), FTextKey(Key.ToString()), EnglishSource.ToString());
}

bool UMT2LocalizationSubsystem::IsSupportedCulture(const FString& CultureCode) const
{
	return UMT2LocalizationSettings::Get().SupportedCultures.ContainsByPredicate(
		[&CultureCode](const FString& Supported)
		{
			return Supported.Equals(CultureCode, ESearchCase::IgnoreCase);
		});
}

void UMT2LocalizationSubsystem::ApplySavedCulture()
{
	FString CultureCode = UMT2LocalizationSettings::Get().DefaultCulture;
	if (GConfig)
	{
		GConfig->GetString(LocalizationConfigSection, CultureConfigKey, CultureCode, GGameUserSettingsIni);
	}
	if (!SetCurrentCulture(CultureCode))
	{
		SetCurrentCulture(UMT2LocalizationSettings::Get().DefaultCulture);
	}
}
