/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Combat/MT2DamageTypes.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MT2FloatingDamageActor.generated.h"

class APlayerCameraManager;
class UTextRenderComponent;

UCLASS(NotBlueprintable, Transient)
class METIN2_API AMT2FloatingDamageActor : public AActor
{
	GENERATED_BODY()

public:
	AMT2FloatingDamageActor();
	virtual void Tick(float DeltaSeconds) override;

	void InitializeDamage(float Damage, EMT2DamageDisplayType DamageType,
		APlayerCameraManager* InCameraManager, bool bReceivedDamage = false);

private:
	static FColor GetDamageColor(EMT2DamageDisplayType DamageType);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UTextRenderComponent> DamageText;

	TWeakObjectPtr<APlayerCameraManager> CameraManager;
	FVector Velocity = FVector::ZeroVector;
	FColor BaseColor = FColor::Yellow;
	float ElapsedTime = 0.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Damage Number", meta = (ClampMin = "0.1", Units = "s"))
	float Lifetime = 1.1f;

	UPROPERTY(EditDefaultsOnly, Category = "Damage Number", meta = (ClampMin = "0.01", Units = "s"))
	float FadeInDuration = 0.14f;

	UPROPERTY(EditDefaultsOnly, Category = "Damage Number", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MovementSpeed = 90.0f;
};
