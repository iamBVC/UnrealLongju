/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Mobs/MT2MobSpawnActor.h"

#include "Components/SceneComponent.h"
#include "Mobs/MT2MobSpawnComponent.h"

AMT2MobSpawnActor::AMT2MobSpawnActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	bNetLoadOnClient = false;
#if WITH_EDITOR
	SetIsSpatiallyLoaded(false);
#endif
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	MobSpawnComponent = CreateDefaultSubobject<UMT2MobSpawnComponent>(TEXT("MobSpawnComponent"));
}
