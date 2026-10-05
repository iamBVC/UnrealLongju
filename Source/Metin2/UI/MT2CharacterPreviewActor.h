/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Player/MT2PlayerTypes.h"
#include "MT2CharacterPreviewActor.generated.h"

class USceneComponent;
class USceneCaptureComponent2D;
class USkeletalMeshComponent;
class USpotLightComponent;
class UStaticMeshComponent;
class UTextureRenderTarget2D;
class UParticleSystemComponent;
struct FMT2CharacterSummary;

// Local actor rendered by UMG's preview world. It deliberately contains no gameplay, replication,
// collision, equipment state or persistence; it only reproduces the selected novice appearance.
UCLASS(NotBlueprintable, Transient)
class METIN2_API AMT2CharacterPreviewActor : public AActor
{
	GENERATED_BODY()

public:
	AMT2CharacterPreviewActor();
	virtual void Tick(float DeltaSeconds) override;

	UTextureRenderTarget2D* InitializePreview(const FIntPoint& Resolution);
	void ApplyAppearance(const FMT2CharacterAppearance& Appearance);
	void ApplyCharacterSummary(const FMT2CharacterSummary& Character);
	void FrameCapture();
	void SetPreviewActive(bool bActive);

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkeletalMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkeletalMeshComponent> HairMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> WeaponMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UParticleSystemComponent> WeaponGlitter;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UParticleSystemComponent> ArmorGlitter;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneCaptureComponent2D> SceneCapture;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USpotLightComponent> FrontLight;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> RenderTarget;

	void RefreshCaptureComponentList();
	uint32 CaptureComponentHash = 0;
};
