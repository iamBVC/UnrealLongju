/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "UI/MT2FloatingDamageActor.h"
#include "Config/MT2PathSettings.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

AMT2FloatingDamageActor::AMT2FloatingDamageActor()
{
	PrimaryActorTick.bCanEverTick = true;
	SetReplicates(false);
	SetActorEnableCollision(false);

	DamageText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("DamageText"));
	SetRootComponent(DamageText);
	DamageText->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
	DamageText->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextCenter);
	DamageText->SetWorldSize(34.0f);
	DamageText->SetCastShadow(false);
	DamageText->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DamageText->SetTranslucentSortPriority(100);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> TranslucentTextMaterial(
		UMT2PathSettings::Path(TEXT("Engine_EngineMaterials_DefaultTextMaterialTranslucent")));
	if (TranslucentTextMaterial.Succeeded())
	{
		DamageText->SetTextMaterial(TranslucentTextMaterial.Object);
	}
}

void AMT2FloatingDamageActor::InitializeDamage(
	float Damage, EMT2DamageDisplayType DamageType, APlayerCameraManager* InCameraManager)
{
	CameraManager = InCameraManager;
	BaseColor = GetDamageColor(DamageType);
	DamageText->SetText(FText::AsNumber(FMath::Max(1, FMath::RoundToInt(Damage))));

	const FVector CameraRight = InCameraManager
		? FRotationMatrix(InCameraManager->GetCameraRotation()).GetUnitAxis(EAxis::Y)
		: FVector::RightVector;
	const float RandomSide = FMath::FRandRange(-0.65f, 0.65f);
	Velocity = (FVector::UpVector + CameraRight * RandomSide).GetSafeNormal() * MovementSpeed;
	FColor InitialColor = BaseColor;
	InitialColor.A = 0;
	DamageText->SetTextRenderColor(InitialColor);
}

void AMT2FloatingDamageActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ElapsedTime += DeltaSeconds;
	if (ElapsedTime >= Lifetime)
	{
		Destroy();
		return;
	}

	SetActorLocation(GetActorLocation() + Velocity * DeltaSeconds);
	if (const APlayerCameraManager* Camera = CameraManager.Get())
	{
		const FVector ToCamera = Camera->GetCameraLocation() - GetActorLocation();
		if (!ToCamera.IsNearlyZero())
		{
			SetActorRotation(ToCamera.Rotation());
		}
	}

	const float Alpha = ElapsedTime < FadeInDuration
		? ElapsedTime / FadeInDuration
		: 1.0f - (ElapsedTime - FadeInDuration) / FMath::Max(Lifetime - FadeInDuration, UE_SMALL_NUMBER);
	FColor CurrentColor = BaseColor;
	CurrentColor.A = static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Alpha, 0.0f, 1.0f) * 255.0f));
	DamageText->SetTextRenderColor(CurrentColor);
}

FColor AMT2FloatingDamageActor::GetDamageColor(EMT2DamageDisplayType DamageType)
{
	switch (DamageType)
	{
	case EMT2DamageDisplayType::Critical: return FColor(255, 35, 35);
	case EMT2DamageDisplayType::Penetrating: return FColor(35, 35, 255);
	case EMT2DamageDisplayType::CriticalPenetrating: return FColor(255, 35, 255);
	case EMT2DamageDisplayType::Poison: return FColor(35, 255, 35);
	default: return FColor(255, 255, 35);
	}
}
