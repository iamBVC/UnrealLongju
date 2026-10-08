/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Characters/MT2CharacterBase.h"
#include "Combat/MT2KnockbackRootMotion.h"
#include "Characters/MT2CharacterMovementComponent.h"

#include "Animation/MT2CharacterAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Characters/MT2CharacterAppearanceComponent.h"
#include "Combat/MT2CombatComponent.h"
#include "Stats/MT2CombatStatsComponent.h"
#include "Components/MT2HealthComponent.h"
#include "Components/MT2ManaComponent.h"
#include "Components/MT2StaminaComponent.h"
#include "Components/MT2MovementSpeedComponent.h"
#include "Components/MT2StatusEffectComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Equipment/MT2EquipmentComponent.h"
#include "Effects/MT2MotionEffectComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/RootMotionSource.h"
#include "Config/MT2GameplaySettings.h"
#include "Net/UnrealNetwork.h"
#include "UI/MT2NameplateComponent.h"

AMT2CharacterBase::AMT2CharacterBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UMT2CharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickMontagesWhenNotRendered;
	GetMesh()->bEnableUpdateRateOptimizations = true;
	GetMesh()->SetCullDistance(20000.0f);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GetMesh()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

	CombatComponent = CreateDefaultSubobject<UMT2CombatComponent>(TEXT("CombatComponent"));
	CombatStatsComponent = CreateDefaultSubobject<UMT2CombatStatsComponent>(TEXT("CombatStatsComponent"));
	AppearanceComponent = CreateDefaultSubobject<UMT2CharacterAppearanceComponent>(TEXT("AppearanceComponent"));
	AppearanceComponent->SetMeshComponent(GetMesh());
	HealthComponent = CreateDefaultSubobject<UMT2HealthComponent>(TEXT("HealthComponent"));
	ManaComponent = CreateDefaultSubobject<UMT2ManaComponent>(TEXT("ManaComponent"));
	StaminaComponent = CreateDefaultSubobject<UMT2StaminaComponent>(TEXT("StaminaComponent"));
	MovementSpeedComponent = CreateDefaultSubobject<UMT2MovementSpeedComponent>(TEXT("MovementSpeedComponent"));

	HairMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("HairMeshComponent"));
	HairMeshComponent->SetupAttachment(GetMesh());
	HairMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HairMeshComponent->SetGenerateOverlapEvents(false);
	HairMeshComponent->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickMontagesWhenNotRendered;
	HairMeshComponent->bEnableUpdateRateOptimizations = true;
	HairMeshComponent->SetCullDistance(10000.0f);
	HairMeshComponent->SetVisibility(false, true);
	HairMeshComponent->SetComponentTickEnabled(false);

	WeaponMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMeshComponent"));
	WeaponMeshComponent->SetupAttachment(GetMesh());
	WeaponMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponMeshComponent->SetGenerateOverlapEvents(false);
	WeaponMeshComponent->SetCullDistance(10000.0f);
	WeaponMeshComponent->SetVisibility(false, true);

	LeftWeaponMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LeftWeaponMeshComponent"));
	LeftWeaponMeshComponent->SetupAttachment(GetMesh());
	LeftWeaponMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LeftWeaponMeshComponent->SetGenerateOverlapEvents(false);
	LeftWeaponMeshComponent->SetCullDistance(10000.0f);
	LeftWeaponMeshComponent->SetVisibility(false, true);

	EquipmentComponent = CreateDefaultSubobject<UMT2EquipmentComponent>(TEXT("EquipmentComponent"));
	EquipmentComponent->SetVisualComponents(
		GetMesh(), HairMeshComponent, WeaponMeshComponent, LeftWeaponMeshComponent);

	StatusEffectComponent = CreateDefaultSubobject<UMT2StatusEffectComponent>(TEXT("StatusEffectComponent"));
	MotionEffectComponent = CreateDefaultSubobject<UMT2MotionEffectComponent>(TEXT("MotionEffectComponent"));

	NameplateComponent = CreateDefaultSubobject<UMT2NameplateComponent>(TEXT("NameplateComponent"));
	NameplateComponent->SetupAttachment(GetRootComponent());
}

void AMT2CharacterBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMT2CharacterBase, bWalkRequested);
	DOREPLIFETIME(AMT2CharacterBase, AnimationSet);
}

void AMT2CharacterBase::SetWalkRequested(bool bNewWalkRequested)
{
	if (bWalkRequested == bNewWalkRequested)
	{
		return;
	}

	bWalkRequested = bNewWalkRequested;
	MovementSpeedComponent->RefreshMovementSpeed();
}

void AMT2CharacterBase::OnRep_WalkRequested()
{
	MovementSpeedComponent->RefreshMovementSpeed();
}

void AMT2CharacterBase::SetAnimationSet(FName NewAnimationSet)
{
	AnimationSet = NewAnimationSet.IsNone() ? FName(TEXT("general")) : NewAnimationSet;
	ApplyAnimationSetToInstance();
}

void AMT2CharacterBase::SetWeaponAnimationSet(int32 WeaponSubType, bool bMounted)
{
	static const FName WeaponSets[] = {
		TEXT("onehand_sword"), TEXT("dualhand_sword"), TEXT("bow"), TEXT("twohand_sword"),
		TEXT("bell"), TEXT("fan")
	};
	FName Set = (WeaponSubType >= 0 && WeaponSubType < UE_ARRAY_COUNT(WeaponSets))
		? WeaponSets[WeaponSubType] : FName(TEXT("general"));
	if (bMounted)
	{
		Set = Set == TEXT("general")
			? FName(TEXT("horse"))
			: FName(*(FString(TEXT("horse_")) + Set.ToString()));
	}

	// playersettingmodule.py registers one unarmed/bow attack, four ground melee attacks, and three
	// mounted melee attacks. CActorInstance::__OnEndCombo then keeps mounted chains at index 1 so the
	// opening swing is not replayed on every loop.
	const bool bBow = WeaponSubType == 2;
	const bool bMeleeWeapon = WeaponSubType >= 0 && WeaponSubType < UE_ARRAY_COUNT(WeaponSets) && !bBow;
	const int32 ComboLength = bMounted ? (bMeleeWeapon ? 3 : 1) : (bMeleeWeapon ? 4 : 1);
	CombatComponent->ConfigureBasicAttackCombo(ComboLength, bMounted && ComboLength > 1 ? 1 : 0);
	SetAnimationSet(Set);
}

void AMT2CharacterBase::PlayReplicatedAnimationAction(
	FName Action, int32 VariantIndex, float PlayRate, FName AnimationSetOverride,
	float MovementLockDuration, bool bOwnerPredicted)
{
	if (!HasAuthority() || Action.IsNone())
	{
		return;
	}
	const FName SetName = AnimationSetOverride.IsNone() ? AnimationSet : AnimationSetOverride;
	MulticastPlayAnimationAction(
		SetName, Action, VariantIndex, FMath::Max(PlayRate, 0.01f),
		FMath::Max(MovementLockDuration, 0.0f), bOwnerPredicted);
}

void AMT2CharacterBase::MulticastPlayAnimationAction_Implementation(
	FName AnimationSetName, FName Action, int32 VariantIndex, float PlayRate,
	float MovementLockDuration, bool bOwnerPredicted)
{
	// A predicted autonomous proxy already started this montage immediately when input occurred.
	if (bOwnerPredicted && GetNetMode() == NM_Client && IsLocallyControlled())
	{
		return;
	}
	if (GetNetMode() == NM_DedicatedServer || !GetMesh())
	{
		return;
	}

	UMT2CharacterAnimInstance* AnimInstance = Cast<UMT2CharacterAnimInstance>(GetMesh()->GetAnimInstance());
	if (!AnimInstance)
	{
		return;
	}
	UAnimSequence* Sequence = Action == TEXT("combo")
		? AnimInstance->GetComboAnimation(AnimationSetName, VariantIndex)
		: AnimInstance->GetAnimation(AnimationSetName, Action, VariantIndex);
	if (!Sequence)
	{
		return;
	}

	Sequence->bEnableRootMotion = false;
	Sequence->RootMotionRootLock = ERootMotionRootLock::RefPose;
	Sequence->bForceRootLock = false;
	AnimInstance->PlaySlotAnimationAsDynamicMontage(
		Sequence, TEXT("DefaultSlot"), 0.2f, 0.2f, FMath::Max(PlayRate, 0.01f), 1, -1.0f, 0.0f);
	MotionEffectComponent->PlayMotionEffects(Sequence, PlayRate);

	if (MovementLockDuration > UE_SMALL_NUMBER)
	{
		GetCharacterMovement()->bOrientRotationToMovement = false;
		GetWorldTimerManager().SetTimer(
			ReplicatedActionMovementTimer, this,
			&AMT2CharacterBase::RestoreMovementOrientationAfterAction,
			MovementLockDuration, false);
	}
}

void AMT2CharacterBase::RestoreMovementOrientationAfterAction()
{
	GetCharacterMovement()->bOrientRotationToMovement = true;
}

void AMT2CharacterBase::OnRep_AnimationSet()
{
	ApplyAnimationSetToInstance();
}

void AMT2CharacterBase::ApplyAnimationSetToInstance()
{
	if (USkeletalMeshComponent* MeshComponent = GetMesh())
	{
		if (UMT2CharacterAnimInstance* AnimInstance = Cast<UMT2CharacterAnimInstance>(MeshComponent->GetAnimInstance()))
		{
			AnimInstance->SetAnimationSet(AnimationSet);
		}
	}
}

void AMT2CharacterBase::InitializeAbilityComponents(UAbilitySystemComponent* AbilitySystemComponent)
{
	HealthComponent->InitializeWithAbilitySystem(AbilitySystemComponent);
	ManaComponent->InitializeWithAbilitySystem(AbilitySystemComponent);
	StaminaComponent->InitializeWithAbilitySystem(AbilitySystemComponent);
	MovementSpeedComponent->InitializeWithAbilitySystem(AbilitySystemComponent);
}

bool AMT2CharacterBase::IsHostileTo(const AActor* Other) const
{
	const AMT2CharacterBase* OtherCharacter = Cast<AMT2CharacterBase>(Other);
	if (!OtherCharacter || OtherCharacter == this || !OtherCharacter->IsAttackable())
	{
		return false;
	}
	// Player hostility is directional and is decided by the authoritative PvP policy.
	if (IsPlayerFaction() && OtherCharacter->IsPlayerFaction())
	{
		return IsPvPEnabledAgainst(OtherCharacter);
	}
	return OtherCharacter->IsPlayerFaction() != IsPlayerFaction();
}

void AMT2CharacterBase::ApplyKnockback(const AActor* KnockbackInstigator, float Distance, float Duration, bool bSideways)
{
	// Old char_skill.cpp CRUSH: victims flagged NOMOVE never slide; the push runs straight along
	// the attacker->victim line (GetDegreeFromPositionXY) for the given sliding length.
	if (!HasAuthority() || !IsValid(KnockbackInstigator) || !FMath::IsFinite(Distance) || Distance <= 0.0f ||
		!CanBeKnockedBack() || GetHealthComponent()->IsDead())
	{
		return;
	}
	FVector Direction = GetActorLocation() - KnockbackInstigator->GetActorLocation();
	Direction.Z = 0.0;
	if (Direction.IsNearlyZero())
	{
		Direction = KnockbackInstigator->GetActorForwardVector();
		Direction.Z = 0.0;
	}
	if (bSideways)
	{
		const FVector Right = KnockbackInstigator->GetActorRightVector().GetSafeNormal2D();
		Direction = FVector::DotProduct(Direction, Right) >= 0 ? Right : -Right;
	}
	// ActorInstance.h: SetBlendingPosition defaults to one second for server sync pushes.
	const float SlideDuration = Duration > 0 ? Duration : 1.f;
	if (!FMath::IsFinite(SlideDuration) || SlideDuration < .05f || SlideDuration > 5.f) { return; }
	StartKnockback(Direction.GetSafeNormal2D(), Distance, SlideDuration);
	MulticastKnockback(Direction.GetSafeNormal2D(), Distance, SlideDuration);
	ForceNetUpdate();
}

void AMT2CharacterBase::MulticastKnockback_Implementation(
	FVector_NetQuantizeNormal Direction, float Distance, float Duration)
{
	// Authoritative movement never depends on RPC delivery or local multicast dispatch.
	if (!HasAuthority()) { StartKnockback(Direction, Distance, Duration); }
}

void AMT2CharacterBase::StartKnockback(const FVector& Direction, float Distance, float Duration)
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Movement || Direction.IsNearlyZero() ||
		!FMath::IsFinite(Distance) || Distance <= 0 || !FMath::IsFinite(Duration) || Duration < .05f || Duration > 5.f ||
		!CanBeKnockedBack() || GetHealthComponent()->IsDead())
	{
		return;
	}
	Movement->SetComponentTickEnabled(true);
	if (!HasAuthority() && !IsLocallyControlled()) { return; }
	GetHealthComponent()->OnDeath.AddUniqueDynamic(this, &AMT2CharacterBase::ClearKnockbackOnDeath);
	// Both authority and autonomous owner use UE's root-motion-source prediction/correction.
	// Simulated proxies follow replicated movement, never run a second local launch.
	const FName SourceName(TEXT("MT2Knockback"));
	Movement->RemoveRootMotionSource(SourceName);
	auto Source = MakeShared<FMT2KnockbackRootMotion>();
	Source->InstanceName = SourceName;
	Source->Priority = 1000;
	Source->AccumulateMode = ERootMotionAccumulateMode::Override;
	Source->Duration = Duration;
	Source->Force = FVector(Direction).GetSafeNormal2D() * (Distance / Duration);
	Source->Settings.SetFlag(ERootMotionSourceSettingsFlags::IgnoreZAccumulate);
	Source->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::SetVelocity;
	Source->FinishVelocityParams.SetVelocity = FVector::ZeroVector;
	Movement->ApplyRootMotionSource(Source);
}

void AMT2CharacterBase::ClearKnockbackOnDeath()
{
	if (auto* Movement = GetCharacterMovement())
	{
		Movement->RemoveRootMotionSource(FName(TEXT("MT2Knockback")));
	}
}
