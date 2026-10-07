#pragma once
#include "Commandlets/Commandlet.h"
#include "MT2BakeMapWaterCommandlet.generated.h"

UCLASS()
class UMT2BakeMapWaterCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UMT2BakeMapWaterCommandlet();
	virtual int32 Main(const FString& Params) override;
};
