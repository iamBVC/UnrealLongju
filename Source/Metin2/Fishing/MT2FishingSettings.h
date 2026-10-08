#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MT2FishingSettings.generated.h"

class UStaticMesh;
class UMaterialInterface;
class UMT2ItemTemplate;

USTRUCT(BlueprintType)
struct METIN2_API FMT2FishingCatch
{
	GENERATED_BODY()
	// Null denotes the legacy miss row; rewards are selected by their item class.
	UPROPERTY(EditAnywhere, Config, Category="Fishing") TSoftClassPtr<UMT2ItemTemplate> ItemTemplate;
	UPROPERTY(EditAnywhere, Config, Category="Fishing") TArray<int32> Weights;
	UPROPERTY(EditAnywhere, Config, Category="Fishing") int32 Difficulty = 1;
	UPROPERTY(EditAnywhere, Config, Category="Fishing") int32 TimeProfile = 0;
	UPROPERTY(EditAnywhere, Config, Category="Fishing") TArray<int32> LengthRange;
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2FishingTimingProfile
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category="Fishing") TArray<int32> Chances;
};

USTRUCT(BlueprintType)
struct METIN2_API FMT2FishingMapTable
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Config, Category="Fishing") int32 MapIndex = 0;
	UPROPERTY(EditAnywhere, Config, Category="Fishing") int32 TableIndex = 0;
};

// Runtime uses cooked Game config, not external legacy files or client-provided loot tables.
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Metin2 Fishing"))
class METIN2_API UMT2FishingSettings : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	virtual FName GetCategoryName() const override { return TEXT("Metin2"); }
	UPROPERTY(EditAnywhere, Config, Category="Rules", meta=(ClampMin="1", ClampMax="3600")) int32 MinimumWaitSeconds = 10;
	UPROPERTY(EditAnywhere, Config, Category="Rules", meta=(ClampMin="1", ClampMax="3600")) int32 MaximumWaitSeconds = 40;
	UPROPERTY(EditAnywhere, Config, Category="Rules", meta=(ClampMin="1", ClampMax="10000", Units="cm")) float CastDistance = 600;
	UPROPERTY(EditAnywhere, Config, Category="Rules", meta=(ClampMin="0", ClampMax="100", Units="cm")) float MovementTolerance = 25;
	UPROPERTY(EditAnywhere, Config, Category="Rules", meta=(ClampMin="0", ClampMax="3600", Units="s")) float DroppedRewardOwnershipSeconds = 60;
	UPROPERTY(EditAnywhere, Config, Category="Loot") TArray<FMT2FishingMapTable> MapTables;
	UPROPERTY(EditAnywhere, Config, Category="Loot") TArray<FMT2FishingCatch> CatchTable;
	UPROPERTY(EditAnywhere, Config, Category="Rod") TArray<int32> FishermanVnums;
	UPROPERTY(EditAnywhere, Config, Category="Timing") TArray<FMT2FishingTimingProfile> TimingProfiles;
	UPROPERTY(EditAnywhere, Config, Category="Presentation") TSoftObjectPtr<UStaticMesh> FloatMesh;
	UPROPERTY(EditAnywhere, Config, Category="Presentation") TSoftObjectPtr<UStaticMesh> RodMesh;
	UPROPERTY(EditAnywhere, Config, Category="Presentation") TSoftObjectPtr<UMaterialInterface> FloatMaterial;
	UPROPERTY(EditAnywhere, Config, Category="Presentation") FVector FloatScale = FVector(.06, .06, .12);
	UPROPERTY(EditAnywhere, Config, Category="Presentation", meta=(Units="cm")) float FloatHeightOffset = 4;
	bool Validate(FString& Error) const;
	int32 FindTable(int32 MapIndex) const;
	int32 PickCatch(int32 TableIndex, int32 Roll) const;
	int32 TimingChance(int32 Profile, int32 ElapsedMilliseconds) const;
	bool ResolveCatch(const FMT2FishingCatch& Catch, int32 ElapsedMilliseconds, int32 FishingPower, int32 TimingRoll, int32 DifficultyRoll) const;
};
