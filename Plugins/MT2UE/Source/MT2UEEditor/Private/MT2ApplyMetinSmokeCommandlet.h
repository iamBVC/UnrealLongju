#pragma once
#include "Commandlets/Commandlet.h"
#include "MT2ApplyMetinSmokeCommandlet.generated.h"

UCLASS()
class UMT2ApplyMetinSmokeCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UMT2ApplyMetinSmokeCommandlet();
	virtual int32 Main(const FString& Params) override;
};
