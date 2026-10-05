#pragma once
#include "Commandlets/Commandlet.h"
#include "MT2ImportMapAttributesCommandlet.generated.h"
UCLASS()
class UMT2ImportMapAttributesCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UMT2ImportMapAttributesCommandlet();
	virtual int32 Main(const FString& Params) override;
};
