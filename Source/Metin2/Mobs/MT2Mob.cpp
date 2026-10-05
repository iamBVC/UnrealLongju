/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Mobs/MT2Mob.h"
#include "Loot/MT2LootTable.h"

#include "AIController.h"
#include "Abilities/MT2CoreAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/MT2MobAnimInstance.h"
#include "Combat/MT2CombatComponent.h"
#include "Core/MT2VnumRegistrySubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Components/CapsuleComponent.h"
#include "Components/MT2HealthComponent.h"
#include "Components/MT2ActorRelevanceComponent.h"
#include "Components/MT2MovementSpeedComponent.h"
#include "Effects/MT2HitEffectActor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Mobs/MT2MobAIComponent.h"
#include "Quests/MT2QuestManagerComponent.h"
#include "Characters/MT2PlayerCharacter.h"
#include "Player/MT2PlayerState.h"
#include "Mobs/MT2MobAIController.h"
#include "Mobs/MT2MetinStone.h"
#include "Mobs/MT2MobLootComponent.h"
#include "Mobs/MT2MobLifecycleComponent.h"
#include "Mobs/MT2MobMovementComponent.h"
#include "Mobs/MT2MobRuntimeSettings.h"
#include "Net/UnrealNetwork.h"
#include "Skills/MT2SkillComponent.h"
#include "Stats/MT2CombatStatsComponent.h"
#include "Stats/MT2PrimaryStatsComponent.h"
#include "TimerManager.h"
#include "World/MT2WorldSimulationSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogMT2MobMotion, Log, All);

AMT2Mob::AMT2Mob(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UMT2MobMovementComponent>(
		ACharacter::CharacterMovementComponentName))
{
	const UMT2MobRuntimeSettings* Settings = GetDefault<UMT2MobRuntimeSettings>();
	// Ticks only to keep facing an attack target (see Tick); the AI itself runs on a timer.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	SetNetUpdateFrequency(Settings->InitialReplicationRate);
	SetMinNetUpdateFrequency(Settings->MinimumReplicationRate);
	SetNetCullDistanceSquared(FMath::Square(Settings->NetCullDistance));

	RelevanceComponent = CreateDefaultSubobject<UMT2ActorRelevanceComponent>(TEXT("RelevanceComponent"));

	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);
	CoreAttributes = CreateDefaultSubobject<UMT2CoreAttributeSet>(TEXT("CoreAttributes"));
	MobAIComponent = CreateDefaultSubobject<UMT2MobAIComponent>(TEXT("MobAIComponent"));
	LootComponent = CreateDefaultSubobject<UMT2MobLootComponent>(TEXT("LootComponent"));
	PrimaryStatsComponent = CreateDefaultSubobject<UMT2PrimaryStatsComponent>(TEXT("PrimaryStatsComponent"));
	LifecycleComponent = CreateDefaultSubobject<UMT2MobLifecycleComponent>(TEXT("LifecycleComponent"));
	SkillComponent = CreateDefaultSubobject<UMT2SkillComponent>(TEXT("SkillComponent"));

	AIControllerClass = AMT2MobAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, Settings->RotationRate, 0.0f);

	// Same as the default "Pawn" profile, but ignores other Pawns and the camera and blocks other
	// mobs instead - see the "Mob" profile in Config/DefaultEngine.ini.
	GetCapsuleComponent()->SetCollisionProfileName(TEXT("Mob"));
	// Click-to-target traces with bTraceComplex=true (see AMT2PlayerCharacter::RequestClickMove),
	// which needs per-poly complex collision on the mesh to register a hit - imported skeletal meshes
	// don't have that enabled by default, so the capsule (a simple shape, always queried regardless of
	// complex/simple trace mode) is the reliable thing to make clickable. The "Mob" profile otherwise
	// mirrors "Pawn", which ignores Visibility entirely; override just that one channel back to Block.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);
	GetCapsuleComponent()->SetGenerateOverlapEvents(false);
	GetCharacterMovement()->bEnablePhysicsInteraction = false;
	GetCharacterMovement()->bUseRVOAvoidance = false;
	GetCharacterMovement()->bAlwaysCheckFloor = false;
	GetCharacterMovement()->bUseFlatBaseForFloorChecks = true;

	GetMesh()->SetRelativeLocation(FVector(0.0f, 0.0f, -90.0f));
	// Character skeletal assets are imported with a 180-degree yaw so they face forward
	// in the asset editor. Preserve the previous in-world facing by compensating here.
	GetMesh()->SetRelativeRotation(FRotator(0.0f, 90.0f, 0.0f));
	// The capsule owns movement, targeting and combat collision. Per-poly skeletal collision adds
	// physics-body synchronization and scene-query work for every animated mob without adding useful
	// gameplay precision.
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->SetGenerateOverlapEvents(false);
}

UAbilitySystemComponent* AMT2Mob::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AMT2Mob::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMT2Mob, MobVnum);
	DOREPLIFETIME(AMT2Mob, MobLevel);
	DOREPLIFETIME(AMT2Mob, Empire);
	DOREPLIFETIME(AMT2Mob, ReplicatedMotion);
	DOREPLIFETIME(AMT2Mob, MotionSerial);
}

void AMT2Mob::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyVisualConfiguration();
}

void AMT2Mob::BeginPlay()
{
	Super::BeginPlay();

	AbilitySystemComponent->InitAbilityActorInfo(this, this);
	InitializeAbilityComponents(AbilitySystemComponent);
	GetHealthComponent()->OnDeath.AddUniqueDynamic(this, &AMT2Mob::HandleDeath);
	GetHealthComponent()->OnRevived.AddUniqueDynamic(this, &AMT2Mob::HandleRevived);
	MobAIComponent->OnStateChanged.AddUniqueDynamic(this, &AMT2Mob::HandleAIStateChanged);
	// Blueprint component defaults can override the native CDO, so enforce this again at runtime.
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->SetGenerateOverlapEvents(false);
	if (GetNetMode() == NM_DedicatedServer)
	{
		GetMesh()->bNoSkeletonUpdate = true;
		GetMesh()->SetComponentTickEnabled(false);
	}
	if (UMT2WorldSimulationSubsystem* Simulation = GetWorld()->GetSubsystem<UMT2WorldSimulationSubsystem>())
	{
		Simulation->RegisterMob(this);
	}

	if (HasAuthority())
	{
		ApplyStatsToRuntimeState();
		MobAIComponent->InitializeHome(GetActorLocation());
		LifecycleComponent->StartRegeneration(GetHealthComponent());
		if (IsStationaryNpc())
		{
			SetReplicateMovement(false);
			SetNetDormancy(DORM_DormantAll);
		}
	}
	else
	{
		// Clients get all mob data as class defaults from the Blueprint - only the derived visual
		// state (capsule size, mesh scale) needs applying.
		ApplyVisualConfiguration();
	}
}

void AMT2Mob::HandleAIStateChanged(EMT2MobAIState, EMT2MobAIState NewState)
{
	SetActorTickEnabled(NewState == EMT2MobAIState::Attacking);
}

bool AMT2Mob::IsStationaryNpc() const
{
	return MobType == EMT2MobType::NPC || MobType == EMT2MobType::Warp ||
		MobType == EMT2MobType::Goto;
}

void AMT2Mob::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UMT2QuestManagerComponent* Manager = QuestConversationOwner.Get())
	{
		Manager->NotifyQuestNpcRemoved(this);
	}
	QuestConversationOwner.Reset();
	QuestConversationPawn.Reset();
	if (UWorld* World = GetWorld())
	{
		if (UMT2WorldSimulationSubsystem* Simulation = World->GetSubsystem<UMT2WorldSimulationSubsystem>())
		{
			Simulation->UnregisterMob(this);
		}
	}
	GetWorldTimerManager().ClearTimer(AttackCooldownTimer);
	GetWorldTimerManager().ClearTimer(CombatMotionLockTimer);
	GetWorldTimerManager().ClearTimer(DeathAnimFreezeTimer);
	Super::EndPlay(EndPlayReason);
}

bool AMT2Mob::TryAcquireQuestConversation(UMT2QuestManagerComponent* Manager, AMT2PlayerCharacter* Player)
{
	check(IsInGameThread());
	const AMT2PlayerState* State = Manager ? Cast<AMT2PlayerState>(Manager->GetOwner()) : nullptr;
	if (!HasAuthority() || !IsValid(Manager) || !IsValid(Player) || !IsValid(State) || !State->HasAuthority() ||
		!Player->HasAuthority() || Player->GetPlayerState() != State || State->GetPawn() != Player ||
		Player->GetWorld() != GetWorld()) { return false; }
	UMT2QuestManagerComponent* Previous = QuestConversationOwner.Get();
	const AMT2PlayerCharacter* PreviousPawn = QuestConversationPawn.Get();
	const AMT2PlayerState* PreviousState = Previous ? Cast<AMT2PlayerState>(Previous->GetOwner()) : nullptr;
	if (Previous && PreviousPawn && IsValid(PreviousState) && PreviousState->GetPawn() == PreviousPawn &&
		PreviousPawn->GetPlayerState() == PreviousState && Previous != Manager) { return false; }
	QuestConversationOwner = Manager;
	QuestConversationPawn = Player;
	return true;
}

void AMT2Mob::ReleaseQuestConversation(UMT2QuestManagerComponent* Manager)
{
	check(IsInGameThread());
	if (HasAuthority() && QuestConversationOwner.Get() == Manager)
	{
		QuestConversationOwner.Reset();
		QuestConversationPawn.Reset();
	}
}

void AMT2Mob::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Keep facing the attack target every frame while in the Attacking state, so the mob tracks a player
	// strafing around it mid-swing. This runs on the server AND on clients (State and TargetActor are both
	// replicated), which is what makes the turn actually show: bOrientRotationToMovement otherwise derives
	// a simulated proxy's rotation from velocity, and the mob is stopped while attacking. Yaw only - a
	// character capsule must never pitch/roll toward a target above or below it.
	if (!MobAIComponent || MobAIComponent->GetState() != EMT2MobAIState::Attacking)
	{
		return;
	}
	const AActor* Target = MobAIComponent->GetTargetActor();
	if (!Target)
	{
		return;
	}
	const FVector ToTarget = Target->GetActorLocation() - GetActorLocation();
	if (ToTarget.IsNearlyZero())
	{
		return;
	}
	FRotator Desired = GetActorRotation();
	Desired.Yaw = ToTarget.Rotation().Yaw;
	const float InterpSpeed = GetDefault<UMT2MobRuntimeSettings>()->AttackFacingInterpolationSpeed;
	const FRotator NewRotation = FMath::RInterpTo(GetActorRotation(), Desired, DeltaSeconds, InterpSpeed);
	SetActorRotation(FRotator(0.0f, NewRotation.Yaw, 0.0f));
}

void AMT2Mob::ConfigureFromDefinition(const FMT2MobDefinition& Definition)
{
	MobVnum = Definition.Vnum;
	InternalName = Definition.InternalName;
	DisplayName = Definition.DisplayName;
	ResourceName = Definition.ResourceName;
	SourceFolder = Definition.SourceFolder;
	Rank = Definition.Rank;
	MobType = Definition.Type;
	MobSize = Definition.Size;

	// Full-map NPC markers use a one-shot server snapshot. NPC actors therefore use the same bounded
	// network relevancy as every other mob instead of remaining replicated across the entire map.
	const UMT2MobRuntimeSettings* Settings = GetDefault<UMT2MobRuntimeSettings>();
	SetNetCullDistanceSquared(FMath::Square(Settings->NetCullDistance));
	if (IsStationaryNpc())
	{
		SetNetUpdateFrequency(Settings->IdleReplicationRate);
		SetMinNetUpdateFrequency(Settings->MinimumReplicationRate);
		SetReplicateMovement(false);
	}
	MobLevel = FMath::Max(Definition.Level, 1);
	Empire = Definition.Empire;
	ScalePercent = FMath::Max(Definition.ScalePercent, 1);
	RaceFlags = Definition.RaceFlags;
	ImmuneFlags = Definition.ImmuneFlags;
	BattleType = Definition.BattleType;
	HitRange = Definition.HitRange;
	Enchants = Definition.Enchants;
	Resistances = Definition.Resistances;
	ElementalAttackValues = Definition.ElementalAttackValues;
	MobSkills = Definition.Skills;
	BerserkHealthPercent = Definition.BerserkHealthPercent;
	StoneSkinHealthPercent = Definition.StoneSkinHealthPercent;
	GodSpeedHealthPercent = Definition.GodSpeedHealthPercent;
	DeathBlowHealthPercent = Definition.DeathBlowHealthPercent;
	ReviveHealthPercent = Definition.ReviveHealthPercent;

	FMT2PrimaryStats PrimaryStats;
	PrimaryStats.Strength = Definition.Strength;
	PrimaryStats.Dexterity = Definition.Dexterity;
	PrimaryStats.Constitution = Definition.Constitution;
	PrimaryStats.Intelligence = Definition.Intelligence;
	PrimaryStatsComponent->ConfigureBaseStats(PrimaryStats);

	FMT2CombatStats CombatStats;
	CombatStats.Defense = Definition.Defense;
	CombatStats.DamageMin = Definition.DamageMin;
	CombatStats.DamageMax = Definition.DamageMax;
	CombatStats.DamageMultiplier = Definition.DamageMultiplier;
	CombatStats.AttackSpeed = Definition.AttackSpeed;
	CombatStats.MovementSpeed = Definition.MovementSpeed;
	CombatStats.AttackRange = Definition.AttackRange;
	CombatStats.MaxHealth = Definition.MaxHealth;
	GetCombatStatsComponent()->ConfigureBaseStats(CombatStats);

	MobAIComponent->Configure(Definition);
	LootComponent->Configure(Definition);
	LifecycleComponent->Configure(Definition);
	ApplyVisualConfiguration();
}

TSubclassOf<UMT2LootTable> AMT2Mob::GetLootTable() const
{
	return LootComponent->GetLootTable();
}

void AMT2Mob::SetLootTable(TSubclassOf<UMT2LootTable> NewLootTable)
{
	LootComponent->SetLootTable(NewLootTable);
}

bool AMT2Mob::TryAttackTarget(AActor* TargetActor)
{
	if (!HasAuthority() || !bAttackReady || !TargetActor || GetHealthComponent()->IsDead())
	{
		return false;
	}

	float Interval = GetCombatStatsComponent()->GetAttackInterval();
	if (const FMT2MobMotionVariant* Variant = ChooseMotion(EMT2MobMotion::NormalAttack))
	{
		if (const UAnimSequence* Sequence = Variant->Animation.LoadSynchronous())
		{
			// Same issue as the player's basic attack (see UMT2CombatComponent::
			// NotifyAttackAnimationDuration): GetAttackInterval() is just a stats-derived floor, and
			// the mob's own attack animation (see the .msa's authored MotionDuration) commonly runs
			// longer than it, which was retriggering the montage and cutting the swing off early.
			// The animation itself plays at AttackSpeed/100 (old client model), so the cadence uses
			// the sped-up duration too.
			const float BlendOverlap = GetDefault<UMT2MobRuntimeSettings>()->AttackBlendOverlap;
			const float PlayRate = GetAttackMotionPlayRate();
			Interval = FMath::Max(Interval, Sequence->GetPlayLength() / PlayRate - BlendOverlap);
		}
	}

	bAttackReady = false;
	GetWorldTimerManager().SetTimer(
		AttackCooldownTimer,
		FTimerDelegate::CreateWeakLambda(this, [this]() { bAttackReady = true; }),
		Interval,
		false);

	PlayMobMotion(EMT2MobMotion::NormalAttack);
	return GetCombatComponent()->PerformBasicAttackOnTarget(TargetActor);
}

float AMT2Mob::GetAttackMotionPlayRate() const
{
	const UMT2CombatStatsComponent* Stats = GetCombatStatsComponent();
	return FMath::Max((Stats ? Stats->GetCalculatedStats().AttackSpeed : 100) / 100.0f, 0.1f);
}

bool AMT2Mob::PlayMobMotion(EMT2MobMotion Motion)
{
	const bool bDeathMotion = Motion == EMT2MobMotion::FrontDead || Motion == EMT2MobMotion::BackDead;
	if (GetHealthComponent()->IsDead() && !bDeathMotion)
	{
		return false;
	}

	const FMT2MobMotionVariant* Variant = ChooseMotion(Motion);
	UAnimSequence* Sequence = Variant ? Variant->Animation.LoadSynchronous() : nullptr;
	UE_LOG(LogMT2MobMotion, Verbose,
		TEXT("[MT2MobMotion] '%s' PlayMobMotion(%d) VariantFound=%s Sequence='%s'"),
		*GetNameSafe(this), static_cast<int32>(Motion),
		Variant ? TEXT("true") : TEXT("false"),
		*GetNameSafe(Sequence));
	if (!Sequence || !GetMesh())
	{
		return false;
	}

	// Turn to face the target before an attack. This runs on the server (authority path) AND on every
	// client (via OnRep_MotionSerial -> PlayMobMotion), so the mob visibly faces the player mid-swing:
	// the server's own SetActorRotation doesn't reach clients because bOrientRotationToMovement derives
	// their rotation from velocity, and the mob is movement-locked (stopped) during the swing, so the
	// yaw we set here holds. TargetActor is replicated, so clients know who to face.
	const bool bAttackMotion = Motion == EMT2MobMotion::NormalAttack ||
		(static_cast<uint8>(Motion) >= static_cast<uint8>(EMT2MobMotion::Special1) &&
		 static_cast<uint8>(Motion) <= static_cast<uint8>(EMT2MobMotion::Special5));
	if (bAttackMotion && MobAIComponent)
	{
		if (const AActor* Target = MobAIComponent->GetTargetActor())
		{
			const FVector ToTarget = Target->GetActorLocation() - GetActorLocation();
			if (!ToTarget.IsNearlyZero())
			{
				FRotator Facing = GetActorRotation();
				Facing.Yaw = ToTarget.Rotation().Yaw; // yaw only - never pitch/roll a character capsule
				SetActorRotation(Facing);
			}
		}
	}

	if (HasAuthority())
	{
		const bool bCombatMotion = Motion == EMT2MobMotion::NormalAttack ||
			(static_cast<uint8>(Motion) >= static_cast<uint8>(EMT2MobMotion::Special1) &&
			 static_cast<uint8>(Motion) <= static_cast<uint8>(EMT2MobMotion::Special5));
		if (bCombatMotion)
		{
			const float PlayRate = Motion == EMT2MobMotion::NormalAttack ? GetAttackMotionPlayRate() : 1.0f;
			LockCombatMovement(Sequence->GetPlayLength() / FMath::Max(PlayRate, 0.01f));
		}
		ReplicatedMotion = Motion;
		++MotionSerial;
		ForceNetUpdate();
	}

	if (GetNetMode() != NM_DedicatedServer)
	{
		if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
		{
			// Attack motions play at AttackSpeed/100 like the old client; other motions at 1.0.
			const float PlayRate = Motion == EMT2MobMotion::NormalAttack ? GetAttackMotionPlayRate() : 1.0f;
			const UMT2MobRuntimeSettings* Settings = GetDefault<UMT2MobRuntimeSettings>();
			UAnimMontage* Montage = AnimInstance->PlaySlotAnimationAsDynamicMontage(
				Sequence, TEXT("DefaultSlot"), Settings->MontageBlendTime,
				bDeathMotion ? 0.0f : Settings->MontageBlendTime,
				PlayRate, 1, -1.0f, 0.0f);

			if (bDeathMotion && Montage)
			{
				GetWorldTimerManager().ClearTimer(DeathAnimFreezeTimer);
				DeathAnimMontage = Montage;
				TWeakObjectPtr<UAnimInstance> WeakAnimInstance(AnimInstance);
				TWeakObjectPtr<UAnimMontage> WeakMontage(Montage);
				const float FreezeDelay = FMath::Max(
					Sequence->GetPlayLength() / FMath::Max(PlayRate, Settings->MinimumTimerDuration)
						- Settings->DeathFreezeLeadTime,
					Settings->MinimumTimerDuration);
				const float DeathPoseFrameOffset = Settings->DeathPoseFrameOffset;
				GetWorldTimerManager().SetTimer(
					DeathAnimFreezeTimer,
					FTimerDelegate::CreateWeakLambda(this, [WeakAnimInstance, WeakMontage, DeathPoseFrameOffset]()
					{
						if (WeakAnimInstance.IsValid() && WeakMontage.IsValid())
						{
							const float HoldPosition =
								FMath::Max(WeakMontage->GetPlayLength() - DeathPoseFrameOffset, 0.0f);
							WeakAnimInstance->Montage_SetPosition(WeakMontage.Get(), HoldPosition);
							WeakAnimInstance->Montage_Pause(WeakMontage.Get());
						}
					}),
					FreezeDelay,
					false);
			}
		}

		// Piggybacks on the same server->client replication PlayMobMotion already uses for the
		// animation itself, so this needs no separate RPC - every client (and the listen-server's own
		// view) spawns its own purely-cosmetic effect actor when it receives/plays a damage reaction.
		if (Motion == EMT2MobMotion::FrontDamage || Motion == EMT2MobMotion::BackDamage)
		{
			if (UWorld* World = GetWorld())
			{
				AMT2HitEffectActor::SpawnDefaultHitEffect(World, GetMesh()->GetComponentLocation());
			}
		}
	}
	return true;
}

void AMT2Mob::LockCombatMovement(float Duration)
{
	if (!HasAuthority())
	{
		return;
	}
	bCombatMotionLocked = true;
	if (AAIController* AIController = Cast<AAIController>(GetController()))
	{
		AIController->StopMovement();
	}
	GetCharacterMovement()->StopMovementImmediately();
	GetWorldTimerManager().SetTimer(
		CombatMotionLockTimer, this, &AMT2Mob::UnlockCombatMovement,
		FMath::Max(Duration, GetDefault<UMT2MobRuntimeSettings>()->MinimumCombatLockDuration), false);
}

void AMT2Mob::UnlockCombatMovement()
{
	bCombatMotionLocked = false;
}

bool AMT2Mob::PlayMobSkillAnimation(int32 SkillVnum)
{
	for (int32 SkillIndex = 0; SkillIndex < MobSkills.Num() && SkillIndex < 5; ++SkillIndex)
	{
		if (MobSkills[SkillIndex].SkillVnum == SkillVnum)
		{
			return PlayMobMotion(static_cast<EMT2MobMotion>(
				static_cast<uint8>(EMT2MobMotion::Special1) + SkillIndex));
		}
	}
	return false;
}

void AMT2Mob::SetLastDamageInstigator(AActor* DamageInstigator)
{
	if (HasAuthority() && DamageInstigator && DamageInstigator != this)
	{
		LastDamageInstigator = DamageInstigator;
	}
}

void AMT2Mob::SetSpawningStone(AMT2MetinStone* Stone)
{
	SpawningStone = Stone;
}

AMT2MetinStone* AMT2Mob::GetSpawningStone() const
{
	return SpawningStone.Get();
}

void AMT2Mob::RecordDamage(AActor* Attacker, float Amount)
{
	if (!HasAuthority() || !Attacker || Attacker == this || Amount <= 0.0f)
	{
		return;
	}
	if (FMT2DamageShare* Existing = DamageShares.FindByPredicate(
		[Attacker](const FMT2DamageShare& Share) { return Share.Attacker == Attacker; }))
	{
		Existing->TotalDamage += Amount;
	}
	else
	{
		FMT2DamageShare& Share = DamageShares.AddDefaulted_GetRef();
		Share.Attacker = Attacker;
		Share.TotalDamage = Amount;
	}

	MobAIComponent->SetTargetActor(Attacker);
	for (const TWeakObjectPtr<AMT2Mob>& GroupMember : SpawnGroupMembers)
	{
		if (AMT2Mob* Member = GroupMember.Get(); Member && !Member->GetHealthComponent()->IsDead())
		{
			Member->GetMobAIComponent()->SetTargetActor(Attacker);
		}
	}
}

void AMT2Mob::SetSpawnGroupMembers(const TArray<AMT2Mob*>& Members)
{
	SpawnGroupMembers.Reset(Members.Num());
	for (AMT2Mob* Member : Members)
	{
		if (Member)
		{
			SpawnGroupMembers.Add(Member);
		}
	}
}

FVector AMT2Mob::FindGroundSpawnLocation(const UWorld* World, TSubclassOf<AMT2Mob> MobClass,
	const FVector& Origin, float StartRadius, const AActor* IgnoredActor)
{
	if (!World || !MobClass)
	{
		return Origin;
	}
	const AMT2Mob* Defaults = MobClass->GetDefaultObject<AMT2Mob>();
	const UCapsuleComponent* Capsule = Defaults ? Defaults->GetCapsuleComponent() : nullptr;
	const UMT2MobRuntimeSettings* Settings = GetDefault<UMT2MobRuntimeSettings>();
	float Radius = Capsule ? Capsule->GetScaledCapsuleRadius() : Settings->MediumCapsuleRadius;
	float HalfHeight =
		Capsule ? Capsule->GetScaledCapsuleHalfHeight() : Settings->MediumCapsuleHalfHeight;
	if (Defaults)
	{
		float UnusedMeshRelativeZ = 0.0f;
		Defaults->CalculateVisualCollision(Radius, HalfHeight, UnusedMeshRelativeZ);
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MT2MobSpawnPlacement), false, IgnoredActor);
	// Offset each ring so candidates don't all line up on the same spokes.
	const float AngleOffset = FMath::FRandRange(0.0f, 360.0f);

	for (int32 Ring = 0; Ring < Settings->PlacementRingCount; ++Ring)
	{
		const float RingRadius = StartRadius + Ring * Settings->PlacementRingSpacing;
		for (int32 Step = 0; Step < Settings->PlacementStepsPerRing; ++Step)
		{
			const float Angle = FMath::DegreesToRadians(
				AngleOffset + 360.0f * Step / Settings->PlacementStepsPerRing
					+ Ring * Settings->PlacementRingAngleOffset);
			const FVector Candidate = Origin
				+ FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * RingRadius;

			// Ground first: without a hit there is no floor here at all (a hole, or off the map).
			FHitResult GroundHit;
			if (!World->LineTraceSingleByObjectType(
				GroundHit,
				Candidate + FVector(0.0f, 0.0f, Settings->PlacementGroundTraceUp),
				Candidate - FVector(0.0f, 0.0f, Settings->PlacementGroundTraceDown),
				FCollisionObjectQueryParams(ECC_WorldStatic),
				QueryParams))
			{
				continue;
			}

			// Then the body: reject anything the mob's own capsule would intersect (walls, props,
			// other mobs), which is what otherwise leaves it wedged or shoved into the air.
			const FVector Location = GroundHit.ImpactPoint
				+ FVector(0.0f, 0.0f, HalfHeight + Settings->PlacementGroundClearance);
			if (World->OverlapAnyTestByChannel(
				Location, FQuat::Identity, ECC_Pawn,
				FCollisionShape::MakeCapsule(Radius, HalfHeight), QueryParams))
			{
				continue;
			}
			return Location;
		}
	}
	return Origin;
}

void AMT2Mob::KillWithoutReward()
{
	if (!HasAuthority() || GetHealthComponent()->IsDead())
	{
		return;
	}
	// Old Dead(NULL): no killer, so HandleDeath must not pay anything out. Clearing the damage map
	// is what makes that true - the reward path keys off it entirely.
	bRewardsDistributed = true;
	DamageShares.Reset();
	GetHealthComponent()->SetHealth(0.0f);
}

void AMT2Mob::CalculateVisualCollision(
	float& OutRadius, float& OutHalfHeight, float& OutMeshRelativeZ) const
{
	const UMT2MobRuntimeSettings* Settings = GetDefault<UMT2MobRuntimeSettings>();
	OutRadius = Settings->MediumCapsuleRadius;
	OutHalfHeight = Settings->MediumCapsuleHalfHeight;
	switch (MobSize)
	{
	case EMT2MobSize::Small:
		OutRadius = Settings->SmallCapsuleRadius;
		OutHalfHeight = Settings->SmallCapsuleHalfHeight;
		break;
	case EMT2MobSize::Large:
		OutRadius = Settings->LargeCapsuleRadius;
		OutHalfHeight = Settings->LargeCapsuleHalfHeight;
		break;
	default:
		break;
	}

	OutMeshRelativeZ = -OutHalfHeight;
	const USkeletalMeshComponent* MeshComponent = GetMesh();
	const USkeletalMesh* SkeletalMesh =
		MeshComponent ? MeshComponent->GetSkeletalMeshAsset() : nullptr;
	if (!Settings->bFitCapsuleToSkeletalMesh || !SkeletalMesh)
	{
		return;
	}

	const float VisualScale = FMath::Max(ScalePercent, 1) / 100.0f;
	const FTransform VisualTransform(
		MeshComponent->GetRelativeRotation(), FVector::ZeroVector, FVector(VisualScale));
	const FBox VisualBounds = SkeletalMesh->GetBounds().GetBox().TransformBy(VisualTransform);
	if (!VisualBounds.IsValid || VisualBounds.GetSize().IsNearlyZero())
	{
		return;
	}

	const float HorizontalExtent = FMath::Max(
		FMath::Max(FMath::Abs(VisualBounds.Min.X), FMath::Abs(VisualBounds.Max.X)),
		FMath::Max(FMath::Abs(VisualBounds.Min.Y), FMath::Abs(VisualBounds.Max.Y)));
	const float VisualHalfHeight = VisualBounds.GetSize().Z * 0.5f;
	OutRadius = FMath::Max(
		HorizontalExtent * Settings->MeshCapsuleRadiusScale + Settings->MeshCapsuleRadiusPadding,
		Settings->MinimumMeshCapsuleRadius);
	OutHalfHeight = FMath::Max3(
		VisualHalfHeight * Settings->MeshCapsuleHeightScale
			+ Settings->MeshCapsuleHalfHeightPadding,
		Settings->MinimumMeshCapsuleHalfHeight,
		OutRadius);

	// Imported mobs use a ground-level pivot. Align the actual lower mesh bound to the capsule
	// bottom, which also handles meshes whose source pivot sits slightly above or below the feet.
	OutMeshRelativeZ = -OutHalfHeight - VisualBounds.Min.Z;
}

void AMT2Mob::ApplyVisualConfiguration()
{
	// Mesh and AnimClass are baked into the Blueprint's mesh component by the importer.
	if (!GetMesh())
	{
		return;
	}

	float CapsuleRadius = 0.0f;
	float CapsuleHalfHeight = 0.0f;
	float MeshRelativeZ = 0.0f;
	CalculateVisualCollision(CapsuleRadius, CapsuleHalfHeight, MeshRelativeZ);

	GetCapsuleComponent()->SetCapsuleSize(CapsuleRadius, CapsuleHalfHeight);
	GetMesh()->SetRelativeLocation(FVector(0.0f, 0.0f, MeshRelativeZ));
	const float VisualScale = FMath::Max(ScalePercent, 1) / 100.0f;
	GetMesh()->SetRelativeScale3D(FVector(VisualScale));
}

void AMT2Mob::ApplyStatsToRuntimeState()
{
	if (!HasAuthority())
	{
		return;
	}

	// The Blueprint's components already carry the imported base stats; push the derived values into
	// the live attribute/combat state.
	const FMT2CombatStats CombatStats = GetCombatStatsComponent()->GetCalculatedStats();
	AbilitySystemComponent->SetNumericAttributeBase(
		UMT2CoreAttributeSet::GetMaxHealthAttribute(), CombatStats.MaxHealth);
	AbilitySystemComponent->SetNumericAttributeBase(
		UMT2CoreAttributeSet::GetHealthAttribute(), CombatStats.MaxHealth);
	AbilitySystemComponent->SetNumericAttributeBase(
		UMT2CoreAttributeSet::GetMovementSpeedAttribute(), CombatStats.MovementSpeed);

	GetCombatComponent()->ConfigureBasicAttack(
		FMath::Max(CombatStats.AttackRange, 50.0f), 60.0f,
		GetCombatStatsComponent()->GetAverageDamage(), GetCombatStatsComponent()->GetAttackInterval(), 1);
	GetMovementSpeedComponent()->SetMovementSpeed(CombatStats.MovementSpeed);
	MobAIComponent->ApplyMovementConstraints();
	ApplyVisualConfiguration();
}

const FMT2MobMotionVariant* AMT2Mob::ChooseMotion(EMT2MobMotion Motion) const
{
	const FMT2MobMotionVariants* MotionVariants = MotionSet.Motions.Find(Motion);
	const TArray<FMT2MobMotionVariant>* Variants = MotionVariants ? &MotionVariants->Items : nullptr;
	if (!Variants || Variants->IsEmpty())
	{
		return nullptr;
	}

	int32 TotalWeight = 0;
	for (const FMT2MobMotionVariant& Variant : *Variants)
	{
		TotalWeight += FMath::Max(Variant.Weight, 1);
	}
	int32 Roll = FMath::RandRange(1, FMath::Max(TotalWeight, 1));
	for (const FMT2MobMotionVariant& Variant : *Variants)
	{
		Roll -= FMath::Max(Variant.Weight, 1);
		if (Roll <= 0)
		{
			return &Variant;
		}
	}
	return &Variants->Last();
}

void AMT2Mob::HandleDeath()
{
	LifecycleComponent->StopRegeneration();
	GetCharacterMovement()->DisableMovement();
	SetActorEnableCollision(false);
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (AAIController* AIController = Cast<AAIController>(GetController()))
	{
		MobAIComponent->NotifyDeath(AIController);
	}
	PlayMobMotion(EMT2MobMotion::FrontDead);

	if (HasAuthority())
	{
		const bool bResurrected = LifecycleComponent->SpawnResurrectionMob();
		if (!bRewardsDistributed)
		{
			bRewardsDistributed = true;
			// Old DistributeExp: a stone-spawned mob banks half its exp on its stone, which pays it
			// out when the stone itself is broken.
			if (AMT2MetinStone* Stone = SpawningStone.Get())
			{
				const int64 StoneShare = LootComponent->GetExperiencePool() / 2;
				LootComponent->AddBonusExperience(-StoneShare);
				Stone->GetLootComponent()->AddBonusExperience(StoneShare);
			}
			LootComponent->GenerateRewards(DamageShares, !bResurrected);

			// Quest kill credit goes to everyone who damaged it, matching how rewards are shared.
			for (const FMT2DamageShare& Share : DamageShares)
			{
				UMT2QuestManagerComponent::DispatchEventForActor(
					Share.Attacker.Get(), EMT2QuestEvent::Kill, GetMobVnum(), this);
			}
		}
		DamageShares.Reset();
		SetLifeSpan(CorpseLifetime);
	}
}

void AMT2Mob::HandleRevived()
{
	GetWorldTimerManager().ClearTimer(DeathAnimFreezeTimer);
	if (UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
	{
		if (DeathAnimMontage.IsValid())
		{
			AnimInstance->Montage_Stop(
				GetDefault<UMT2MobRuntimeSettings>()->MontageBlendTime, DeathAnimMontage.Get());
		}
	}
	DeathAnimMontage.Reset();
	SetActorEnableCollision(true);
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	LifecycleComponent->StartRegeneration(GetHealthComponent());
}

void AMT2Mob::OnRep_MotionSerial()
{
	PlayMobMotion(ReplicatedMotion);
}

bool AMT2Mob::CanBeKnockedBack() const
{
	// char_skill.cpp skips the CRUSH slide for victims with AIFLAG_NOMOVE (stones, doors).
	const UMT2MobAIComponent* AI = GetMobAIComponent();
	return !AI || !AI->HasFlag(EMT2MobAIFlag::NoMove);
}
