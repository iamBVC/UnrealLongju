#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MT2MapAttributes.generated.h"

class AMT2MapPresentationActor;

// Source-order cells: X increases east, Y increases south. World-space mirroring is queried below.
USTRUCT()
struct METIN2_API FMT2MapAttributes
{
	GENERATED_BODY()
	UPROPERTY() FIntPoint Size = FIntPoint::ZeroValue;
	UPROPERTY() TArray<uint8> Flags;
	bool Query(const FVector& Location, const FVector2D& WorldMin, const FVector2D& WorldMax, uint8& OutFlags) const;
};

UCLASS()
class METIN2_API UMT2MapAttributeSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	void RegisterMap(AMT2MapPresentationActor* Map);
	void UnregisterMap(AMT2MapPresentationActor* Map);
	bool IsSafeZone(const FVector& Location) const;
private:
	TArray<TWeakObjectPtr<AMT2MapPresentationActor>> Maps;
};
