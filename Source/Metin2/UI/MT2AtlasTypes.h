/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "MT2AtlasTypes.generated.h"

USTRUCT(BlueprintType)
struct METIN2_API FMT2AtlasRect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atlas", meta = (ClampMin = "0.0"))
	float X = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atlas", meta = (ClampMin = "0.0"))
	float Y = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atlas", meta = (ClampMin = "0.0"))
	float Width = 32.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atlas", meta = (ClampMin = "0.0"))
	float Height = 32.0f;

	FMT2AtlasRect() = default;
	FMT2AtlasRect(float InX, float InY, float InWidth, float InHeight)
		: X(InX), Y(InY), Width(InWidth), Height(InHeight)
	{
	}

	bool IsValid() const { return Width > 0.0f && Height > 0.0f; }
};
