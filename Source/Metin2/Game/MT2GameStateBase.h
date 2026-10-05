/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "MT2GameStateBase.generated.h"

UCLASS(Blueprintable)
class METIN2_API AMT2GameStateBase : public AGameStateBase
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Server")
	const FString& GetMapId() const { return MapId; }

	UFUNCTION(BlueprintPure, Category = "Server")
	int32 GetServerChannel() const { return ServerChannel; }

private:
	UPROPERTY(Replicated)
	FString MapId;

	UPROPERTY(Replicated)
	int32 ServerChannel = 1;
};
