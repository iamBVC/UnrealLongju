/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Player/MT2PlayerTypes.h"
#include "UI/MT2AtlasImage.h"
#include "MT2EmpireFlagImage.generated.h"

// Compact atlas-backed empire flag shared by chat and world nameplates. Atlas and regions remain
// editable in Project Settings -> Metin2 Gameplay -> UI|Empire.
UCLASS(meta = (DisplayName = "MT2 Empire Flag"))
class METIN2_API UMT2EmpireFlagImage : public UMT2AtlasImage
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Empire")
	void SetEmpire(EMT2Empire NewEmpire);

	UFUNCTION(BlueprintPure, Category = "Empire")
	EMT2Empire GetEmpire() const { return Empire; }

protected:
	virtual void SynchronizeProperties() override;

private:
	void RefreshEmpireBrush();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Empire", meta = (AllowPrivateAccess = "true"))
	EMT2Empire Empire = EMT2Empire::None;
};
