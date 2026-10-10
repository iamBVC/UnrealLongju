#pragma once
#include "Commandlets/Commandlet.h"
#include "MT2ConfigureDevilTowerCommandlet.generated.h"

UCLASS()
class UMT2ConfigureDevilTowerCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UMT2ConfigureDevilTowerCommandlet();
	virtual int32 Main(const FString& Params) override;
};
