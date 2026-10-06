#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UObject/SoftObjectPath.h"
#include "MT2PathSettings.generated.h"

USTRUCT()
struct METIN2_API FMT2ConfiguredAssetPath
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Path")
	FName Key;
	UPROPERTY(EditAnywhere, Config, Category = "Path")
	FSoftObjectPath Value;
};

USTRUCT()
struct METIN2_API FMT2ConfiguredLocation
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category = "Path")
	FName Key;
	UPROPERTY(EditAnywhere, Config, Category = "Path")
	FString Value;
};

// The catalog is snapshotted on first use. Restart after edits: constructors, registries,
// generated assets and existing Blueprint overrides cannot safely be relocated live.
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Asset and File Paths"))
class METIN2_API UMT2PathSettings : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	virtual FName GetCategoryName() const override { return TEXT("Metin2"); }

	// Stable, process-lifetime strings, including for older const TCHAR* call sites.
	// Format arguments describe the exact permitted printf tokens, not a fallback path.
	static const TCHAR* Path(FName Key, const TCHAR* Format = TEXT(""));
	static bool HasFormat(const FString& Value, const FString& Expected);
	// No C varargs: INI text can never change argument types or execute a %n conversion.
	template <typename... Types>
	static FString Format(FName Key, const TCHAR* Signature, Types... Arguments)
	{
		return FormatValues(Key, Signature, TArray<FString>{ToString(Arguments)...});
	}

	UPROPERTY(EditAnywhere, Config, Category = "Paths", meta = (ConfigRestartRequired = true, TitleProperty = "Key"))
	TArray<FMT2ConfiguredAssetPath> Assets;

	// {Key} references another catalog location; {ProjectDir}, {ContentDir}, {SavedDir}
	// are resolved using Unreal's platform-aware project directories.
	UPROPERTY(EditAnywhere, Config, Category = "Paths", meta = (ConfigRestartRequired = true, TitleProperty = "Key"))
	TArray<FMT2ConfiguredLocation> Locations;

private:
	static FString ToString(const TCHAR* Value) { return FString(Value); }
	static FString ToString(const FString& Value) { return Value; }
	static FString ToString(int32 Value) { return FString::FromInt(Value); }
	static FString FormatValues(FName Key, const TCHAR* Signature, const TArray<FString>& Arguments);
};
