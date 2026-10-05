/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Effects/MT2HitEffectActor.h"

#include "Components/BillboardComponent.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"

namespace
{
	TArray<UTexture2D*> LoadDefaultHitEffectFrames()
	{
		static TArray<TWeakObjectPtr<UTexture2D>> CachedFrames;
		static bool bInitialized = false;
		if (!bInitialized)
		{
			bInitialized = true;
			static const TCHAR* FramePaths[] = {
				TEXT("/Game/ymir_work/effect/monster2/T_impact1.T_impact1"),
				TEXT("/Game/ymir_work/effect/monster2/T_impact2.T_impact2"),
				TEXT("/Game/ymir_work/effect/monster2/T_impact3.T_impact3"),
			};
			for (const TCHAR* Path : FramePaths)
			{
				if (UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, Path))
				{
					CachedFrames.Add(Texture);
				}
			}
		}

		TArray<UTexture2D*> Result;
		for (const TWeakObjectPtr<UTexture2D>& Frame : CachedFrames)
		{
			if (UTexture2D* Texture = Frame.Get())
			{
				Result.Add(Texture);
			}
		}
		return Result;
	}
}

AMT2HitEffectActor::AMT2HitEffectActor()
{
	PrimaryActorTick.bCanEverTick = true;
	SetActorTickEnabled(false);
	bReplicates = false;

	Billboard = CreateDefaultSubobject<UBillboardComponent>(TEXT("Billboard"));
	RootComponent = Billboard;
}

void AMT2HitEffectActor::SpawnDefaultHitEffect(UWorld* World, const FVector& Location)
{
	if (!World)
	{
		return;
	}

	const TArray<UTexture2D*> Frames = LoadDefaultHitEffectFrames();
	if (Frames.IsEmpty())
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AMT2HitEffectActor* Effect = World->SpawnActor<AMT2HitEffectActor>(Location, FRotator::ZeroRotator, SpawnParams))
	{
		Effect->PlayFrames(Frames, 0.05f, 40.0f);
	}
}

void AMT2HitEffectActor::PlayFrames(const TArray<UTexture2D*>& Frames, float FrameDuration, float Size)
{
	FrameTextures.Reset();
	for (UTexture2D* Frame : Frames)
	{
		if (Frame)
		{
			FrameTextures.Add(Frame);
		}
	}
	if (FrameTextures.IsEmpty())
	{
		Destroy();
		return;
	}

	SecondsPerFrame = FMath::Max(FrameDuration, 0.01f);
	CurrentFrameIndex = 0;
	ElapsedTime = 0.0f;
	Billboard->SetRelativeScale3D(FVector(Size / 32.0f));
	Billboard->SetSprite(FrameTextures[0]);
	SetActorTickEnabled(true);
	SetLifeSpan(SecondsPerFrame * FrameTextures.Num() + 0.15f);
}

void AMT2HitEffectActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	ElapsedTime += DeltaSeconds;
	const int32 NextFrameIndex = FMath::Min(FMath::FloorToInt(ElapsedTime / SecondsPerFrame), FrameTextures.Num() - 1);
	if (NextFrameIndex != CurrentFrameIndex)
	{
		CurrentFrameIndex = NextFrameIndex;
		Billboard->SetSprite(FrameTextures[CurrentFrameIndex]);
	}
}
