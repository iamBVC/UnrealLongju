/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "World/MT2MapPresentationActor.h"

#include "Audio/MT2AudioUserSettings.h"
#include "Components/AudioComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkyLightComponent.h"
#include "Sound/SoundBase.h"

AMT2MapPresentationActor::AMT2MapPresentationActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	bNetLoadOnClient = true;
	#if WITH_EDITOR
	SetIsSpatiallyLoaded(false);
	#endif
	SetCanBeDamaged(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	WaterComponent = CreateDefaultSubobject<UMT2MapWaterComponent>(TEXT("MapWater"));
	WaterComponent->SetupAttachment(SceneRoot);

	EnvironmentDirectionalLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("EnvironmentDirectionalLight"));
	EnvironmentDirectionalLight->SetupAttachment(SceneRoot);
	EnvironmentDirectionalLight->SetMobility(EComponentMobility::Movable);
	EnvironmentDirectionalLight->SetVisibility(false);

	EnvironmentSkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("EnvironmentSkyLight"));
	EnvironmentSkyLight->SetupAttachment(SceneRoot);
	EnvironmentSkyLight->SetMobility(EComponentMobility::Movable);
	EnvironmentSkyLight->SourceType = ESkyLightSourceType::SLS_CapturedScene;
	EnvironmentSkyLight->bRealTimeCapture = false;
	EnvironmentSkyLight->SetVisibility(false);

	EnvironmentFog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("EnvironmentFog"));
	EnvironmentFog->SetupAttachment(SceneRoot);
	EnvironmentFog->SetVisibility(false);

	MusicComponent = CreateDefaultSubobject<UAudioComponent>(TEXT("MapMusic"));
	MusicComponent->SetupAttachment(SceneRoot);
	MusicComponent->bAutoActivate = false;
	MusicComponent->bAllowSpatialization = false;
	MusicComponent->bIsUISound = true;
}

void AMT2MapPresentationActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyEnvironmentComponents();
	RefreshWaterRendering();
}

void AMT2MapPresentationActor::BeginPlay()
{
	Super::BeginPlay();
	GetWorld()->GetSubsystem<UMT2MapAttributeSubsystem>()->RegisterMap(this);
	ApplyEnvironmentComponents();
	RefreshWaterRendering();
	if (GetNetMode() == NM_DedicatedServer || !DefaultMusic || !MusicComponent) return;

	MusicComponent->SetSound(DefaultMusic);
	MusicComponent->SetVolumeMultiplier(FMT2AudioUserSettings::GetMusicVolume());
	FMT2AudioUserSettings::OnChanged.AddWeakLambda(this, [this]
	{
		if (MusicComponent)
		{
			MusicComponent->SetVolumeMultiplier(FMT2AudioUserSettings::GetMusicVolume());
		}
	});
	MusicComponent->OnAudioFinished.AddUniqueDynamic(
		this, &AMT2MapPresentationActor::RestartDefaultMusic);
	MusicComponent->Play();
}

void AMT2MapPresentationActor::ConfigureEnvironment(const FMT2MapEnvironmentSettings& InEnvironment)
{
	Environment = InEnvironment;
	ApplyEnvironmentComponents();
}

void AMT2MapPresentationActor::RefreshWaterRendering()
{
	if (WaterComponent) { WaterComponent->Rebuild(*this); }
}

void AMT2MapPresentationActor::ApplyEnvironmentComponents()
{
	if (!EnvironmentDirectionalLight || !EnvironmentSkyLight || !EnvironmentFog) return;

	const FVector LightDirection = Environment.LightDirection.GetSafeNormal(
		UE_SMALL_NUMBER, FVector(0.5f, 0.5f, -0.5f));
	EnvironmentDirectionalLight->SetRelativeRotation(FRotationMatrix::MakeFromX(LightDirection).Rotator());
	EnvironmentDirectionalLight->SetLightColor(Environment.DirectionalLightColor);
	EnvironmentDirectionalLight->SetIntensity(Environment.DirectionalLightIntensity);
	EnvironmentDirectionalLight->SetVisibility(Environment.bDirectionalLightEnabled);

	EnvironmentSkyLight->SetLightColor(Environment.AmbientLightColor);
	EnvironmentSkyLight->SetIntensity(Environment.AmbientLightIntensity);
	EnvironmentSkyLight->SetVisibility(Environment.bDirectionalLightEnabled);
	if (GetNetMode() != NM_DedicatedServer && EnvironmentSkyLight->IsRegistered()
		&& EnvironmentSkyLight->IsVisible())
	{
		EnvironmentSkyLight->RecaptureSky();
	}

	EnvironmentFog->SetFogInscatteringColor(Environment.FogColor);
	EnvironmentFog->SetFogDensity(Environment.FogDensity);
	EnvironmentFog->SetFogHeightFalloff(0.000001f);
	EnvironmentFog->SetFogMaxOpacity(1.0f);
	EnvironmentFog->SetStartDistance(Environment.FogNearDistance);
	EnvironmentFog->SetFogCutoffDistance(Environment.FogFarDistance);
	EnvironmentFog->SetVisibility(Environment.bFogEnabled);
}

void AMT2MapPresentationActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld()) { World->GetSubsystem<UMT2MapAttributeSubsystem>()->UnregisterMap(this); }
	FMT2AudioUserSettings::OnChanged.RemoveAll(this);
	if (MusicComponent)
	{
		MusicComponent->OnAudioFinished.RemoveDynamic(
			this, &AMT2MapPresentationActor::RestartDefaultMusic);
		MusicComponent->Stop();
	}
	Super::EndPlay(EndPlayReason);
}

void AMT2MapPresentationActor::RestartDefaultMusic()
{
	if (DefaultMusic && MusicComponent && !IsActorBeingDestroyed())
	{
		MusicComponent->Play();
	}
}

void AMT2MapPresentationActor::Configure(
	const FString& InMapId, int32 InMapIndex, const FIntPoint& InMapCells,
	const FVector2D& InWorldMin, const FVector2D& InWorldMax,
	const TArray<FMT2MinimapTile>& InMinimapTiles, const FVector2D& InDefaultTownSpawn,
	const TArray<FVector2D>& InEmpireTownSpawns)
{
	MapId = InMapId;
	MapIndex = InMapIndex;
	MapCells = InMapCells;
	WorldMin = InWorldMin;
	WorldMax = InWorldMax;
	MinimapTiles = InMinimapTiles;
	DefaultTownSpawn = InDefaultTownSpawn;
	EmpireTownSpawns = InEmpireTownSpawns;
}

FVector2D AMT2MapPresentationActor::GetTownSpawn(EMT2Empire Empire) const
{
	const int32 EmpireIndex = static_cast<int32>(Empire) - 1;
	const FVector2D Spawn = EmpireTownSpawns.IsValidIndex(EmpireIndex)
		? EmpireTownSpawns[EmpireIndex]
		: DefaultTownSpawn;
	if (!Spawn.IsNearlyZero()) return Spawn;
	return FVector2D((WorldMin.X + WorldMax.X) * 0.5f, (WorldMin.Y + WorldMax.Y) * 0.5f);
}

bool AMT2MapPresentationActor::ContainsWorldLocation(const FVector& WorldLocation) const
{
	const FVector2D Minimum(FMath::Min(WorldMin.X, WorldMax.X), FMath::Min(WorldMin.Y, WorldMax.Y));
	const FVector2D Maximum(FMath::Max(WorldMin.X, WorldMax.X), FMath::Max(WorldMin.Y, WorldMax.Y));
	constexpr float EdgeMargin = 1.0f;
	return WorldLocation.X > Minimum.X + EdgeMargin && WorldLocation.X < Maximum.X - EdgeMargin
		&& WorldLocation.Y > Minimum.Y + EdgeMargin && WorldLocation.Y < Maximum.Y - EdgeMargin;
}
