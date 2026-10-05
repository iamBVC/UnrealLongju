/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Skills/MT2SkillVisualEffectComponent.h"

#include "Characters/MT2CharacterBase.h"
#include "Characters/MT2PlayerCharacter.h"
#include "Components/MT2StatusEffectComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Equipment/MT2EquipmentComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "Player/MT2PlayerState.h"
#include "Skills/MT2SkillComponent.h"
#include "Skills/MT2SkillDefinition.h"

namespace
{
	int64 MakeVisualKey(int32 SkillVnum, int32 VisualIndex)
	{
		return (static_cast<int64>(SkillVnum) << 32) | static_cast<uint32>(VisualIndex);
	}

	bool MatchesWeaponMode(
		EMT2SkillVisualWeaponMode RequiredMode, const UMT2EquipmentComponent* Equipment)
	{
		if (RequiredMode == EMT2SkillVisualWeaponMode::Any)
		{
			return true;
		}
		const bool bTwoHanded = Equipment && Equipment->GetEquippedWeaponSubType() == 3;
		return RequiredMode == EMT2SkillVisualWeaponMode::TwoHanded
			? bTwoHanded : !bTwoHanded;
	}
}

UMT2SkillVisualEffectComponent::UMT2SkillVisualEffectComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(false);
}

void UMT2SkillVisualEffectComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (PendingLoadHandle.IsValid())
	{
		PendingLoadHandle->CancelHandle();
		PendingLoadHandle.Reset();
	}
	ClearEffects();
	Super::EndPlay(EndPlayReason);
}

void UMT2SkillVisualEffectComponent::RefreshEffects()
{
	AMT2PlayerCharacter* Character = Cast<AMT2PlayerCharacter>(GetOwner());
	if (!Character || Character->GetNetMode() == NM_DedicatedServer)
	{
		ClearEffects();
		return;
	}

	const UMT2StatusEffectComponent* StatusEffects = Character->GetStatusEffectComponent();
	const AMT2PlayerState* PlayerState = Character->GetPlayerState<AMT2PlayerState>();
	const UMT2SkillComponent* Skills = PlayerState ? PlayerState->GetSkillComponent() : nullptr;
	const UMT2EquipmentComponent* Equipment = Character->GetEquipmentComponent();
	if (!StatusEffects || !Skills)
	{
		ClearEffects();
		return;
	}

	struct FDesiredVisual
	{
		int64 Key = 0;
		const FMT2SkillPersistentVisual* Visual = nullptr;
	};
	TArray<FDesiredVisual> DesiredVisuals;
	TSet<int64> DesiredKeys;
	TArray<FSoftObjectPath> MissingAssets;
	uint32 DesiredHash = 0;

	TSet<int32> ProcessedSkills;
	for (const FMT2StatusEffect& StatusEffect : StatusEffects->GetEffects())
	{
		if (StatusEffect.Type <= 0 || ProcessedSkills.Contains(StatusEffect.Type))
		{
			continue;
		}
		ProcessedSkills.Add(StatusEffect.Type);

		const UMT2SkillDefinition* Definition = Skills->FindSkillDefinition(StatusEffect.Type);
		if (!Definition)
		{
			continue;
		}
		for (int32 Index = 0; Index < Definition->PersistentVisuals.Num(); ++Index)
		{
			const FMT2SkillPersistentVisual& Visual = Definition->PersistentVisuals[Index];
			if (Visual.Effect.IsNull() || !MatchesWeaponMode(Visual.WeaponMode, Equipment))
			{
				continue;
			}
			const int64 Key = MakeVisualKey(Definition->Vnum, Index);
			DesiredVisuals.Add({Key, &Visual});
			DesiredKeys.Add(Key);
			DesiredHash = HashCombine(DesiredHash, GetTypeHash(Visual.Effect.ToSoftObjectPath()));
			if (!Visual.Effect.Get())
			{
				MissingAssets.AddUnique(Visual.Effect.ToSoftObjectPath());
			}
		}
	}

	for (auto It = ActiveEffects.CreateIterator(); It; ++It)
	{
		if (!DesiredKeys.Contains(It.Key()))
		{
			if (UParticleSystemComponent* Effect = It.Value())
			{
				Effect->DeactivateSystem();
				Effect->DestroyComponent();
			}
			MovementOnlyEffects.Remove(It.Key());
			It.RemoveCurrent();
		}
	}

	if (!MissingAssets.IsEmpty())
	{
		if (!PendingLoadHandle.IsValid() || PendingLoadHash != DesiredHash)
		{
			if (PendingLoadHandle.IsValid())
			{
				PendingLoadHandle->CancelHandle();
			}
			PendingLoadHash = DesiredHash;
			PendingLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
				MissingAssets,
				FStreamableDelegate::CreateUObject(
					this, &UMT2SkillVisualEffectComponent::FinishPendingLoad, DesiredHash));
		}
	}
	else
	{
		PendingLoadHandle.Reset();
	}

	for (const FDesiredVisual& Desired : DesiredVisuals)
	{
		if (ActiveEffects.Contains(Desired.Key) || !Desired.Visual)
		{
			continue;
		}
		UParticleSystem* Template = Desired.Visual->Effect.Get();
		USceneComponent* Parent = Desired.Visual->Target == EMT2SkillVisualTarget::WeaponMesh
			? static_cast<USceneComponent*>(Equipment ? Equipment->GetWeaponMeshComponent() : nullptr)
			: static_cast<USceneComponent*>(Character->GetMesh());
		if (!Template || !Parent)
		{
			continue;
		}

		const FTransform& Relative = Desired.Visual->RelativeTransform;
		UParticleSystemComponent* Effect = UGameplayStatics::SpawnEmitterAttached(
			Template, Parent, Desired.Visual->AttachSocket,
			Relative.GetLocation(), Relative.Rotator(), Relative.GetScale3D(),
			EAttachLocation::KeepRelativeOffset, false, EPSCPoolMethod::None, true);
		if (!Effect)
		{
			continue;
		}
		ActiveEffects.Add(Desired.Key, Effect);
		if (Desired.Visual->bOnlyWhileMoving)
		{
			MovementOnlyEffects.Add(Desired.Key);
		}
	}

	SetComponentTickEnabled(!MovementOnlyEffects.IsEmpty());
	if (!MovementOnlyEffects.IsEmpty())
	{
		TickComponent(0.0f, LEVELTICK_All, nullptr);
	}
}

void UMT2SkillVisualEffectComponent::FinishPendingLoad(uint32 ExpectedHash)
{
	if (PendingLoadHash != ExpectedHash)
	{
		return;
	}
	PendingLoadHandle.Reset();
	RefreshEffects();
}

void UMT2SkillVisualEffectComponent::TickComponent(
	float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const bool bShouldPlay = IsCharacterMoving();
	for (const int64 Key : MovementOnlyEffects)
	{
		const TObjectPtr<UParticleSystemComponent>* Found = ActiveEffects.Find(Key);
		UParticleSystemComponent* Effect = Found ? Found->Get() : nullptr;
		if (!Effect || Effect->IsActive() == bShouldPlay)
		{
			continue;
		}
		if (bShouldPlay)
		{
			Effect->ActivateSystem(true);
		}
		else
		{
			Effect->DeactivateSystem();
		}
	}
}

bool UMT2SkillVisualEffectComponent::IsCharacterMoving() const
{
	const AMT2CharacterBase* Character = Cast<AMT2CharacterBase>(GetOwner());
	return Character && Character->GetVelocity().SizeSquared2D() > 1.0f;
}

void UMT2SkillVisualEffectComponent::ClearEffects()
{
	for (TPair<int64, TObjectPtr<UParticleSystemComponent>>& Pair : ActiveEffects)
	{
		if (UParticleSystemComponent* Effect = Pair.Value)
		{
			Effect->DeactivateSystem();
			Effect->DestroyComponent();
		}
	}
	ActiveEffects.Reset();
	MovementOnlyEffects.Reset();
	SetComponentTickEnabled(false);
}
