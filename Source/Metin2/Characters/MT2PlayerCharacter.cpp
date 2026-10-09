/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Characters/MT2PlayerCharacter.h"
#include "Fishing/MT2FishingComponent.h"
#include "Fishing/MT2FishingSettings.h"
#include "Config/MT2PathSettings.h"
#include "World/MT2MapAttributes.h"
#include "Audio/MT2SoundPlaybackSubsystem.h"
#include "World/MT2WorldSimulationSubsystem.h"

#include "Animation/MT2CharacterAnimInstance.h"
#include "Animation/MT2AnimationMotionData.h"

#include "Abilities/MT2GameplayTags.h"
#include "Abilities/MT2CoreAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Characters/MT2CharacterAppearanceComponent.h"
#include "Characters/MT2CharacterAppearanceSettings.h"
#include "Components/CapsuleComponent.h"
#include "Components/MT2HealthComponent.h"
#include "Components/MT2ManaComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Combat/MT2CombatComponent.h"
#include "Config/MT2GameplaySettings.h"
#include "EnhancedInputComponent.h"
#include "Equipment/MT2EquipmentComponent.h"
#include "Effects/MT2MotionEffectComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "Core/MT2VnumRegistrySubsystem.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputActionValue.h"
#include "Input/MT2InputConfig.h"
#include "Items/MT2InventoryComponent.h"
#include "Items/MT2Item.h"
#include "Items/MT2ItemBonusSettings.h"
#include "Items/MT2ItemTemplate.h"
#include "Items/Use/MT2SkillTrainingItemTemplates.h"
#include "Items/MT2WorldItem.h"
#include "Mounts/MT2MountComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Persistence/MT2PersistenceComponent.h"
#include "Quests/MT2QuestComponent.h"
#include "Quests/MT2QuestManagerComponent.h"
#include "Quests/MT2QuestTypes.h"
#include "Skills/MT2SkillComponent.h"
#include "Skills/MT2SkillDefinition.h"
#include "Skills/MT2SkillFormula.h"
#include "Stats/MT2CombatStatsComponent.h"
#include "Trade/MT2TradeComponent.h"
#include "UI/MT2CursorCarrySubsystem.h"
#include "UI/MT2GameHUDWidget.h"
#include "Characters/MT2RegenerationComponent.h"
#include "Combat/MT2TargetIndicatorComponent.h"
#include "Player/MT2AdminCommandComponent.h"
#include "Skills/MT2SkillCastComponent.h"
#include "Skills/MT2SkillVisualEffectComponent.h"
#include "UI/MT2GMMarkComponent.h"
#include "UI/MT2HUD.h"
#include "UI/MT2InventoryWidget.h"
#include "Mobs/MT2Mob.h"
#include "Npcs/MT2Npc.h"
#include "Npcs/MT2NpcInteractionComponent.h"
#include "Npcs/MT2NpcShopComponent.h"
#include "UI/MT2ShopWidget.h"
#include "Player/MT2PlayerController.h"
#include "Player/MT2PlayerState.h"
#include "Components/MT2MovementSpeedComponent.h"
#include "Components/MT2StatusEffectComponent.h"
#include "Stats/MT2CombatStatsComponent.h"
#include "Stats/MT2PrimaryStatsComponent.h"
#include "Stats/MT2PlayerStatFormula.h"
#include "UI/MT2FloatingDamageActor.h"
#include "Duel/MT2DuelComponent.h"
#include "UI/MT2RespawnWidget.h"
#include "UI/MT2RefinementDialogWidget.h"

DEFINE_LOG_CATEGORY_STATIC(LogMT2Targeting, Log, All);

namespace
{
	void PlayRefinementOutcomeSound(const UObject* WorldContextObject, bool bSuccess)
	{
		const TSoftObjectPtr<USoundBase>& Sound = bSuccess
			? UMT2GameplaySettings::Get().RefinementSuccessSound
			: UMT2GameplaySettings::Get().RefinementFailureSound;
		if (USoundBase* LoadedSound = Sound.LoadSynchronous())
		{
			UMT2SoundPlaybackSubsystem::PlayExclusive2D(WorldContextObject, LoadedSound);
		}
	}
}

AMT2PlayerCharacter::AMT2PlayerCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = true;
	Movement->RotationRate = FRotator(0.0, 540.0, 0.0);
	JumpMaxCount = 0;

	GetMesh()->SetRelativeLocation(FVector(0.0, 0.0, -90.0));
	// Character skeletal assets are converted from right-handed Granny data by mirroring source X
	// (same convention as the map import) and face -Y in the asset editor. The +90 degree yaw here
	// turns them to face +X in world space, which is what movement and facing logic expects.
	GetMesh()->SetRelativeRotation(FRotator(0.0, 90.0, 0.0));
	// Cursor targeting traces use ECC_Visibility. The default Pawn capsule ignores that channel,
	// which made traces pass through players and hit the terrain behind them.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	// Characters never push the camera around: standing next to another player - or being surrounded -
	// would otherwise yank the view in close. Only level geometry moves the camera, which is how the
	// old game behaves. Mobs and NPCs already get this from the "Mob" collision profile.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GetCapsuleComponent()->SetGenerateOverlapEvents(true);

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 450.0f;
	CameraBoom->bUsePawnControlRotation = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	// First-person view (toggled with X), riding the head bone so it sits at the character's eyes
	// and moves with the animation. bUsePawnControlRotation overrides the bone's own rotation, so
	// the view stays level and aims where the player is looking rather than where the head happens
	// to be swinging.
	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(GetMesh(), FirstPersonSocketName);
	FirstPersonCamera->bUsePawnControlRotation = true;
	FirstPersonCamera->SetFieldOfView(FirstPersonFieldOfView);
	// The follow camera stays the active one until the player asks for first person.
	FirstPersonCamera->SetActive(false);

	GetCombatComponent()->ConfigureBasicAttack(200.0f, 65.0f, 10.0f, 0.65f, 1);

	InventoryComponent = CreateDefaultSubobject<UMT2InventoryComponent>(TEXT("InventoryComponent"));
	FishingComponent = CreateDefaultSubobject<UMT2FishingComponent>(TEXT("FishingComponent"));
	PrimaryStatsComponent = CreateDefaultSubobject<UMT2PrimaryStatsComponent>(TEXT("PrimaryStatsComponent"));
	MountComponent = CreateDefaultSubobject<UMT2MountComponent>(TEXT("MountComponent"));

	GMMarkComponent = CreateDefaultSubobject<UMT2GMMarkComponent>(TEXT("GMMarkComponent"));
	GMMarkComponent->SetupAttachment(RootComponent);

	// The rotating red ring on the current target (old EFFECT_SELECT). Local-only visual.
	TargetIndicatorComponent =
		CreateDefaultSubobject<UMT2TargetIndicatorComponent>(TEXT("TargetIndicatorComponent"));

	RegenerationComponent =
		CreateDefaultSubobject<UMT2RegenerationComponent>(TEXT("RegenerationComponent"));

	SkillCastComponent =
		CreateDefaultSubobject<UMT2SkillCastComponent>(TEXT("SkillCastComponent"));
	SkillVisualEffectComponent =
		CreateDefaultSubobject<UMT2SkillVisualEffectComponent>(TEXT("SkillVisualEffectComponent"));

	AdminCommandComponent =
		CreateDefaultSubobject<UMT2AdminCommandComponent>(TEXT("AdminCommandComponent"));
}

UAbilitySystemComponent* AMT2PlayerCharacter::GetAbilitySystemComponent() const
{
	const AMT2PlayerState* MT2PlayerState = GetPlayerState<AMT2PlayerState>();
	return MT2PlayerState ? MT2PlayerState->GetAbilitySystemComponent() : nullptr;
}

FMT2PrimaryStats AMT2PlayerCharacter::GetCalculatedPrimaryStats() const
{
	FMT2PrimaryStats Result;
	if (const AMT2PlayerState* State = GetPlayerState<AMT2PlayerState>())
	{
		if (const UMT2PrimaryStatsComponent* StateStats = State->GetPrimaryStatsComponent())
		{
			Result = StateStats->GetCalculatedStats();
		}
	}
	if (PrimaryStatsComponent)
	{
		const FMT2PrimaryStats PawnStats = PrimaryStatsComponent->GetCalculatedStats();
		Result.Strength += PawnStats.Strength;
		Result.Dexterity += PawnStats.Dexterity;
		Result.Constitution += PawnStats.Constitution;
		Result.Intelligence += PawnStats.Intelligence;
	}
	return Result;
}

void AMT2PlayerCharacter::BeginPlay()
{
	Super::BeginPlay();
	// Reapply after Blueprint defaults/collision profiles have loaded. This must run on every client
	// because cursor traces are local and target simulated player proxies there.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	GetCapsuleComponent()->SetGenerateOverlapEvents(true);
	GetCombatComponent()->OnBasicAttackPerformed.AddUniqueDynamic(
		this, &AMT2PlayerCharacter::HandleBasicAttackPerformed);
	GetHealthComponent()->OnDeath.AddUniqueDynamic(this, &AMT2PlayerCharacter::HandleDeath);
	GetHealthComponent()->OnRevived.AddUniqueDynamic(this, &AMT2PlayerCharacter::HandleRevived);
	GetHealthComponent()->OnValueChanged.AddUniqueDynamic(this, &AMT2PlayerCharacter::HandleHealthChanged);
	InventoryComponent->OnEquipmentChanged.AddUniqueDynamic(this, &AMT2PlayerCharacter::HandleEquipmentChanged);
	InventoryComponent->OnInventoryChanged.AddUniqueDynamic(this, &AMT2PlayerCharacter::HandleInventoryChanged);
	GetStatusEffectComponent()->OnStatusEffectsChanged.AddUniqueDynamic(
		this, &AMT2PlayerCharacter::HandleStatusEffectsChanged);
	GetAppearanceComponent()->OnAppearanceApplied.AddUniqueDynamic(
		this, &AMT2PlayerCharacter::HandleCharacterAppearanceApplied);
	PrimaryStatsComponent->OnPrimaryStatsChanged.AddUniqueDynamic(
		this, &AMT2PlayerCharacter::HandlePrimaryStatsChanged);

	RegenerationComponent->StartRegen();
	HandleEquipmentChanged();
	SkillVisualEffectComponent->RefreshEffects();
}

void AMT2PlayerCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateSafeZoneNotification();

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const bool bAttackMotionActive = bAttackFacingLocked && Now < AttackMotionEndTime;
	if (bAttackMotionActive)
	{
		// Metin2 keeps m_fAdvancingRotation fixed for the whole swing. In particular, authored
		// lateral root displacement must never feed UE's orient-to-movement yaw.
		Movement->bOrientRotationToMovement = false;
		if (IsLocallyControlled() && AttackAdvanceSpeed > UE_SMALL_NUMBER &&
			!AttackAdvanceDirection.IsNearlyZero())
		{
			// This uses the normal CharacterMovement prediction path. ScaleInput maps the authored
			// cm/s onto the current movement cap without creating a second replicated root source.
			const float ScaleInput = FMath::Clamp(
				AttackAdvanceSpeed / FMath::Max(Movement->MaxWalkSpeed, 1.0f), 0.0f, 1.0f);
			AddMovementInput(AttackAdvanceDirection, ScaleInput);
		}
	}
	else if (bAttackFacingLocked)
	{
		bAttackFacingLocked = false;
		AttackAdvanceDirection = FVector::ZeroVector;
		AttackAdvanceSpeed = 0.0f;
		Movement->bOrientRotationToMovement = true;
	}

	// Decide chase-vs-attack (and start/stop auto-move accordingly) before actually stepping the
	// move this frame, so the same frame that comes into range also stops walking instead of
	// overshooting by one tick.
	UpdateCombatEngagement(DeltaSeconds);
	UpdateAutoMove();

	// Reached the NPC we clicked: stop and interact once.
	if (AMT2Npc* Npc = PendingInteractNpc.Get(); Npc && IsLocallyControlled())
	{
		const float Range = Npc->GetInteractionComponent()
			? Npc->GetInteractionComponent()->GetInteractionRange() : 250.0f;
		if (FVector::Dist2D(GetActorLocation(), Npc->GetActorLocation()) <= Range)
		{
			PendingInteractNpc.Reset();
			AutoMoveTargetActor.Reset();
			bHasAutoMoveTarget = false;
			GetCharacterMovement()->StopMovementImmediately();
			ServerInteractNpc(Npc);
		}
	}

	if (AActor* Target = PendingItemInteractionTarget.Get(); Target && IsLocallyControlled())
	{
		constexpr float ItemInteractionRange = 250.0f;
		if (FVector::Dist2D(GetActorLocation(), Target->GetActorLocation()) <= ItemInteractionRange)
		{
			const int32 InventorySlot = PendingItemInteractionSlot;
			PendingItemInteractionTarget.Reset();
			PendingItemInteractionSlot = INDEX_NONE;
			AutoMoveTargetActor.Reset();
			bHasAutoMoveTarget = false;
			GetCharacterMovement()->StopMovementImmediately();
			ServerUseInventoryItemOnActor(Target, InventorySlot);
		}
	}
}

void AMT2PlayerCharacter::Move(const FVector2D& MovementInput)
{
	if (!MovementInput.IsNearlyZero() && FishingComponent) { FishingComponent->RequestCancelFishing(); }
	if (!Controller || IsDead() || IsInteractionUIOpen())
	{
		return;
	}
	const bool bAttackMotionActive = bAttackFacingLocked && GetWorld() &&
		GetWorld()->GetTimeSeconds() < AttackMotionEndTime;
	const FRotator ControlRotation = Controller->GetControlRotation();
	const FRotator YawRotation(0.0, ControlRotation.Yaw, 0.0);

	if (bAttackMotionActive)
	{
		// Attack input steers the swing (and the authored forward displacement) toward where the
		// input points *relative to the camera*, exactly like walking: holding a direction aims the
		// combo there rather than spinning the pawn by an amount relative to its own facing.
		// AttackSteeringRate still caps how fast the swing may come round.
		if (!MovementInput.IsNearlyZero() && GetWorld())
		{
			const FVector DesiredDirection =
				FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X) * MovementInput.Y +
				FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y) * MovementInput.X;
			if (!DesiredDirection.IsNearlyZero())
			{
				const float MaxStep = AttackSteeringRate * GetWorld()->GetDeltaSeconds();
				const float NewYaw = FMath::FixedTurn(
					GetActorRotation().Yaw, DesiredDirection.Rotation().Yaw, MaxStep);
				const float DeltaYaw = FMath::FindDeltaAngleDegrees(AttackFacingYaw, NewYaw);
				SetActorRotation(FRotator(0.0f, NewYaw, 0.0f));
				AttackFacingYaw = NewYaw;
				AttackAdvanceDirection =
					FRotator(0.0f, DeltaYaw, 0.0f).RotateVector(AttackAdvanceDirection);
				ServerSetAttackFacingYaw(NewYaw);
			}
		}
		return;
	}

	if (!MovementInput.IsNearlyZero())
	{
		StopAutoMove();
		if (APlayerController* PlayerController = Cast<APlayerController>(Controller))
		{
			PlayerController->StopMovement();
		}
	}

	AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X), MovementInput.Y);
	AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y), MovementInput.X);
}

void AMT2PlayerCharacter::ServerSetAttackFacingYaw_Implementation(float NewYaw)
{
	if (!bAttackFacingLocked || !GetWorld() || GetWorld()->GetTimeSeconds() >= AttackMotionEndTime)
	{
		return;
	}
	NewYaw = FRotator::NormalizeAxis(NewYaw);
	const float DeltaYaw = FMath::FindDeltaAngleDegrees(AttackFacingYaw, NewYaw);
	AttackFacingYaw = NewYaw;
	AttackAdvanceDirection = FRotator(0.0f, DeltaYaw, 0.0f).RotateVector(AttackAdvanceDirection);
	SetActorRotation(FRotator(0.0f, NewYaw, 0.0f));
}

bool AMT2PlayerCharacter::TraceCursorTarget(FHitResult& OutHit) const
{
	APlayerController* PlayerController = Cast<APlayerController>(Controller);
	UWorld* World = GetWorld();
	if (!PlayerController || !World)
	{
		return false;
	}
	FVector WorldOrigin = FVector::ZeroVector;
	FVector WorldDirection = FVector::ZeroVector;
	if (!PlayerController->DeprojectMousePositionToWorld(WorldOrigin, WorldDirection))
	{
		return false;
	}

	// The player capsule blocks ECC_Visibility so that *other* characters can be clicked, which also
	// puts our own body between the cursor and the world: clicks that happen to cross the character
	// hit it instead of the ground behind, and in first person it fills the whole view. Ignoring
	// self (and anything attached, e.g. the equipped weapon) is why this can't be
	// GetHitResultUnderCursor, which takes no ignore list.
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MT2CursorTarget), true, this);
	TArray<AActor*> AttachedActors;
	GetAttachedActors(AttachedActors);
	QueryParams.AddIgnoredActors(AttachedActors);

	// Matches APlayerController::HitResultTraceDistance, the reach GetHitResultUnderCursor used.
	constexpr float CursorTraceDistance = 100000.0f;
	return World->LineTraceSingleByChannel(
		OutHit, WorldOrigin, WorldOrigin + WorldDirection * CursorTraceDistance,
		ECC_Visibility, QueryParams);
}

void AMT2PlayerCharacter::AttachFirstPersonCamera()
{
	// The appearance system swaps the skeletal mesh per race/sex at runtime, so the head bone can
	// only be resolved once a mesh is actually set - not in the constructor.
	if (GetMesh()->DoesSocketExist(FirstPersonSocketName))
	{
		FirstPersonCamera->AttachToComponent(
			GetMesh(), FAttachmentTransformRules::KeepRelativeTransform, FirstPersonSocketName);
		FirstPersonCamera->SetRelativeLocation(FirstPersonCameraOffset);
		return;
	}
	// No head bone: sit on the capsule at eye height rather than silently landing at the mesh
	// origin, which is down at the character's feet.
	UE_LOG(LogMT2Targeting, Warning,
		TEXT("[MT2Camera] No '%s' bone on the equipped mesh; first person falls back to the capsule."),
		*FirstPersonSocketName.ToString());
	FirstPersonCamera->AttachToComponent(
		GetCapsuleComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	FirstPersonCamera->SetRelativeLocation(
		FVector(0.0, 0.0, FirstPersonEyeHeight) + FirstPersonCameraOffset);
}

void AMT2PlayerCharacter::SetFirstPersonCameraOffset(const FVector& Offset)
{
	FirstPersonCameraOffset = Offset;
	// Re-apply through the same path so the capsule fallback keeps its eye-height baseline.
	AttachFirstPersonCamera();
}

#if WITH_EDITOR
void AMT2PlayerCharacter::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	static const FName OffsetName =
		GET_MEMBER_NAME_CHECKED(AMT2PlayerCharacter, FirstPersonCameraOffset);
	static const FName SocketName =
		GET_MEMBER_NAME_CHECKED(AMT2PlayerCharacter, FirstPersonSocketName);
	const FName Changed = PropertyChangedEvent.GetPropertyName();
	if (Changed == OffsetName || Changed == SocketName)
	{
		AttachFirstPersonCamera();
	}
}
#endif

void AMT2PlayerCharacter::SetFirstPersonEnabled(bool bEnabled)
{
	if (bFirstPersonActive == bEnabled)
	{
		return;
	}
	bFirstPersonActive = bEnabled;
	if (bEnabled)
	{
		AttachFirstPersonCamera();
	}
	// APawn::CalcCamera picks the first *active* camera component, so activating one and
	// deactivating the other is the whole switch.
	FirstPersonCamera->SetActive(bEnabled);
	FollowCamera->SetActive(!bEnabled);

	// The camera sits inside the character's head, so without this the local player's own body
	// (and anything attached to it, like the equipped weapon) fills the view. Owner-no-see is
	// client-local: other players still see this character normally.
	//TArray<USceneComponent*> MeshParts;
	//GetMesh()->GetChildrenComponents(true, MeshParts);
	//MeshParts.Add(GetMesh());
	//for (USceneComponent* Part : MeshParts)
	//{
	//	if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Part))
	//	{
	//		Primitive->SetOwnerNoSee(bEnabled);
	//	}
	//}

	// Leaving first person with the right button still down must release the pawn-follows-camera
	// lock, or the body would stay glued to the control yaw back in third person.
	UpdatePawnYawFollowsCamera();
}

void AMT2PlayerCharacter::Look(const FVector2D& LookInput)
{
	if (!bCameraRotateHeld)
	{
		return;
	}

	AddControllerYawInput(LookInput.X);
	AddControllerPitchInput(LookInput.Y);
}

bool AMT2PlayerCharacter::Attack()
{
	if (FishingComponent && FishingComponent->HasRodEquipped()) { return false; }
	if (MountComponent && !MountComponent->CanAttackWhileMounted())
	{
		return false;
	}
	UAbilitySystemComponent* AbilitySystem = GetAbilitySystemComponent();
	if (!AbilitySystem)
	{
		return false;
	}

	FGameplayTagContainer AttackTags;
	AttackTags.AddTag(MT2GameplayTags::Ability_Attack);
	return AbilitySystem->TryActivateAbilitiesByTag(AttackTags, true);
}

void AMT2PlayerCharacter::SetServerAutoAttackTarget(AActor* Target)
{
	if (!HasAuthority()) return;
	bAutoEngageTarget = IsValid(Target) && Target != this;
	GetCombatComponent()->SetSelectedTarget(bAutoEngageTarget ? Target : nullptr);
	if (!bAutoEngageTarget)
	{
		StopAutoMove();
		GetCombatComponent()->StopBasicAttackLoop();
	}
}

void AMT2PlayerCharacter::SetServerAutoMoveGoal(const FVector& Destination)
{
	if (!HasAuthority() || Destination.ContainsNaN()) return;
	SetServerAutoAttackTarget(nullptr);
	AutoMoveTargetActor.Reset();
	AutoMoveTargetLocation = Destination;
	bHasAutoMoveTarget = true;
}

void AMT2PlayerCharacter::RequestClickMove()
{
	if (bCameraRotateHeld || IsDead())
	{
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(Controller);
	if (!PlayerController)
	{
		return;
	}

	ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer();
	UMT2CursorCarrySubsystem* Carry = LocalPlayer
		? LocalPlayer->GetSubsystem<UMT2CursorCarrySubsystem>() : nullptr;
	const bool bHandlingCarry = Carry && Carry->IsCarrying();
	// A carried item must be placeable even while a shop/refinement window holds the normal
	// interaction lock. The server still revalidates the slot and all item restrictions.
	if (!bHandlingCarry && IsInteractionUIOpen())
	{
		return;
	}
	if (const AMT2PlayerController* MT2Controller = Cast<AMT2PlayerController>(PlayerController);
		MT2Controller && MT2Controller->IsPointerOverGameUI())
	{
		return;
	}

	FHitResult Hit;
	if (!TraceCursorTarget(Hit) || !Hit.bBlockingHit)
	{
		return;
	}

	// Route cursor-carried items by what was clicked: another actor receives an item interaction
	// request, while terrain opens the drop confirmation.
	if (bHandlingCarry)
	{
		if (Carry->GetSourceQuickSlot() != INDEX_NONE)
		{
			if (AMT2PlayerState* CarryState = GetPlayerState<AMT2PlayerState>())
			{
				CarryState->ServerSetQuickSlot(Carry->GetSourceQuickSlot(), EMT2QuickSlotType::None, 0);
			}
		}
		else if (Carry->GetCarryKind() == EMT2CarryKind::InventoryItem)
		{
			AActor* HitActor = Hit.GetActor();
			const bool bValidActorTarget =
				Cast<AMT2Npc>(HitActor) ||
				(Cast<AMT2PlayerCharacter>(HitActor) && HitActor != this);
			if (bValidActorTarget)
			{
				bAutoEngageTarget = false;
				GetCombatComponent()->StopBasicAttackLoop();
				GetCombatComponent()->SetSelectedTarget(HitActor);
				PendingInteractNpc.Reset();
				PendingItemInteractionTarget = HitActor;
				PendingItemInteractionSlot = Carry->GetCarryPayload();
				StartAutoMoveToActor(HitActor);
			}
			else
			{
				const AMT2HUD* MT2HUD = Cast<AMT2HUD>(PlayerController->GetHUD());
				UMT2GameHUDWidget* GameHUD = MT2HUD ? MT2HUD->GetGameHUDWidget() : nullptr;
				if (UMT2InventoryWidget* InventoryUI = GameHUD ? GameHUD->GetInventoryWidget() : nullptr)
				{
					InventoryUI->OpenDropDialogForSlot(Carry->GetCarryPayload());
				}
			}
		}
		Carry->EndCarry();
		return;
	}

	// NPCs are approached and interacted with, never engaged - old game OnClick model.
	PendingInteractNpc.Reset();
	PendingItemInteractionTarget.Reset();
	PendingItemInteractionSlot = INDEX_NONE;
	if (AMT2Npc* Npc = Cast<AMT2Npc>(Hit.GetActor()))
	{
		bAutoEngageTarget = false;
		GetCombatComponent()->SetSelectedTarget(nullptr);
		GetCombatComponent()->StopBasicAttackLoop();
		PendingInteractNpc = Npc;
		StartAutoMoveToActor(Npc);
		return;
	}

	AActor* HitActor = Hit.GetActor();
	const UPrimitiveComponent* HitComponent = Hit.GetComponent();
	const bool bActorTarget =
		HitActor &&
		HitActor != this &&
		HitComponent &&
		HitComponent->GetCollisionObjectType() != ECC_WorldStatic;

	if (bActorTarget)
	{
		// Only actors with an AbilitySystemComponent (mobs, other players) are worth auto-chasing
		// and auto-attacking; clicking e.g. scenery still just walks there like before.
		bAutoEngageTarget = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(HitActor) != nullptr;
		UE_LOG(LogMT2Targeting, Warning, TEXT("[MT2Click] Clicked '%s' bAutoEngageTarget=%s"),
			*GetNameSafe(HitActor), bAutoEngageTarget ? TEXT("true") : TEXT("false"));
		GetCombatComponent()->SetSelectedTarget(HitActor);
		StartAutoMoveToActor(HitActor);
		return;
	}

	bAutoEngageTarget = false;
	GetCombatComponent()->SetSelectedTarget(nullptr);
	GetCombatComponent()->StopBasicAttackLoop();
	StartAutoMoveToLocation(Hit.ImpactPoint);
}

void AMT2PlayerCharacter::RequestInspectMob()
{
	if (IsDead())
	{
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(Controller);
	if (!PlayerController)
	{
		return;
	}
	if (const AMT2PlayerController* MT2Controller = Cast<AMT2PlayerController>(PlayerController);
		MT2Controller && MT2Controller->IsPointerOverGameUI())
	{
		return;
	}

	FHitResult Hit;
	if (!TraceCursorTarget(Hit))
	{
		return;
	}
	AMT2CharacterBase* CharacterTarget = Cast<AMT2CharacterBase>(Hit.GetActor());
	if (!CharacterTarget || CharacterTarget == this)
	{
		return;
	}

	// Right click is inspection only: cancel automatic combat/chasing, then update the target panel.
	bAutoEngageTarget = false;
	if (AutoMoveTargetActor.IsValid())
	{
		StopAutoMove();
	}
	GetCombatComponent()->StopBasicAttackLoop();
	GetCombatComponent()->SetSelectedTarget(CharacterTarget);
}

void AMT2PlayerCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	InitializePlayerState();
}

void AMT2PlayerCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	InitializePlayerState();
}

void AMT2PlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	AMT2PlayerController* MT2Controller = Cast<AMT2PlayerController>(Controller);
	UMT2InputConfig* ActiveInputConfig = MT2Controller ? MT2Controller->GetInputConfig() : nullptr;

	if (!EnhancedInputComponent || !ActiveInputConfig)
	{
		return;
	}

	EnhancedInputComponent->BindAction(
		ActiveInputConfig->GetMoveAction(), ETriggerEvent::Triggered, this, &AMT2PlayerCharacter::HandleMoveInput);
	EnhancedInputComponent->BindAction(
		ActiveInputConfig->GetLookAction(), ETriggerEvent::Triggered, this, &AMT2PlayerCharacter::HandleLookInput);
	EnhancedInputComponent->BindAction(
		ActiveInputConfig->GetAttackAction(), ETriggerEvent::Started, this, &AMT2PlayerCharacter::HandleAttackPressed);
	EnhancedInputComponent->BindAction(
		ActiveInputConfig->GetAttackAction(), ETriggerEvent::Completed, this, &AMT2PlayerCharacter::HandleAttackReleased);
	EnhancedInputComponent->BindAction(
		ActiveInputConfig->GetAttackAction(), ETriggerEvent::Canceled, this, &AMT2PlayerCharacter::HandleAttackReleased);
	EnhancedInputComponent->BindAction(
		ActiveInputConfig->GetWalkAction(), ETriggerEvent::Started, this, &AMT2PlayerCharacter::HandleWalkPressed);
	EnhancedInputComponent->BindAction(
		ActiveInputConfig->GetWalkAction(), ETriggerEvent::Completed, this, &AMT2PlayerCharacter::HandleWalkReleased);
	EnhancedInputComponent->BindAction(
		ActiveInputConfig->GetWalkAction(), ETriggerEvent::Canceled, this, &AMT2PlayerCharacter::HandleWalkReleased);
	EnhancedInputComponent->BindAction(
		ActiveInputConfig->GetCameraRotateAction(), ETriggerEvent::Started, this, &AMT2PlayerCharacter::HandleCameraRotatePressed);
	EnhancedInputComponent->BindAction(
		ActiveInputConfig->GetCameraRotateAction(), ETriggerEvent::Completed, this, &AMT2PlayerCharacter::HandleCameraRotateReleased);
	EnhancedInputComponent->BindAction(
		ActiveInputConfig->GetCameraRotateAction(), ETriggerEvent::Canceled, this, &AMT2PlayerCharacter::HandleCameraRotateReleased);
	EnhancedInputComponent->BindAction(
		ActiveInputConfig->GetClickMoveAction(), ETriggerEvent::Started, this, &AMT2PlayerCharacter::HandleClickMovePressed);
	EnhancedInputComponent->BindAction(
		ActiveInputConfig->GetKeyboardCameraYawAction(), ETriggerEvent::Triggered,
		this, &AMT2PlayerCharacter::HandleKeyboardCameraYaw);
	EnhancedInputComponent->BindAction(
		ActiveInputConfig->GetKeyboardCameraZoomAction(), ETriggerEvent::Triggered,
		this, &AMT2PlayerCharacter::HandleKeyboardCameraZoom);
	EnhancedInputComponent->BindAction(
		ActiveInputConfig->GetKeyboardCameraPitchAction(), ETriggerEvent::Triggered,
		this, &AMT2PlayerCharacter::HandleKeyboardCameraPitch);
}

void AMT2PlayerCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		if (AMT2PlayerState* State = GetPlayerState<AMT2PlayerState>()) { State->GetDuelComponent()->CancelAllDuels(); }
	}
	RegenerationComponent->StopRegen();
	GetCombatComponent()->StopBasicAttackLoop();
	GetCombatComponent()->OnBasicAttackPerformed.RemoveDynamic(
		this, &AMT2PlayerCharacter::HandleBasicAttackPerformed);
	GetHealthComponent()->OnDeath.RemoveDynamic(this, &AMT2PlayerCharacter::HandleDeath);
	GetHealthComponent()->OnRevived.RemoveDynamic(this, &AMT2PlayerCharacter::HandleRevived);
	GetHealthComponent()->OnValueChanged.RemoveDynamic(this, &AMT2PlayerCharacter::HandleHealthChanged);
	InventoryComponent->OnEquipmentChanged.RemoveDynamic(this, &AMT2PlayerCharacter::HandleEquipmentChanged);
	InventoryComponent->OnInventoryChanged.RemoveDynamic(this, &AMT2PlayerCharacter::HandleInventoryChanged);
	GetAppearanceComponent()->OnAppearanceApplied.RemoveDynamic(
		this, &AMT2PlayerCharacter::HandleCharacterAppearanceApplied);
	PrimaryStatsComponent->OnPrimaryStatsChanged.RemoveDynamic(
		this, &AMT2PlayerCharacter::HandlePrimaryStatsChanged);

	if (AMT2PlayerState* BoundState = BoundPlayerState.Get())
	{
		BoundState->OnCharacterAppearanceChanged.RemoveDynamic(
			this, &AMT2PlayerCharacter::HandleCharacterAppearanceChanged);
		BoundState->OnLevelChanged.RemoveDynamic(
			this, &AMT2PlayerCharacter::HandlePlayerLevelChanged);
		if (BoundState->GetPrimaryStatsComponent())
		{
			BoundState->GetPrimaryStatsComponent()->OnPrimaryStatsChanged.RemoveDynamic(
				this, &AMT2PlayerCharacter::HandlePrimaryStatsChanged);
		}
	}

	Super::EndPlay(EndPlayReason);
}

void AMT2PlayerCharacter::HandleMoveInput(const FInputActionValue& Value)
{
	Move(Value.Get<FVector2D>());
}

void AMT2PlayerCharacter::HandleLookInput(const FInputActionValue& Value)
{
	const FVector2D LookInput = Value.Get<FVector2D>();
	if (bCameraRotateHeld)
	{
		RightMouseDragDistance += LookInput.Size();
	}
	Look(LookInput);
}

void AMT2PlayerCharacter::HandleAttackPressed()
{
	if (FishingComponent && FishingComponent->HasRodEquipped())
	{
		StopAutoMove(); GetCombatComponent()->StopBasicAttackLoop(); FishingComponent->RequestToggleFishing(); return;
	}
	GetCombatComponent()->StartBasicAttackLoop();
}

void AMT2PlayerCharacter::HandleAttackReleased()
{
	GetCombatComponent()->StopBasicAttackLoop();
}

void AMT2PlayerCharacter::HandleWalkPressed()
{
	SetWalkRequested(true);
	ServerSetWalkRequested(true);
}

void AMT2PlayerCharacter::HandleWalkReleased()
{
	SetWalkRequested(false);
	ServerSetWalkRequested(false);
}

void AMT2PlayerCharacter::HandleCameraRotatePressed()
{
	if (const AMT2PlayerController* MT2Controller = Cast<AMT2PlayerController>(Controller);
		MT2Controller && MT2Controller->IsPointerOverGameUI())
	{
		return;
	}
	bCameraRotateHeld = true;
	RightMouseDragDistance = 0.0f;
	UpdatePawnYawFollowsCamera();
}

void AMT2PlayerCharacter::HandleCameraRotateReleased()
{
	const bool bWasCameraRotateHeld = bCameraRotateHeld;
	bCameraRotateHeld = false;
	UpdatePawnYawFollowsCamera();
	if (bWasCameraRotateHeld && RightMouseDragDistance <= RightClickInspectDragThreshold)
	{
		RequestInspectMob();
	}
}

void AMT2PlayerCharacter::UpdatePawnYawFollowsCamera()
{
	// In first person, holding the right mouse should turn the whole pawn to face the camera, so the
	// player can spin on the spot without walking. bUseControllerRotationYaw glues the body yaw to
	// the control rotation; it must win over bOrientRotationToMovement (which only turns the pawn
	// when there's velocity) while it's on. Third person keeps its old free-look behaviour.
	// Never override the attack-motion facing lock, which drives its own rotation during a swing.
	const bool bFollow = bFirstPersonActive && bCameraRotateHeld && !bAttackFacingLocked;
	bUseControllerRotationYaw = bFollow;
	GetCharacterMovement()->bOrientRotationToMovement = !bFollow;
}

void AMT2PlayerCharacter::HandleKeyboardCameraYaw(const FInputActionValue& Value)
{
	if (!Controller || !GetWorld())
	{
		return;
	}
	FRotator CameraRotation = Controller->GetControlRotation();
	CameraRotation.Yaw += Value.Get<float>() * KeyboardCameraYawSpeed * GetWorld()->GetDeltaSeconds();
	Controller->SetControlRotation(CameraRotation);
}

void AMT2PlayerCharacter::HandleKeyboardCameraZoom(const FInputActionValue& Value)
{
	if (!CameraBoom || !GetWorld())
	{
		return;
	}
	CameraBoom->TargetArmLength = FMath::Clamp(
		CameraBoom->TargetArmLength +
			Value.Get<float>() * KeyboardCameraZoomSpeed * GetWorld()->GetDeltaSeconds(),
		MinimumCameraDistance, MaximumCameraDistance);
}

void AMT2PlayerCharacter::HandleKeyboardCameraPitch(const FInputActionValue& Value)
{
	if (const APlayerController* PlayerController = Cast<APlayerController>(Controller);
		PlayerController && PlayerController->IsInputKeyDown(EKeys::LeftControl))
	{
		return;
	}
	if (!Controller || !GetWorld())
	{
		return;
	}
	FRotator CameraRotation = Controller->GetControlRotation();
	CameraRotation.Pitch = FMath::Clamp(
		FRotator::NormalizeAxis(CameraRotation.Pitch) +
			Value.Get<float>() * KeyboardCameraPitchSpeed * GetWorld()->GetDeltaSeconds(),
		-80.0f, 80.0f);
	Controller->SetControlRotation(CameraRotation);
}

void AMT2PlayerCharacter::HandleClickMovePressed()
{
	RequestClickMove();
}

void AMT2PlayerCharacter::StartAutoMoveToLocation(const FVector& TargetLocation)
{
	AutoMoveTargetActor.Reset();
	AutoMoveTargetLocation = TargetLocation;
	bHasAutoMoveTarget = true;
}

void AMT2PlayerCharacter::StartAutoMoveToActor(AActor* TargetActor)
{
	if (!TargetActor || TargetActor == this)
	{
		return;
	}

	AutoMoveTargetActor = TargetActor;
	AutoMoveTargetLocation = TargetActor->GetActorLocation();
	bHasAutoMoveTarget = true;
}

void AMT2PlayerCharacter::StopAutoMove()
{
	bHasAutoMoveTarget = false;
	AutoMoveTargetActor.Reset();
}

void AMT2PlayerCharacter::UpdateAutoMove()
{
	if (!bHasAutoMoveTarget || !Controller || IsAttackMovementLocked() || IsDead())
	{
		return;
	}

	if (AActor* TargetActor = AutoMoveTargetActor.Get())
	{
		AutoMoveTargetLocation = TargetActor->GetActorLocation();
	}

	FVector ToTarget = AutoMoveTargetLocation - GetActorLocation();
	ToTarget.Z = 0.0f;

	if (ToTarget.SizeSquared() <= FMath::Square(AutoMoveAcceptanceRadius))
	{
		StopAutoMove();
		return;
	}

	AddMovementInput(ToTarget.GetSafeNormal(), 1.0f);
}

void AMT2PlayerCharacter::UpdateCombatEngagement(float DeltaSeconds)
{
	if (!bAutoEngageTarget || IsDead())
	{
		return;
	}

	UMT2CombatComponent* Combat = GetCombatComponent();
	AActor* Target = Combat->GetSelectedTarget();
	const UAbilitySystemComponent* TargetAbilitySystem =
		Target ? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target) : nullptr;
	const bool bTargetGone =
		!Target || !TargetAbilitySystem || TargetAbilitySystem->HasMatchingGameplayTag(MT2GameplayTags::Status_Dead);

	if (bTargetGone)
	{
		bAutoEngageTarget = false;
		Combat->SetSelectedTarget(nullptr);
		Combat->StopBasicAttackLoop();
		if (AutoMoveTargetActor.Get() == Target)
		{
			StopAutoMove();
		}
		return;
	}

	const float DistanceSquared = FVector::DistSquared(GetActorLocation(), Target->GetActorLocation());
	const float AttackRange = Combat->GetBasicAttackRange();
	if (DistanceSquared > FMath::Square(AttackRange))
	{
		// Out of range: keep/resume chasing, don't swing at empty air.
		if (Combat->IsBasicAttackHeld())
		{
			UE_LOG(LogMT2Targeting, Warning, TEXT("[MT2Engage] '%s' out of range (dist=%.0f > range=%.0f), stopping attack loop"),
				*GetNameSafe(Target), FMath::Sqrt(DistanceSquared), AttackRange);
			Combat->StopBasicAttackLoop();
		}
		if (AutoMoveTargetActor.Get() != Target)
		{
			UE_LOG(LogMT2Targeting, Warning, TEXT("[MT2Engage] Chasing '%s'"), *GetNameSafe(Target));
			StartAutoMoveToActor(Target);
		}
	}
	else
	{
		// In range: stop closing the distance and start swinging.
		if (AutoMoveTargetActor.Get() == Target)
		{
			UE_LOG(LogMT2Targeting, Warning, TEXT("[MT2Engage] '%s' in range (dist=%.0f <= range=%.0f), stopping chase"),
				*GetNameSafe(Target), FMath::Sqrt(DistanceSquared), AttackRange);
			StopAutoMove();
		}
		if (!Combat->IsBasicAttackHeld())
		{
			UE_LOG(LogMT2Targeting, Warning, TEXT("[MT2Engage] Starting attack loop on '%s'"), *GetNameSafe(Target));
			Combat->StartBasicAttackLoop();
		}

		// Standing still while swinging isn't covered by bOrientRotationToMovement (no velocity to
		// derive a facing from), so turn to face the target explicitly while in attack range.
		RotateTowardsTarget(Target, DeltaSeconds);
	}
}

void AMT2PlayerCharacter::RotateTowardsTarget(const AActor* Target, float DeltaSeconds)
{
	if (!Target)
	{
		return;
	}

	FVector ToTarget = Target->GetActorLocation() - GetActorLocation();
	ToTarget.Z = 0.0f;
	if (ToTarget.IsNearlyZero())
	{
		return;
	}

	const FRotator DesiredRotation = ToTarget.Rotation();
	const FRotator NewRotation = FMath::RInterpConstantTo(
		GetActorRotation(), DesiredRotation, DeltaSeconds, GetCharacterMovement()->RotationRate.Yaw);
	SetActorRotation(NewRotation);
}

bool AMT2PlayerCharacter::IsAttackMovementLocked() const
{
	const UAbilitySystemComponent* AbilitySystem = GetAbilitySystemComponent();
	return AbilitySystem && AbilitySystem->HasMatchingGameplayTag(MT2GameplayTags::State_Attacking);
}

bool AMT2PlayerCharacter::IsDead() const
{
	return GetHealthComponent()->IsDead();
}

bool AMT2PlayerCharacter::IsPvPEnabledAgainst(const AMT2CharacterBase* Other) const
{
	const AMT2PlayerCharacter* OtherPlayer = Cast<AMT2PlayerCharacter>(Other);
	// This overrides duels, karma and empire hostility, and is rechecked at the actual hit.
	if (!OtherPlayer || IsInSafeZone() || OtherPlayer->IsInSafeZone())
	{
		return false;
	}
	if (IsDuelingWith(OtherPlayer))
	{
		return true;
	}

	const AMT2PlayerState* AttackerState = GetPlayerState<AMT2PlayerState>();
	const AMT2PlayerState* DefenderState = OtherPlayer->GetPlayerState<AMT2PlayerState>();
	if (!AttackerState || !DefenderState)
	{
		return false;
	}

	// Negative-karma players are open PvP targets for everyone, including their own empire.
	if (DefenderState->GetRawAlignment() < 0)
	{
		return true;
	}

	// Empire war is always enabled between valid, different empires.
	const EMT2Empire AttackerEmpire = AttackerState->GetEmpire();
	const EMT2Empire DefenderEmpire = DefenderState->GetEmpire();
	if (AttackerEmpire != EMT2Empire::None && DefenderEmpire != EMT2Empire::None
		&& AttackerEmpire != DefenderEmpire)
	{
		return true;
	}

	// Aggressive mode lets the attacker opt into friendly PvP. It never changes the defender's
	// permissions by itself; neutral allies still cannot retaliate unless this attacker has negative karma.
	return AttackerState->IsAggressiveMode();
}

bool AMT2PlayerCharacter::IsInSafeZone() const
{
	const UWorld* World = GetWorld();
	const UMT2MapAttributeSubsystem* Attributes = World ? World->GetSubsystem<UMT2MapAttributeSubsystem>() : nullptr;
	return Attributes && Attributes->IsSafeZone(GetActorLocation());
}

bool AMT2PlayerCharacter::IsDuelingWith(const AMT2CharacterBase* Other) const
{
	const AMT2PlayerCharacter* Opponent = Cast<AMT2PlayerCharacter>(Other);
	const AMT2PlayerState* Self = GetPlayerState<AMT2PlayerState>();
	return Opponent && Opponent != this && Self &&
		Self->GetDuelComponent()->IsFighting(Opponent->GetPlayerState<AMT2PlayerState>());
}

void AMT2PlayerCharacter::UpdateSafeZoneNotification()
{
	if (!HasAuthority()) { return; }
	AMT2PlayerController* PlayerController = Cast<AMT2PlayerController>(GetController());
	if (!PlayerController) { return; }
	const bool bSafe = IsInSafeZone();
	if (!bSafeZoneNotified || SafeZoneNotifiedController != PlayerController || bLastSafeZone != bSafe)
	{
		PlayerController->SendSystemChatMessage(bSafe ? TEXT("safezone area") : TEXT("unprotected area"));
		bSafeZoneNotified = true;
		bLastSafeZone = bSafe;
		SafeZoneNotifiedController = PlayerController;
	}
}

void AMT2PlayerCharacter::HandleDeath()
{
	GetCombatComponent()->CancelPendingBasicAttackHits();
	if (FishingComponent && HasAuthority()) { FishingComponent->CancelFishing(); }
	DeathLocation = GetActorLocation();
	if (HasAuthority() && MountComponent)
	{
		MountComponent->ForceDismount();
	}

	// Cancel any in-flight movement/combat state - a dead character shouldn't keep chasing,
	// attacking, or walking toward wherever it was headed.
	bAutoEngageTarget = false;
	StopAutoMove();
	GetCombatComponent()->SetSelectedTarget(nullptr);
	GetCombatComponent()->StopBasicAttackLoop();
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	RegenerationComponent->StopRegen();

	if (HasAuthority())
	{
		if (UMT2WorldSimulationSubsystem* Simulation =
			GetWorld()->GetSubsystem<UMT2WorldSimulationSubsystem>())
		{
			Simulation->ClearMobTargetsFor(this);
		}
		GetStatusEffectComponent()->RemoveEffectsOnDeath();
		AMT2PlayerCharacter* Killer = LastPlayerDamageInstigator.Get();
		LastPlayerDamageInstigator.Reset();
		AMT2PlayerState* VictimState = GetPlayerState<AMT2PlayerState>();
		const bool bDuelDeath = VictimState && VictimState->GetDuelComponent()->HandleDefeat(
			Killer ? Killer->GetPlayerState<AMT2PlayerState>() : nullptr);
		if (!bDuelDeath && Killer && Killer != this)
		{
			if (AMT2PlayerState* KillerState = Killer->GetPlayerState<AMT2PlayerState>();
				KillerState && KillerState->IsAggressiveMode())
			{
				KillerState->ChangeKarmaPoints(-FMath::Max(UMT2GameplaySettings::Get().AggressivePlayerKillKarmaPenalty, 0));
			}
		}
		const int32 Karma = VictimState ? VictimState->GetKarmaPoints() : 0;
		const float DropChance = Karma < 0
			? FMath::Clamp(
				static_cast<float>(-Karma) / static_cast<float>(-MT2KarmaLimits::Minimum),
				0.0f, 1.0f)
			: 0.0f;
		if (!bDuelDeath && Killer && Killer != this && FMath::FRand() < DropChance)
		{
			FMT2ItemSlot DroppedEquipment;
			if (GetInventoryComponent()->ExtractRandomEquippedItem(DroppedEquipment))
			{
				const AMT2PlayerState* KillerState =
					Killer->GetPlayerState<AMT2PlayerState>();
				const FString OwnerId = AMT2WorldItem::ResolvePlayerIdentity(KillerState);
				const FString OwnerName = KillerState
					? KillerState->GetCharacterName() : FString();
				if (!AMT2WorldItem::SpawnWorldItemInstance(
					GetWorld(), DeathLocation, DroppedEquipment, this, OwnerId, OwnerName,
					UMT2GameplaySettings::Get().PvPEquipmentDropOwnershipSeconds))
				{
					GetInventoryComponent()->AddItemSlotPartial(DroppedEquipment);
				}
			}
		}
	}

	if (GetNetMode() == NM_DedicatedServer || !GetMesh())
	{
		return;
	}

	UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
	const UMT2CharacterAnimInstance* MT2AnimInstance = Cast<UMT2CharacterAnimInstance>(AnimInstance);
	UAnimSequence* DeadSequence = MT2AnimInstance ? MT2AnimInstance->GetActiveAnimation(TEXT("dead")) : nullptr;
	if (AnimInstance && DeadSequence)
	{
		constexpr float BlendOutTime = 0.2f;
		UAnimMontage* Montage = AnimInstance->PlaySlotAnimationAsDynamicMontage(
			DeadSequence, TEXT("DefaultSlot"), 0.2f, BlendOutTime, 1.0f, 1, -1.0f, 0.0f);
		if (Montage)
		{
			// A one-shot montage automatically starts blending back to the (idle) locomotion pose
			// once its remaining time drops to BlendOutTime, which would make the character look like
			// it stood back up while still logically dead. Montage_Pause alone doesn't prevent that -
			// the blend-out is driven by remaining time, not by whether playback is paused. Instead,
			// zero the play rate strictly BEFORE the blend-out window even opens, so the montage never
			// reaches "nearly finished" and never triggers the automatic blend back to locomotion.
			TWeakObjectPtr<UAnimInstance> WeakAnimInstance(AnimInstance);
			TWeakObjectPtr<UAnimMontage> WeakMontage(Montage);
			const float FreezeDelay = FMath::Max(DeadSequence->GetPlayLength() - BlendOutTime - 0.05f, 0.05f);
			GetWorldTimerManager().SetTimer(
				DeathAnimFreezeTimer,
				FTimerDelegate::CreateWeakLambda(this, [WeakAnimInstance, WeakMontage]()
				{
					if (WeakAnimInstance.IsValid() && WeakMontage.IsValid())
					{
						WeakAnimInstance->Montage_SetPlayRate(WeakMontage.Get(), 0.0f);
					}
				}),
				FreezeDelay,
				false);
		}
	}

	if (IsLocallyControlled())
	{
		if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
		{
			// /Game/UI/MT2Respawn is authored by MT2GenerateUIBlueprintsCommandlet, same as every other
			// real UI window in this project (taskbar, inventory, HUD) - not the raw C++ class.
			const TSubclassOf<UMT2RespawnWidget> RespawnWidgetClass =
				LoadClass<UMT2RespawnWidget>(nullptr, UMT2PathSettings::Path(TEXT("UI_MT2Respawn")));
			if (!RespawnWidgetClass)
			{
				UE_LOG(LogMT2Targeting, Error, TEXT("Required UI asset /Game/UI/MT2Respawn is missing or invalid."));
			}
			else if (UMT2RespawnWidget* Widget = CreateWidget<UMT2RespawnWidget>(PlayerController, RespawnWidgetClass))
			{
				Widget->AddToViewport(100);
				RespawnWidget = Widget;
			}
		}
	}
}

void AMT2PlayerCharacter::HandleRevived()
{
	GetWorldTimerManager().ClearTimer(DeathAnimFreezeTimer);
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	RegenerationComponent->StartRegen();

	if (GetNetMode() != NM_DedicatedServer)
	{
		if (UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
		{
			AnimInstance->StopAllMontages(0.2f);
		}
	}

	if (UMT2RespawnWidget* Widget = RespawnWidget.Get())
	{
		Widget->RemoveFromParent();
	}
	RespawnWidget.Reset();
}

void AMT2PlayerCharacter::RequestRespawn(bool bRespawnInTown)
{
	ServerRequestRespawn(bRespawnInTown);
}

void AMT2PlayerCharacter::ServerRequestRespawn_Implementation(bool bRespawnInTown)
{
	if (!IsDead())
	{
		return;
	}

	const FVector RespawnLocation = bRespawnInTown ? FindTownRespawnLocation() : DeathLocation;
	SetActorLocation(RespawnLocation, false, nullptr, ETeleportType::ResetPhysics);

	// The old server revives with a FLAT 50 HP, not full health (do_restart, cmd_general.cpp:635/643);
	// the passive 3s recovery event then heals the character back up. See
	// Docs/OldGameResearch/DeathAndRespawn.md.
	UMT2HealthComponent* Health = GetHealthComponent();
	Health->SetHealth(FMath::Min(50.0f, Health->GetMaxHealth()));

	// SetHealth() sets the attribute directly rather than through a GameplayEffect, so it doesn't go
	// through UMT2CoreAttributeSet::PostGameplayEffectExecute - which is the only place Status_Dead
	// normally gets cleared. Clear it the same way that code does, or the attack ability (and
	// anything else gated on this tag) would stay blocked forever after respawning.
	if (UAbilitySystemComponent* AbilitySystem = GetAbilitySystemComponent())
	{
		AbilitySystem->SetLooseGameplayTagCount(MT2GameplayTags::Status_Dead, 0, EGameplayTagReplicationState::TagOnly);
	}
}

FVector AMT2PlayerCharacter::FindTownRespawnLocation() const
{
	TArray<AActor*> PlayerStarts;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), APlayerStart::StaticClass(), PlayerStarts);

	AActor* FallbackStart = nullptr;
	for (AActor* Start : PlayerStarts)
	{
		if (!Start)
		{
			continue;
		}
		if (Start->ActorHasTag(TEXT("Town")))
		{
			return Start->GetActorLocation();
		}
		if (!FallbackStart)
		{
			FallbackStart = Start;
		}
	}

	return FallbackStart ? FallbackStart->GetActorLocation() : DeathLocation;
}

void AMT2PlayerCharacter::ServerSetWalkRequested_Implementation(bool bNewWalkRequested)
{
	SetWalkRequested(bNewWalkRequested);
}

void AMT2PlayerCharacter::InitializePlayerState()
{
	AMT2PlayerState* MT2PlayerState = GetPlayerState<AMT2PlayerState>();
	if (MT2PlayerState)
	{
		MT2PlayerState->InitializeAbilitySystem(this);
		InitializeAbilityComponents(MT2PlayerState->GetAbilitySystemComponent());
	}

	if (BoundPlayerState.Get() != MT2PlayerState)
	{
		if (AMT2PlayerState* PreviousPlayerState = BoundPlayerState.Get())
		{
			PreviousPlayerState->OnCharacterAppearanceChanged.RemoveDynamic(
				this, &AMT2PlayerCharacter::HandleCharacterAppearanceChanged);
			PreviousPlayerState->OnLevelChanged.RemoveDynamic(
				this, &AMT2PlayerCharacter::HandlePlayerLevelChanged);
			if (PreviousPlayerState->GetPrimaryStatsComponent())
			{
				PreviousPlayerState->GetPrimaryStatsComponent()->OnPrimaryStatsChanged.RemoveDynamic(
					this, &AMT2PlayerCharacter::HandlePrimaryStatsChanged);
			}
		}

		BoundPlayerState = MT2PlayerState;
		if (MT2PlayerState)
		{
			MT2PlayerState->OnCharacterAppearanceChanged.AddUniqueDynamic(
				this, &AMT2PlayerCharacter::HandleCharacterAppearanceChanged);
			MT2PlayerState->OnLevelChanged.AddUniqueDynamic(
				this, &AMT2PlayerCharacter::HandlePlayerLevelChanged);
			if (MT2PlayerState->GetPrimaryStatsComponent())
			{
				MT2PlayerState->GetPrimaryStatsComponent()->OnPrimaryStatsChanged.AddUniqueDynamic(
					this, &AMT2PlayerCharacter::HandlePrimaryStatsChanged);
			}
		}
	}

	if (MT2PlayerState)
	{
		GetAppearanceComponent()->ApplyCharacterAppearance(MT2PlayerState->GetCharacterAppearance());
		GetEquipmentComponent()->ApplyDefaultHair(MT2PlayerState->GetCharacterAppearance());

		// Buffs/debuffs restored from the persistent payload before this pawn existed (old game:
		// LoadAffect on login). Server-only by construction - only the server stages them.
		TArray<FMT2StatusEffect> RestoredEffects;
		if (HasAuthority() && MT2PlayerState->ConsumePendingStatusEffects(RestoredEffects))
		{
			GetStatusEffectComponent()->RestoreEffects(RestoredEffects);
		}
		TArray<FMT2ItemSlot> RestoredInventory;
		TArray<FMT2ItemSlot> RestoredEquipment;
		const bool bRestoredInventory = HasAuthority() &&
			MT2PlayerState->ConsumePendingInventory(RestoredInventory, RestoredEquipment);
		if (bRestoredInventory)
		{
			InventoryComponent->RestoreItems(RestoredInventory, RestoredEquipment);
			MT2PlayerState->GrantStartingItemsIfNeeded(this);
		}
		else if (HasAuthority() && GetWorld() && GetWorld()->WorldType == EWorldType::PIE)
		{
			// PIE players do not necessarily pass through coordinator persistence, but should still
			// receive the configured new-character loadout for gameplay testing.
			MT2PlayerState->GrantStartingItemsIfNeeded(this);
		}
		RefreshDerivedStats();
	}
}

void AMT2PlayerCharacter::HandleCharacterAppearanceChanged(const FMT2CharacterAppearance& Appearance)
{
	GetAppearanceComponent()->ApplyCharacterAppearance(Appearance);
	GetEquipmentComponent()->ApplyDefaultHair(Appearance);
	RefreshDerivedStats();
}

void AMT2PlayerCharacter::HandleCharacterAppearanceApplied(const FMT2CharacterAppearance& Appearance)
{
	GetEquipmentComponent()->ApplyDefaultHair(Appearance);
	HandleEquipmentChanged();
}

void AMT2PlayerCharacter::HandlePlayerLevelChanged(int32, int32)
{
	RefreshDerivedStats();
}

void AMT2PlayerCharacter::HandlePrimaryStatsChanged(FMT2PrimaryStats, FMT2PrimaryStats)
{
	RefreshDerivedStats();
}

void AMT2PlayerCharacter::ClientShowDamageNumber_Implementation(
	AActor* TargetActor, float Damage, EMT2DamageDisplayType DamageType)
{
	APlayerController* LocalController = Cast<APlayerController>(GetController());
	if (!IsValid(TargetActor) || !LocalController || !LocalController->IsLocalController() || !GetWorld() ||
		!FMath::IsFinite(Damage) || Damage <= 0.0f)
	{
		return;
	}

	FVector BoundsOrigin;
	FVector BoundsExtent;
	TargetActor->GetActorBounds(true, BoundsOrigin, BoundsExtent);
	const FVector SpawnLocation = BoundsOrigin + FVector(0.0f, 0.0f, BoundsExtent.Z + 8.0f);
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AMT2FloatingDamageActor* DamageActor = GetWorld()->SpawnActor<AMT2FloatingDamageActor>(
		AMT2FloatingDamageActor::StaticClass(), SpawnLocation, FRotator::ZeroRotator, SpawnParameters))
	{
		DamageActor->InitializeDamage(Damage, DamageType, LocalController->PlayerCameraManager, TargetActor == this);
	}
	// Receiving a number must not add a second set of attacker-side impact sounds.
	if (TargetActor == this) { return; }

	const UMT2GameplaySettings& Settings = UMT2GameplaySettings::Get();
	if (!Settings.BasicHitSounds.IsEmpty())
	{
		const int32 SoundIndex = FMath::RandRange(0, Settings.BasicHitSounds.Num() - 1);
		if (USoundBase* LoadedSound = Settings.BasicHitSounds[SoundIndex].LoadSynchronous())
		{
			UMT2SoundPlaybackSubsystem::PlayExclusiveAtLocation(
				this, LoadedSound, TargetActor->GetActorLocation());
		}
	}

	const TSoftObjectPtr<USoundBase>* HitSound = nullptr;
	if (DamageType == EMT2DamageDisplayType::Critical ||
		DamageType == EMT2DamageDisplayType::CriticalPenetrating)
	{
		HitSound = &Settings.CriticalHitSound;
	}
	else if (DamageType == EMT2DamageDisplayType::Penetrating)
	{
		HitSound = &Settings.PenetratingHitSound;
	}
	if (HitSound)
	{
		if (USoundBase* LoadedSound = HitSound->LoadSynchronous())
		{
			// Positional like every other hit sound, so it carries the same attenuation instead of
			// sitting flat on top of the mix.
			UMT2SoundPlaybackSubsystem::PlayExclusiveAtLocation(
				this, LoadedSound, GetActorLocation());
		}
	}
}

FString AMT2PlayerCharacter::BuildUnarmedAttackAnimationPath(int32 ComboIndex) const
{
	const AMT2PlayerState* MT2PlayerState = GetPlayerState<AMT2PlayerState>();
	if (!MT2PlayerState) return FString();
	const FMT2CharacterAppearance& Appearance = MT2PlayerState->GetCharacterAppearance();
	FString Race;
	switch (Appearance.Race)
	{
	case EMT2CharacterRace::Warrior: Race = TEXT("warrior"); break;
	case EMT2CharacterRace::Assassin: Race = TEXT("assassin"); break;
	case EMT2CharacterRace::Sura: Race = TEXT("sura"); break;
	case EMT2CharacterRace::Shaman: Race = TEXT("shaman"); break;
	default: return FString();
	}
	const bool bPrimaryFolder =
		(Appearance.Race == EMT2CharacterRace::Warrior && Appearance.Sex == EMT2CharacterSex::Male) ||
		(Appearance.Race == EMT2CharacterRace::Assassin && Appearance.Sex == EMT2CharacterSex::Female) ||
		(Appearance.Race == EMT2CharacterRace::Sura && Appearance.Sex == EMT2CharacterSex::Male) ||
		(Appearance.Race == EMT2CharacterRace::Shaman && Appearance.Sex == EMT2CharacterSex::Female);
	const TCHAR* Folder = bPrimaryFolder ? TEXT("pc") : TEXT("pc2");
	const TCHAR* Animation = (ComboIndex % 2) == 0 ? TEXT("A_attack") : TEXT("A_attack_1");
	return UMT2PathSettings::Format(TEXT("ymir_work_Name_Name_general_Name"), TEXT("%s%s%s%s"), Folder, *Race, Animation, Animation);
}

void AMT2PlayerCharacter::HandleBasicAttackPerformed(int32 ComboIndex)
{
	GetCombatComponent()->SetBasicAttackMotion(nullptr, 1.f);
	RegenerationComponent->MarkActivity();

	if (!GetMesh()) return;
	UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
	UAnimSequence* Sequence = nullptr;
	// Combat metadata must not depend on a rendered/live AnimInstance (absent on headless servers,
	// and reset asynchronously when equipment/body visuals change).
	const UMT2CharacterAnimInstance* MotionDefaults = nullptr;
	if (const auto* State = GetPlayerState<AMT2PlayerState>())
	{
		const auto& Appearance = State->GetCharacterAppearance();
		if (const auto* Profile = GetDefault<UMT2CharacterAppearanceSettings>()->FindAnimationProfile(Appearance.Race, Appearance.Sex))
		{
			UClass* AnimationClass = Profile->AnimInstanceClass.LoadSynchronous();
			MotionDefaults = AnimationClass ? Cast<UMT2CharacterAnimInstance>(AnimationClass->GetDefaultObject()) : nullptr;
		}
	}
	if (MotionDefaults) { Sequence = MotionDefaults->GetComboAnimation(GetAnimationSet(), ComboIndex); }
	if (!Sequence)
	{
		if (const auto* MT2AnimInstance = Cast<UMT2CharacterAnimInstance>(AnimInstance))
			Sequence = MT2AnimInstance->GetComboAnimation(GetAnimationSet(), ComboIndex);
	}
	if (!Sequence && GetAnimationSet() == TEXT("general"))
	{
		Sequence = LoadObject<UAnimSequence>(nullptr, *BuildUnarmedAttackAnimationPath(ComboIndex));
	}
	if (Sequence)
	{
		// Equivalent to CActorInstance::SetAdvancingRotation(fDirRot): capture one yaw for this
		// swing. Animation root rotation and lateral movement are not allowed to steer the actor.
		AttackFacingYaw = GetActorRotation().Yaw;
		bAttackFacingLocked = true;
		GetCharacterMovement()->bOrientRotationToMovement = false;

		// The generator keeps only animated root Z for foot contact and fixes root X/Y/rotation to
		// the skeleton reference basis. Capsule movement still comes from .msa accumulation below.
		Sequence->bEnableRootMotion = false;
		Sequence->RootMotionRootLock = ERootMotionRootLock::RefPose;
		Sequence->bForceRootLock = false;

		// The old client plays attack motions at AttackSpeed/100 (InstanceBaseMovement.cpp:6-12), so
		// gear/buff attack speed (e.g. Sword+0 = +22 -> 1.22x) speeds up both the swing animation and
		// the swing cadence. See Docs/OldGameResearch/AttackSpeedAndMovement.md.
		const UMT2CombatStatsComponent* Stats = GetCombatStatsComponent();
		const float PlayRate = FMath::Max(
			(Stats ? Stats->GetCalculatedStats().AttackSpeed : 100) / 100.0f, 0.1f);
		const UMT2AnimationMotionData* MotionData =
			Sequence->GetAssetUserData<UMT2AnimationMotionData>();
		UE_LOG(LogMT2Targeting, Verbose, TEXT("[MT2Knockback] %s combo=%d set=%s motion=%s force=%.1f hit=%d"),
			*GetName(), ComboIndex, *GetAnimationSet().ToString(), *Sequence->GetName(),
			MotionData ? MotionData->ExternalForce : 0.f, MotionData ? MotionData->HittingType : 0);
		const float AuthoredDuration = MotionData && MotionData->MotionDuration > UE_SMALL_NUMBER
			? MotionData->MotionDuration
			: Sequence->GetPlayLength();
		const float AttackDuration = AuthoredDuration / PlayRate;
		GetCombatComponent()->SetBasicAttackMotion(Sequence, PlayRate);
		const float InputWindow = MotionData && MotionData->bHasComboInputData && MotionData->DirectInputTime > UE_SMALL_NUMBER
			? MotionData->DirectInputTime / PlayRate
			: AttackDuration * 0.9f;

		// The autonomous client starts immediately for responsive combat. The authoritative server
		// multicasts the same action below to every simulated proxy and skips this predicted owner.
		if (!HasAuthority() && IsLocallyControlled() && AnimInstance && GetNetMode() != NM_DedicatedServer)
		{
			AnimInstance->PlaySlotAnimationAsDynamicMontage(
				Sequence, TEXT("DefaultSlot"), 0.2f, 0.2f, PlayRate, 1, -1.0f, 0.0f);
			GetMotionEffectComponent()->PlayMotionEffects(Sequence, PlayRate);
		}
		FVector WorldAdvance = FVector::ZeroVector;
		if (MotionData && MotionData->bHasAccumulation && InputWindow > UE_SMALL_NUMBER &&
			(!MountComponent || !MountComponent->IsMounted()))
		{
			// Source coordinates use -Y as forward. Keep the small authored lateral X displacement,
			// but resolve it once through the fixed advancing yaw, exactly like the old model matrix.
			const FVector LocalAdvance(-MotionData->Accumulation.Y, MotionData->Accumulation.X, 0.0f);
			WorldAdvance = FRotator(0.0, AttackFacingYaw, 0.0).RotateVector(LocalAdvance);
		}
		AttackAdvanceDirection = WorldAdvance.GetSafeNormal();
		AttackAdvanceSpeed = WorldAdvance.Size() / FMath::Max(InputWindow, 0.05f);
		AttackMotionEndTime = (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0) + InputWindow;

		// Legacy attack data drives each swing, including weaker pushes before the finisher.
		if (HasAuthority())
		{
			GetCombatComponent()->SetPendingKnockback(
				MotionData && MotionData->HittingType != 0 ? UMT2CombatComponent::MotionKnockbackDistance(MotionData->ExternalForce) : 0.f,
				UMT2CombatComponent::MotionKnockbackDuration, false, MotionData ? MotionData->HittingType : 2);
		}
		// Publish timing and displacement for combat/UI consumers. Actual movement is fed through
		// AddMovementInput in Tick so it follows the same network prediction as normal locomotion.
		GetCombatComponent()->NotifyAttackMotion(InputWindow, WorldAdvance);
		if (HasAuthority())
		{
			PlayReplicatedAnimationAction(
				TEXT("combo"), ComboIndex, PlayRate, GetAnimationSet(), InputWindow, true);
		}
	}
}

void AMT2PlayerCharacter::HandleHealthChanged(float OldValue, float NewValue)
{
	if (NewValue < OldValue)
	{
		RegenerationComponent->MarkActivity();
	}
}

void AMT2PlayerCharacter::ServerExecuteChatCommand_Implementation(const FString& Command)
{
	// Chat commands are GM-only, like the old server's level-gated interpreter table. The RPC is the
	// security boundary; the actual command set lives in the admin-command component.
	const AMT2PlayerState* AdminState = GetPlayerState<AMT2PlayerState>();
	if (!AdminState || !AdminState->IsAdmin())
	{
		UE_LOG(LogMT2Targeting, Warning,
			TEXT("[MT2Chat] %s tried '%s' without admin rights."),
			AdminState ? *AdminState->GetCharacterName() : TEXT("<unknown>"), *Command);
		return;
	}
	AdminCommandComponent->Execute(Command);
}

void AMT2PlayerCharacter::ServerUseSkill_Implementation(int32 SkillVnum)
{
	if (SkillVnum == 123) { FishingComponent->RequestToggleFishing(); return; }
	if (FishingComponent->HasRodEquipped()) { return; }
	// All the server-side decision logic (gates, cost, buffs, damage, cooldown/lock) lives in the
	// skill-cast component; the character only owns the shared attack-motion animation multicast.
	const FMT2SkillCastResult Result = SkillCastComponent->TryUseSkill(SkillVnum);
	if (!Result.bCast)
	{
		return;
	}

	// Old client NEW_LookAtDestInstance: turn to face the victim before the skill fires. The yaw is
	// resolved once here (the server has the selected target) so every client applies the same
	// facing. Self/no-target skills (buffs) keep their current facing.
	bool bFaceTarget = false;
	float TargetYaw = GetActorRotation().Yaw;
	if (const AActor* Target = GetCombatComponent()->GetSelectedTarget(); Target && Target != this)
	{
		const FVector ToTarget = Target->GetActorLocation() - GetActorLocation();
		if (ToTarget.SizeSquared2D() > KINDA_SMALL_NUMBER)
		{
			TargetYaw = ToTarget.Rotation().Yaw;
			bFaceTarget = true;
		}
	}
	MulticastPlaySkillAnimation(SkillVnum, Result.MasteryGrade, bFaceTarget, TargetYaw);
}

TMap<FString, double> AMT2PlayerCharacter::BuildSkillFormulaVariables(
	const UMT2SkillDefinition* Definition, int32 Level, EMT2SkillFormulaValue ValueType) const
{
	// Faithful port of the old server's per-skill CPoly variables (char_skill.cpp ComputeSkill ->
	// SetPointVar). Every variable an .skilldesc poly can reference is provided, so skill damage
	// scales with the character's stats exactly like the old game: physical skills through the
	// physical attack power (which folds in STR + level), magic skills through the magic attack
	// power (IQ + level), plus the raw stat terms and the hp/def references. The cast and the
	// tooltip both go through here, so the numbers shown match the numbers dealt.
	const FMT2PrimaryStats Stats = GetCalculatedPrimaryStats();
	const AMT2PlayerState* State = GetPlayerState<AMT2PlayerState>();
	const int32 CharacterLevel = State ? State->GetCharacterLevel() : 1;
	const UMT2CombatStatsComponent* CombatStats = GetCombatStatsComponent();
	const FMT2CombatStats Combat = CombatStats ? CombatStats->GetCalculatedStats() : FMT2CombatStats();
	const FMT2CombatStatBonuses& Bonuses = CombatStats
		? CombatStats->GetBonuses() : FMT2CombatStatBonuses();

	// The old client evaluates the tooltip's poly twice, forcing the weapon roll to its minimum and
	// then its maximum (CPoly RANDOM_TYPE_FORCE_MIN/MAX) to print a "min - max" range; an actual
	// cast rolls between them. WeaponDamage is the equipped weapon's rolled contribution.
	float WeaponDamage = 0.0f;
	if (Bonuses.DamageMax > 0.0f)
	{
		switch (ValueType)
		{
		case EMT2SkillFormulaValue::Min: WeaponDamage = Bonuses.DamageMin; break;
		case EMT2SkillFormulaValue::Max: WeaponDamage = Bonuses.DamageMax; break;
		default: WeaponDamage = FMath::FRandRange(Bonuses.DamageMin, Bonuses.DamageMax); break;
		}
	}

	// Combat.DamageMax is the old POINT_ATT_GRADE (2*level + stat attack, where stat attack is
	// 2*STR for warrior/sura, (4*STR+2*DEX)/3 for assassin, (4*STR+2*IQ)/3 for shaman - see
	// MT2PlayerStatFormula). Combat.MagicAttack is POINT_MAGIC_ATT_GRADE (2*level + 2*IQ). Both
	// already move with allocated stat points.
	const float PhysicalAttack = Combat.DamageMax + WeaponDamage;
	const float MagicAttack = Combat.MagicAttack + WeaponDamage;

	// `atk` is whichever attack power this skill's damage-type flag selects, mirroring ComputeSkill's
	// choice between CalcMeleeDamage / CalcMagicDamage / CalcArrowDamage.
	float AttackPower = PhysicalAttack;
	if (Definition && Definition->HasSkillFlag(EMT2SkillFlag::UseMagicDamage))
	{
		AttackPower = MagicAttack;
	}
	// (USE_ARROW_DAMAGE skills fall back to the physical attack power until bow damage is modelled.)

	// Old ar = CalcAttackRating(this, this): the caster's own rating, a 0..1 factor. Unlike basic
	// attacks, the skill damage pipeline does not re-apply it, so it's exposed here for polys that do.
	const float AttackRating = MT2PlayerStatFormula::CalculateAttackRating(
		CharacterLevel, Stats.Dexterity, CharacterLevel, Stats.Dexterity);
	const float MaxHealth = GetHealthComponent() ? GetHealthComponent()->GetMaxHealth() : 0.0f;
	const float MaxMana = GetManaComponent() ? GetManaComponent()->GetMaxMana() : 0.0f;

	TMap<FString, double> Variables;
	Variables.Add(TEXT("k"), Definition ? Definition->GetPowerFactor(Level) : 0.0);
	Variables.Add(TEXT("atk"), AttackPower);
	Variables.Add(TEXT("minatk"), AttackPower);
	Variables.Add(TEXT("maxatk"), AttackPower);
	Variables.Add(TEXT("mtk"), MagicAttack);
	Variables.Add(TEXT("wep"), WeaponDamage);
	Variables.Add(TEXT("minwep"), WeaponDamage);
	Variables.Add(TEXT("maxwep"), WeaponDamage);
	Variables.Add(TEXT("str"), Stats.Strength);
	Variables.Add(TEXT("dex"), Stats.Dexterity);
	Variables.Add(TEXT("con"), Stats.Constitution);
	Variables.Add(TEXT("iq"), Stats.Intelligence);
	Variables.Add(TEXT("lv"), CharacterLevel);
	Variables.Add(TEXT("maxhp"), MaxHealth);
	Variables.Add(TEXT("maxsp"), MaxMana);
	Variables.Add(TEXT("def"), Combat.Defense);
	Variables.Add(TEXT("odef"), Combat.Defense);   // no separate def-bonus split yet
	Variables.Add(TEXT("ar"), AttackRating);
	Variables.Add(TEXT("chain"), 0.0);
	Variables.Add(TEXT("horse_level"), 0.0);
	return Variables;
}

void AMT2PlayerCharacter::MulticastPlaySkillAnimation_Implementation(
	int32 SkillVnum, int32 MasteryGrade, bool bFaceTarget, float TargetYaw)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const AMT2PlayerState* State = GetPlayerState<AMT2PlayerState>();
	const UMT2SkillComponent* Skills = State ? State->GetSkillComponent() : nullptr;
	const UMT2SkillDefinition* Definition = Skills ? Skills->FindSkillDefinition(SkillVnum) : nullptr;
	if (!Definition)
	{
		return;
	}
	const bool bFemale = State->GetCharacterAppearance().Sex == EMT2CharacterSex::Female;
	UAnimSequence* CastAnimation =
		Cast<UAnimSequence>(Definition->GetCastAnimation(MasteryGrade, bFemale).LoadSynchronous());
	UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!CastAnimation || !AnimInstance)
	{
		return;
	}

	// Old client NEW_LookAtDestInstance: snap to face the victim before the motion starts, so the
	// swing (and its authored forward advance) points at the target. The facing lock below then
	// holds this yaw for the whole cast.
	if (bFaceTarget)
	{
		SetActorRotation(FRotator(0.0f, TargetYaw, 0.0f));
	}

	// Same treatment as the basic-attack motions: capture one facing yaw for the whole motion,
	// strip root motion (the generator bakes root XY/rotation to the reference basis), and drive
	// the capsule from the .msa accumulation instead - see PerformCombo.
	AttackFacingYaw = GetActorRotation().Yaw;
	bAttackFacingLocked = true;
	GetCharacterMovement()->bOrientRotationToMovement = false;
	CastAnimation->bEnableRootMotion = false;
	CastAnimation->RootMotionRootLock = ERootMotionRootLock::RefPose;
	CastAnimation->bForceRootLock = false;

	AnimInstance->PlaySlotAnimationAsDynamicMontage(CastAnimation, TEXT("DefaultSlot"), 0.2f, 0.2f);
	GetMotionEffectComponent()->PlayMotionEffects(CastAnimation);

	const UMT2AnimationMotionData* MotionData = CastAnimation->GetAssetUserData<UMT2AnimationMotionData>();
	const float MotionWindow = MotionData && MotionData->MotionDuration > UE_SMALL_NUMBER
		? MotionData->MotionDuration : CastAnimation->GetPlayLength();
	FVector WorldAdvance = FVector::ZeroVector;
	if (MotionData && MotionData->bHasAccumulation && MotionWindow > UE_SMALL_NUMBER)
	{
		// Source coordinates use -Y as forward; resolve once through the locked yaw.
		const FVector LocalAdvance(-MotionData->Accumulation.Y, MotionData->Accumulation.X, 0.0f);
		WorldAdvance = FRotator(0.0, AttackFacingYaw, 0.0).RotateVector(LocalAdvance);
	}
	AttackAdvanceDirection = WorldAdvance.GetSafeNormal();
	AttackAdvanceSpeed = WorldAdvance.Size() / FMath::Max(MotionWindow, 0.05f);
	AttackMotionEndTime = (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0) + MotionWindow;
	GetCombatComponent()->NotifyAttackMotion(MotionWindow, WorldAdvance);
}

void AMT2PlayerCharacter::ClientNotifySkillCooldown_Implementation(int32 SkillVnum, float CooldownSeconds)
{
	const AMT2PlayerState* State = GetPlayerState<AMT2PlayerState>();
	if (UMT2SkillComponent* Skills = State ? State->GetSkillComponent() : nullptr)
	{
		Skills->NotifyCooldownStarted(SkillVnum, CooldownSeconds);
	}
}

void AMT2PlayerCharacter::ClientNotifySkillDenied_Implementation(
	int32 SkillVnum, EMT2SkillDenyReason Reason)
{
	// locale_game.txt CANNOT_SKILL_* - the same wording the old client shows.
	const TCHAR* Message = TEXT("I cannot use this skill.");
	switch (Reason)
	{
	case EMT2SkillDenyReason::NeedTarget: Message = TEXT("Who is the target?"); break;
	case EMT2SkillDenyReason::NotMatchableWeapon:
		Message = TEXT("I cannot use this skill with this weapon."); break;
	case EMT2SkillDenyReason::WaitCooltime: Message = TEXT("I cannot use this skill yet."); break;
	case EMT2SkillDenyReason::NotEnoughSP: Message = TEXT("I do not have enough SP!"); break;
	case EMT2SkillDenyReason::NotEnoughHP: Message = TEXT("I do not have enough HP!"); break;
	case EMT2SkillDenyReason::MustRide:
		Message = TEXT("I must be riding a military mount to use this skill."); break;
	case EMT2SkillDenyReason::CannotUseMounted:
		Message = TEXT("I cannot use this skill while mounted."); break;
	}
	if (AMT2PlayerController* PlayerController = Cast<AMT2PlayerController>(GetController()))
	{
		PlayerController->AddInfoChatLine(Message);
	}
	UE_LOG(LogMT2Targeting, Verbose, TEXT("[MT2Skill] %d denied: %s"), SkillVnum, Message);
}

void AMT2PlayerCharacter::SetInteractionUIOpen(bool bOpen)
{
	InteractionUIOpenCount = FMath::Max(0, InteractionUIOpenCount + (bOpen ? 1 : -1));
	if (bOpen)
	{
		GetCharacterMovement()->StopMovementImmediately();
		AutoMoveTargetActor.Reset();
		bHasAutoMoveTarget = false;
	}
}

void AMT2PlayerCharacter::ServerInteractNpc_Implementation(AMT2Npc* Npc)
{
	if (Npc && Npc->GetInteractionComponent())
	{
		Npc->GetInteractionComponent()->Interact(this);
	}
}

void AMT2PlayerCharacter::ServerUseInventoryItemOnActor_Implementation(
	AActor* TargetActor, int32 InventorySlot)
{
	if (!IsValid(TargetActor) || TargetActor == this || IsDead() || TargetActor->GetWorld() != GetWorld() ||
		(!Cast<AMT2Npc>(TargetActor) && !Cast<AMT2PlayerCharacter>(TargetActor)) ||
		FVector::Dist2D(GetActorLocation(), TargetActor->GetActorLocation()) > 350.0f ||
		!InventoryComponent)
	{
		return;
	}

	const TArray<FMT2ItemSlot>& Slots = InventoryComponent->GetSlots();
	if (!Slots.IsValidIndex(InventorySlot) || Slots[InventorySlot].IsEmpty())
	{
		return;
	}

	if (AMT2PlayerCharacter* TargetPlayer = Cast<AMT2PlayerCharacter>(TargetActor))
	{
		if (AMT2PlayerState* State = GetPlayerState<AMT2PlayerState>())
		{
			if (UMT2TradeComponent* Trade = State->GetTradeComponent())
			{
				Trade->ServerRequestTradeWithItem(TargetPlayer, InventorySlot);
			}
		}
		return;
	}

	if (AMT2Npc* Npc = Cast<AMT2Npc>(TargetActor);
		Npc && UMT2GameplaySettings::Get().RefinementBlacksmithVnums.Contains(
			Npc->GetMobVnum()))
	{
		if (InventoryComponent->CanRefineItem(InventorySlot))
		{
			ClientOpenRefinementDialog(InventorySlot, INDEX_NONE, Npc);
		}
		return;
	}

	if (AMT2Npc* Npc = Cast<AMT2Npc>(TargetActor))
	{
		AMT2PlayerState* State = GetPlayerState<AMT2PlayerState>();
		UMT2QuestManagerComponent* Manager = State ? State->GetQuestManagerComponent() : nullptr;
		if (State && State->GetTradeComponent() && State->GetTradeComponent()->IsTrading()) { return; }
		if (Manager && Manager->DispatchEvent(EMT2QuestEvent::ItemTake, Npc->GetMobVnum(), Npc, InventorySlot)) { return; }
	}
	OnInventoryItemActorInteractionRequested.Broadcast(
		TargetActor, InventorySlot, Slots[InventorySlot].Vnum);
}

void AMT2PlayerCharacter::ServerRefineInventoryItem_Implementation(
	int32 TargetSlot, int32 ScrollSlot, AMT2Npc* Blacksmith)
{
	if (!InventoryComponent)
	{
		ClientNotifyRefinementResult(EMT2RefinementResult::Invalid);
		return;
	}

	const bool bUsingScroll = ScrollSlot != INDEX_NONE;
	if (!bUsingScroll &&
		(!Blacksmith ||
			!UMT2GameplaySettings::Get().RefinementBlacksmithVnums.Contains(
				Blacksmith->GetMobVnum()) ||
			FVector::Dist2D(GetActorLocation(), Blacksmith->GetActorLocation()) > 350.0f))
	{
		ClientNotifyRefinementResult(EMT2RefinementResult::Invalid);
		return;
	}

	ClientNotifyRefinementResult(
		InventoryComponent->RefineItem(TargetSlot, ScrollSlot));
}

void AMT2PlayerCharacter::ClientOpenRefinementDialog_Implementation(
	int32 TargetSlot, int32 ScrollSlot, AMT2Npc* Blacksmith)
{
	OpenRefinementDialogLocal(TargetSlot, ScrollSlot, Blacksmith);
}

void AMT2PlayerCharacter::OpenRefinementDialogLocal(
	int32 TargetSlot, int32 ScrollSlot, AMT2Npc* Blacksmith)
{
	if (!IsLocallyControlled() || !InventoryComponent ||
		!InventoryComponent->CanRefineItem(TargetSlot))
	{
		return;
	}
	if (!RefinementDialogWidget)
	{
		RefinementDialogWidget = CreateWidget<UMT2RefinementDialogWidget>(
			GetController<APlayerController>(),
			UMT2RefinementDialogWidget::StaticClass());
		if (RefinementDialogWidget)
		{
			RefinementDialogWidget->AddToViewport(120);
		}
	}
	if (RefinementDialogWidget)
	{
		RefinementDialogWidget->OpenRefinement(
			TargetSlot, ScrollSlot, Blacksmith);
	}
}

void AMT2PlayerCharacter::ClientNotifyRefinementResult_Implementation(
	EMT2RefinementResult Result)
{
	const TCHAR* Message = nullptr;
	switch (Result)
	{
	case EMT2RefinementResult::Success:
		Message = TEXT("Item refinement succeeded."); break;
	case EMT2RefinementResult::FailedDestroyed:
		Message = TEXT("Item refinement failed. The item was destroyed."); break;
	case EMT2RefinementResult::FailedDowngraded:
		Message = TEXT("Item refinement failed. Refinement decreased by one level."); break;
	case EMT2RefinementResult::FailedAtMinimum:
		Message = TEXT("Item refinement failed. The item remains at +0."); break;
	case EMT2RefinementResult::NotEnoughYang:
		Message = TEXT("Not enough Yang."); break;
	case EMT2RefinementResult::MissingMaterials:
		Message = TEXT("Required refinement materials are missing."); break;
	case EMT2RefinementResult::NoInventorySpace:
		Message = TEXT("There is not enough inventory space for the upgraded item."); break;
	default:
		Message = TEXT("This item cannot be refined."); break;
	}
	if (AMT2PlayerController* PlayerController =
		Cast<AMT2PlayerController>(Controller))
	{
		PlayerController->AddInfoChatLine(Message);
	}

	const bool bSuccess = Result == EMT2RefinementResult::Success;
	const bool bFailure =
		Result == EMT2RefinementResult::FailedDestroyed ||
		Result == EMT2RefinementResult::FailedDowngraded ||
		Result == EMT2RefinementResult::FailedAtMinimum;
	if (bSuccess || bFailure)
	{
		PlayRefinementOutcomeSound(this, bSuccess);
	}
}

void AMT2PlayerCharacter::ClientOpenNpcShop_Implementation(AMT2Npc* Npc)
{
	if (!Npc || !IsLocallyControlled())
	{
		return;
	}
	if (!ShopWidget)
	{
		ShopWidget = CreateWidget<UMT2ShopWidget>(GetWorld(), UMT2ShopWidget::StaticClass());
		if (ShopWidget)
		{
			ShopWidget->AddToViewport(60);
		}
	}
	if (ShopWidget)
	{
		ShopWidget->OpenShop(Npc);
	}

	// The inventory opens alongside the shop so the player can buy/sell without opening it manually.
	const AMT2HUD* MT2HUD = Cast<AMT2HUD>(GetController<APlayerController>()
		? GetController<APlayerController>()->GetHUD() : nullptr);
	UMT2GameHUDWidget* GameHUD = MT2HUD ? MT2HUD->GetGameHUDWidget() : nullptr;
	if (UMT2InventoryWidget* Inventory = GameHUD ? GameHUD->GetInventoryWidget() : nullptr)
	{
		Inventory->OpenInventory();
	}
}

void AMT2PlayerCharacter::ServerShopBuy_Implementation(AMT2Npc* Npc, int32 EntryIndex, int32 Count)
{
	if (Npc && Npc->GetShopComponent())
	{
		Npc->GetShopComponent()->BuyItem(this, EntryIndex, Count);
	}
}

void AMT2PlayerCharacter::ServerShopSell_Implementation(AMT2Npc* Npc, int32 InventorySlot, int32 Count)
{
	if (Npc && Npc->GetShopComponent())
	{
		Npc->GetShopComponent()->SellItem(this, InventorySlot, Count);
	}
}

void AMT2PlayerCharacter::ServerUseInventoryItemByVnum_Implementation(int32 Vnum)
{
	const UMT2InventoryComponent* Inventory = GetInventoryComponent();
	if (!Inventory || Vnum <= 0)
	{
		return;
	}
	const TArray<FMT2ItemSlot>& Slots = Inventory->GetSlots();
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		if (Slots[Index].Vnum == Vnum && Slots[Index].Count > 0)
		{
			ServerUseInventoryItem_Implementation(Index);
			return;
		}
	}
}

void AMT2PlayerCharacter::ServerReadSkillBook_Implementation(int32 InventorySlot)
{
	AMT2PlayerState* State = GetPlayerState<AMT2PlayerState>();
	UMT2SkillComponent* Skills = State ? State->GetSkillComponent() : nullptr;
	UMT2InventoryComponent* Inventory = GetInventoryComponent();
	AMT2PlayerController* MT2Controller = Cast<AMT2PlayerController>(GetController());
	if (!Skills || !Inventory)
	{
		return;
	}

	const UMT2ItemSkillBookTemplate* Book =
		Cast<UMT2ItemSkillBookTemplate>(Inventory->GetTemplateAtSlot(InventorySlot));
	if (!Book)
	{
		return; // not a skill book (or empty slot)
	}
	const TArray<FMT2ItemSlot>& InventorySlots = Inventory->GetSlots();
	if (!InventorySlots.IsValidIndex(InventorySlot))
	{
		return;
	}
	// Generic 50300 books store the rolled skill per instance, as socket 0 did in the old server.
	const int32 SkillVnum = InventorySlots[InventorySlot].SkillVnum > 0
		? InventorySlots[InventorySlot].SkillVnum : Book->SkillVnum;
	if (SkillVnum <= 0)
	{
		if (MT2Controller)
		{
			MT2Controller->SendSystemChatMessage(TEXT("This skill book has no skill assigned."));
		}
		return;
	}
	const UMT2SkillDefinition* Definition = Skills->FindSkillDefinition(SkillVnum);
	const FString SkillName = Definition
		? Definition->GetGradeDisplayName(0).ToString() : FString::Printf(TEXT("skill #%d"), SkillVnum);

	auto Say = [MT2Controller](const FString& Message)
	{
		if (MT2Controller)
		{
			MT2Controller->SendSystemChatMessage(Message);
		}
	};

	// Not eligible -> tell the player exactly why (wrong race/class, not Master yet, ...) and keep the book.
	if (const FText DenyReason = Skills->GetSkillBookDenyReason(SkillVnum); !DenyReason.IsEmpty())
	{
		Say(DenyReason.ToString());
		return;
	}

	const int32 PreviousLevel = Skills->GetSkillLevel(SkillVnum);
	Inventory->ConsumeItemAtSlot(InventorySlot);
	int32 ReadsRemaining = 0;
	const EMT2SkillBookResult Result = Skills->LearnSkillByBook(SkillVnum, ReadsRemaining);
	const FString From = MT2SkillMastery::LevelLabel(PreviousLevel);
	const FString To = MT2SkillMastery::LevelLabel(Skills->GetSkillLevel(SkillVnum));

	switch (Result)
	{
	case EMT2SkillBookResult::LeveledUp:
		if (AMT2PlayerState* PS = GetPlayerState<AMT2PlayerState>())
		{
			PS->ForceNetUpdate(); // push the new level so the client UI can refresh right away
		}
		ClientRefreshSkillsUI();
		Say(FString::Printf(TEXT("[Skill] %s: read succeeded! %s -> %s"), *SkillName, *From, *To));
		break;
	case EMT2SkillBookResult::Progressed:
		Say(FString::Printf(
			TEXT("[Skill] %s: read succeeded (%s, %d more to level up)."), *SkillName, *From, ReadsRemaining));
		break;
	case EMT2SkillBookResult::Failed:
		Say(FString::Printf(TEXT("[Skill] %s: read failed. %s -> %s"), *SkillName, *From, *To));
		break;
	default:
		Say(FString::Printf(TEXT("You cannot read the %s book right now."), *SkillName));
		break;
	}
}

void AMT2PlayerCharacter::ClientRefreshSkillsUI_Implementation()
{
	const AMT2PlayerState* State = GetPlayerState<AMT2PlayerState>();
	if (UMT2SkillComponent* Skills = State ? State->GetSkillComponent() : nullptr)
	{
		Skills->NotifySkillLevelsChanged();
	}
}

bool AMT2PlayerCharacter::BeginGrandMasterTraining(int32 InventorySlot)
{
	if (!HasAuthority())
	{
		return false;
	}
	AMT2PlayerState* State = GetPlayerState<AMT2PlayerState>();
	UMT2InventoryComponent* Inventory = GetInventoryComponent();
	UMT2SkillComponent* Skills = State ? State->GetSkillComponent() : nullptr;
	UMT2QuestComponent* Quests = State ? State->GetQuestComponent() : nullptr;
	if (!State || !Inventory || !Skills || !Quests ||
		!Cast<UMT2GrandMasterTrainingItemTemplate>(
			Inventory->GetTemplateAtSlot(InventorySlot)))
	{
		return false;
	}

	TArray<int32> EligibleSkills;
	FMT2DialogPayload Dialog;
	Dialog.Title = TEXT("Grand Master Training");
	Dialog.TextLines.Add(TEXT("Choose the Grand Master skill to improve."));
	for (const FMT2SkillLevelEntry& Entry : Skills->GetSkillLevels())
	{
		if (!Skills->CanTrainGrandMasterSkill(Entry.SkillVnum))
		{
			continue;
		}
		EligibleSkills.Add(Entry.SkillVnum);
		const UMT2SkillDefinition* Definition = Skills->FindSkillDefinition(Entry.SkillVnum);
		const FString SkillName = Definition
			? Definition->GetGradeDisplayName(2).ToString()
			: FString::Printf(TEXT("Skill %d"), Entry.SkillVnum);
		Dialog.Options.Add(FString::Printf(
			TEXT("%s (%s)"), *SkillName, *MT2SkillMastery::LevelLabel(Entry.Level)));
	}
	if (EligibleSkills.IsEmpty())
	{
		Dialog.TextLines = {TEXT("You have no skills between G1 and G10.")};
		Quests->ShowDialog(Dialog, [](int32) {});
		return true;
	}

	Dialog.Options.Add(TEXT("Cancel"));
	Quests->ShowDialog(
		Dialog,
		[WeakThis = TWeakObjectPtr<AMT2PlayerCharacter>(this),
		 InventorySlot, EligibleSkills](int32 ChoiceIndex)
		{
			if (WeakThis.IsValid())
			{
				WeakThis->ResolveGrandMasterTrainingSelection(
					InventorySlot, EligibleSkills, ChoiceIndex);
			}
		});
	return true;
}

void AMT2PlayerCharacter::ResolveGrandMasterTrainingSelection(
	int32 InventorySlot, const TArray<int32>& SkillVnums, int32 ChoiceIndex)
{
	if (!HasAuthority() || ChoiceIndex <= 0 || ChoiceIndex > SkillVnums.Num())
	{
		return;
	}
	AMT2PlayerState* State = GetPlayerState<AMT2PlayerState>();
	UMT2InventoryComponent* Inventory = GetInventoryComponent();
	UMT2SkillComponent* Skills = State ? State->GetSkillComponent() : nullptr;
	UMT2QuestComponent* Quests = State ? State->GetQuestComponent() : nullptr;
	const int32 SkillVnum = SkillVnums[ChoiceIndex - 1];
	if (!State || !Inventory || !Skills || !Quests ||
		!Cast<UMT2GrandMasterTrainingItemTemplate>(
			Inventory->GetTemplateAtSlot(InventorySlot)) ||
		!Skills->CanTrainGrandMasterSkill(SkillVnum))
	{
		return;
	}

	const int32 GrandMasterLevel = Skills->GetSkillLevel(SkillVnum) - 29;
	const int32 FailurePenalty = GrandMasterLevel * 1000;
	FMT2DialogPayload ResultDialog;
	ResultDialog.Title = TEXT("Grand Master Training");
	if (State->GetKarmaPoints() <= MT2KarmaLimits::Minimum + FailurePenalty)
	{
		ResultDialog.TextLines.Add(FString::Printf(
			TEXT("You need more karma to train this G%d skill."), GrandMasterLevel));
		Quests->ShowDialog(ResultDialog, [](int32) {});
		return;
	}

	if (!Inventory->ConsumeItemAtSlot(InventorySlot))
	{
		return;
	}
	const bool bSucceeded = Skills->TrainGrandMasterSkill(SkillVnum);
	const int32 KarmaPenalty = FailurePenalty * (bSucceeded ? 2 : 1);
	State->ChangeKarmaPoints(-KarmaPenalty);

	const UMT2SkillDefinition* Definition = Skills->FindSkillDefinition(SkillVnum);
	const FString SkillName = Definition
		? Definition->GetGradeDisplayName(2).ToString()
		: FString::Printf(TEXT("Skill %d"), SkillVnum);
	ResultDialog.TextLines.Add(bSucceeded
		? FString::Printf(TEXT("%s improved to %s."), *SkillName,
			*MT2SkillMastery::LevelLabel(Skills->GetSkillLevel(SkillVnum)))
		: FString::Printf(TEXT("%s training failed."), *SkillName));
	ResultDialog.TextLines.Add(FString::Printf(
		TEXT("Karma decreased by %d."), KarmaPenalty));
	Quests->ShowDialog(ResultDialog, [](int32) {});
}

void AMT2PlayerCharacter::ServerMoveInventoryItem_Implementation(int32 FromSlot, int32 ToSlot)
{
	UMT2InventoryComponent* Inventory = GetInventoryComponent();
	const TArray<FMT2ItemSlot>& Slots = Inventory->GetSlots();
	const int32 Vnum = Slots.IsValidIndex(FromSlot) ? Slots[FromSlot].Vnum : 0;
	if (Vnum > 0 && Inventory->MoveItem(FromSlot, ToSlot))
	{
		ClientConfirmInventoryAction(Vnum, false);
	}
}

void AMT2PlayerCharacter::ServerApplyInventoryBonusItem_Implementation(
	int32 SourceSlot, int32 TargetSlot, bool bTargetEquipped)
{
	UMT2InventoryComponent* Inventory = GetInventoryComponent();
	const TArray<FMT2ItemSlot>& Slots = Inventory->GetSlots();
	const int32 Vnum = Slots.IsValidIndex(SourceSlot) ? Slots[SourceSlot].Vnum : 0;
	if (Vnum <= 0)
	{
		return;
	}

	const EMT2ItemBonusApplyResult Result =
		Inventory->ApplyBonusConsumable(SourceSlot, TargetSlot, bTargetEquipped);
	if (Result != EMT2ItemBonusApplyResult::Invalid)
	{
		ClientConfirmInventoryAction(Vnum, true);
	}

	const UMT2ItemBonusSettings* Settings = GetDefault<UMT2ItemBonusSettings>();
	const bool bRandomizer = Settings &&
		(Vnum == Settings->ChangeNormalBonusesItemVnum ||
		 Vnum == Settings->ChangeRareBonusesItemVnum);
	ClientNotifyInventoryBonusResult(
		bRandomizer, Result == EMT2ItemBonusApplyResult::Success);
}

void AMT2PlayerCharacter::ServerApplyMetinStone_Implementation(
	int32 SourceSlot, int32 TargetSlot, bool bTargetEquipped)
{
	UMT2InventoryComponent* Inventory = GetInventoryComponent();
	const TArray<FMT2ItemSlot>& Slots = Inventory->GetSlots();
	const int32 Vnum = Slots.IsValidIndex(SourceSlot) ? Slots[SourceSlot].Vnum : 0;
	const EMT2MetinSocketApplyResult Result =
		Inventory->ApplyMetinStone(SourceSlot, TargetSlot, bTargetEquipped);
	if (Result == EMT2MetinSocketApplyResult::Success ||
		Result == EMT2MetinSocketApplyResult::Failed)
	{
		ClientConfirmInventoryAction(Vnum, true);
	}
	ClientNotifyMetinSocketResult(Result);
}

void AMT2PlayerCharacter::ClientNotifyMetinSocketResult_Implementation(
	EMT2MetinSocketApplyResult Result)
{
	const TCHAR* Message = nullptr;
	switch (Result)
	{
	case EMT2MetinSocketApplyResult::Success:
		Message = TEXT("Metin stone attachment succeeded."); break;
	case EMT2MetinSocketApplyResult::Failed:
		Message = TEXT("Metin stone attachment failed."); break;
	case EMT2MetinSocketApplyResult::NoSocket:
		Message = TEXT("This item has no compatible empty socket."); break;
	case EMT2MetinSocketApplyResult::Incompatible:
		Message = TEXT("This Metin stone cannot be attached to that item."); break;
	case EMT2MetinSocketApplyResult::DuplicateStone:
		Message = TEXT("A Metin stone of this type is already attached."); break;
	default:
		Message = TEXT("The Metin stone could not be attached."); break;
	}
	if (AMT2PlayerController* PlayerController = Cast<AMT2PlayerController>(Controller))
	{
		PlayerController->AddInfoChatLine(Message);
	}

	if (Result == EMT2MetinSocketApplyResult::Success ||
		Result == EMT2MetinSocketApplyResult::Failed)
	{
		PlayRefinementOutcomeSound(
			this,
			Result == EMT2MetinSocketApplyResult::Success);
	}
}

void AMT2PlayerCharacter::ServerOpenInventoryLootCrate_Implementation(int32 CrateSlot, int32 KeySlot)
{
	UMT2InventoryComponent* Inventory = GetInventoryComponent();
	const TArray<FMT2ItemSlot>& Slots = Inventory->GetSlots();
	const int32 CrateVnum = Slots.IsValidIndex(CrateSlot) ? Slots[CrateSlot].Vnum : 0;
	const EMT2LootCrateOpenResult Result = Inventory->OpenLootCrate(CrateSlot, KeySlot);
	if (Result == EMT2LootCrateOpenResult::Success)
	{
		ClientConfirmInventoryAction(CrateVnum, true);
	}
	ClientNotifyLootCrateResult(Result);
}

void AMT2PlayerCharacter::ClientNotifyLootCrateResult_Implementation(EMT2LootCrateOpenResult Result)
{
	const TCHAR* Message = nullptr;
	switch (Result)
	{
	case EMT2LootCrateOpenResult::Success: Message = TEXT("Loot crate opened."); break;
	case EMT2LootCrateOpenResult::LevelTooLow:
		Message = TEXT("Your level is too low to open this loot crate."); break;
	case EMT2LootCrateOpenResult::KeyRequired: Message = TEXT("This loot crate requires a key."); break;
	case EMT2LootCrateOpenResult::WrongKey: Message = TEXT("This key does not fit the loot crate."); break;
	case EMT2LootCrateOpenResult::NoRewards: Message = TEXT("This loot crate has no rewards configured."); break;
	default: Message = TEXT("The loot crate could not be opened."); break;
	}
	if (AMT2PlayerController* PlayerController = Cast<AMT2PlayerController>(Controller))
	{
		PlayerController->AddInfoChatLine(Message);
	}
}

void AMT2PlayerCharacter::ClientNotifyInventoryBonusResult_Implementation(
	bool bRandomizer, bool bSucceeded)
{
	const TCHAR* Action = bRandomizer ? TEXT("Bonus randomization") : TEXT("Bonus addition");
	const TCHAR* Outcome = bSucceeded ? TEXT("succeeded.") : TEXT("failed.");
	if (AMT2PlayerController* PlayerController = Cast<AMT2PlayerController>(Controller))
	{
		PlayerController->AddInfoChatLine(FString::Printf(TEXT("%s %s"), Action, Outcome));
	}
}

void AMT2PlayerCharacter::ServerEquipInventoryItem_Implementation(int32 InventorySlot)
{
	UMT2InventoryComponent* Inventory = GetInventoryComponent();
	const TArray<FMT2ItemSlot>& Slots = Inventory->GetSlots();
	const int32 Vnum = Slots.IsValidIndex(InventorySlot) ? Slots[InventorySlot].Vnum : 0;
	if (Vnum > 0 && Inventory->EquipItemFromSlot(InventorySlot))
	{
		ClientConfirmInventoryAction(Vnum, true);
	}
}

void AMT2PlayerCharacter::ServerUnequipItem_Implementation(int32 WearPosition)
{
	UMT2InventoryComponent* Inventory = GetInventoryComponent();
	const TArray<FMT2ItemSlot>& Equipment = Inventory->GetEquipment();
	const int32 Vnum = Equipment.IsValidIndex(WearPosition) ? Equipment[WearPosition].Vnum : 0;
	if (Vnum > 0 && Inventory->UnequipItem(WearPosition))
	{
		ClientConfirmInventoryAction(Vnum, false);
	}
}

void AMT2PlayerCharacter::ServerUseInventoryItem_Implementation(int32 InventorySlot)
{
	UMT2InventoryComponent* Inventory = GetInventoryComponent();
	const TArray<FMT2ItemSlot>& Slots = Inventory->GetSlots();
	const int32 Vnum = Slots.IsValidIndex(InventorySlot) ? Slots[InventorySlot].Vnum : 0;
	if (Vnum > 0 && Inventory->UseItem(InventorySlot))
	{
		ClientConfirmInventoryAction(Vnum, true);
	}
}

void AMT2PlayerCharacter::ServerDropInventoryItem_Implementation(int32 InventorySlot, int32 Count)
{
	UMT2InventoryComponent* Inventory = GetInventoryComponent();
	if (!Inventory || (GetHealthComponent() && GetHealthComponent()->IsDead()))
	{
		return;
	}

	if (!Inventory->GetSlots().IsValidIndex(InventorySlot) ||
		Inventory->GetSlots()[InventorySlot].IsEmpty())
	{
		return;
	}

	FMT2ItemSlot DroppedItem;
	if (!Inventory->ExtractItemForDrop(InventorySlot, Count, DroppedItem))
	{
		UE_LOG(LogMT2Targeting, Warning,
			TEXT("[MT2ItemDrop] Rejected slot=%d count=%d for %s."),
			InventorySlot, Count, *GetNameSafe(this));
		ClientNotifyItemDropFailed(TEXT("This item cannot be dropped."));
		return;
	}

	AMT2WorldItem* WorldItem = AMT2WorldItem::SpawnWorldItemInstance(
		GetWorld(), GetActorLocation(), DroppedItem, this);
	if (!WorldItem)
	{
		UE_LOG(LogMT2Targeting, Error,
			TEXT("[MT2ItemDrop] Failed to spawn vnum=%d count=%d for %s; restoring inventory."),
			DroppedItem.Vnum, DroppedItem.Count, *GetNameSafe(this));
		Inventory->AddItemSlotPartial(DroppedItem);
		ClientNotifyItemDropFailed(TEXT("The item could not be dropped and was returned to your inventory."));
		return;
	}
}

void AMT2PlayerCharacter::ClientNotifyItemDropFailed_Implementation(const FString& Message)
{
	if (AMT2PlayerController* PlayerController = Cast<AMT2PlayerController>(Controller))
	{
		PlayerController->AddInfoChatLine(Message);
	}
}

void AMT2PlayerCharacter::RequestPickupNearbyItems()
{
	if (IsLocallyControlled() || HasAuthority())
	{
		ServerPickupNearbyItems();
	}
}

void AMT2PlayerCharacter::RequestPickupWorldItem(AMT2WorldItem* WorldItem)
{
	if ((IsLocallyControlled() || HasAuthority()) && IsValid(WorldItem))
	{
		ServerPickupWorldItem(WorldItem);
	}
}

void AMT2PlayerCharacter::ServerPickupWorldItem_Implementation(AMT2WorldItem* WorldItem)
{
	TryPickupWorldItemOnServer(WorldItem);
}

void AMT2PlayerCharacter::TryPickupWorldItemOnServer(AMT2WorldItem* WorldItem)
{
	constexpr float PickupRadius = 1000.0f;
	if (!IsValid(WorldItem) || (GetHealthComponent() && GetHealthComponent()->IsDead()) ||
		FVector::DistSquared2D(WorldItem->GetActorLocation(), GetActorLocation())
			> FMath::Square(PickupRadius))
	{
		return;
	}

	const int32 Vnum = WorldItem->GetItemVnum();
	AMT2PlayerCharacter* ReceivingCharacter = nullptr;
	if (WorldItem->TryPickup(this, &ReceivingCharacter) > 0 && ReceivingCharacter)
	{
		ReceivingCharacter->ClientConfirmInventoryAction(Vnum, false);
	}
}

void AMT2PlayerCharacter::ServerPickupNearbyItems_Implementation()
{
	if (GetHealthComponent() && GetHealthComponent()->IsDead())
	{
		return;
	}

	constexpr float PickupRadius = 1000.0f;
	const float PickupRadiusSquared = FMath::Square(PickupRadius);
	TArray<AMT2WorldItem*> NearbyItems;
	for (TActorIterator<AMT2WorldItem> It(GetWorld()); It; ++It)
	{
		AMT2WorldItem* WorldItem = *It;
		if (IsValid(WorldItem) && FVector::DistSquared2D(
			WorldItem->GetActorLocation(), GetActorLocation()) <= PickupRadiusSquared)
		{
			NearbyItems.Add(WorldItem);
		}
	}

	for (AMT2WorldItem* WorldItem : NearbyItems)
	{
		if (!IsValid(WorldItem))
		{
			continue;
		}
		TryPickupWorldItemOnServer(WorldItem);
	}
}

void AMT2PlayerCharacter::ClientConfirmInventoryAction_Implementation(int32 Vnum, bool bUseSound)
{
	OnInventoryActionConfirmed.Broadcast(Vnum, bUseSound);
}

void AMT2PlayerCharacter::HandleEquipmentChanged()
{
	if (HasAuthority())
	{
		if (AMT2PlayerState* State = GetPlayerState<AMT2PlayerState>())
		{
			// UNIQUE_ITEM_HIDE_ALIGNMENT_TITLE masks presentation, never real alignment.
			const auto& Worn = GetInventoryComponent()->GetEquipment();
			State->SetAlignmentHidden(Worn.ContainsByPredicate([](const FMT2ItemSlot& Slot)
				{ return Slot.Vnum == 70048 && Slot.Count > 0; }));
		}
	}
	// Apply worn weapon/body meshes from the equipment slots, then recompute stat bonuses. WEAR_WEAPON=4,
	// WEAR_BODY=0 (old client EWearPositions). Armor equips a whole-body skeletal mesh; weapons attach.
	UGameInstance* GameInstance = GetGameInstance();
	UMT2VnumRegistrySubsystem* Registry = GameInstance ? GameInstance->GetSubsystem<UMT2VnumRegistrySubsystem>() : nullptr;
	if (Registry && GetEquipmentComponent())
	{
		const TArray<FMT2ItemSlot>& Worn = GetInventoryComponent()->GetEquipment();
		constexpr int32 WearBody = 0;
		constexpr int32 WearWeapon = 4;

		const FMT2ItemSlot& WeaponSlot = Worn.IsValidIndex(WearWeapon) ? Worn[WearWeapon] : FMT2ItemSlot();
		if (const UMT2ItemWeaponTemplate* Weapon = Cast<UMT2ItemWeaponTemplate>(Registry->ResolveItemTemplateClass(WeaponSlot.Vnum).GetDefaultObject()))
		{
			const AMT2PlayerState* State = GetPlayerState<AMT2PlayerState>();
			const bool bWarrior = State && State->GetCharacterAppearance().Race == EMT2CharacterRace::Warrior;
			GetEquipmentComponent()->EquipWeapon(
				Weapon->WorldMesh,
				bWarrior ? FName(TEXT("equip_right_hand")) : FName(TEXT("equip_right")),
				Weapon->WeaponSubType, Weapon->RefinementLevel);
			SetWeaponAnimationSet(Weapon->WeaponSubType, MountComponent && MountComponent->IsMounted());
		}
		else if (const UMT2ItemRodTemplate* Rod = Cast<UMT2ItemRodTemplate>(Registry->ResolveItemTemplateClass(WeaponSlot.Vnum).GetDefaultObject()))
		{
			const AMT2PlayerState* State = GetPlayerState<AMT2PlayerState>();
			const bool bWarrior = State && State->GetCharacterAppearance().Race == EMT2CharacterRace::Warrior;
			GetEquipmentComponent()->EquipWeapon(Rod->WorldMesh.IsNull() ? GetDefault<UMT2FishingSettings>()->RodMesh : Rod->WorldMesh,
				bWarrior ? FName(TEXT("equip_right_hand")) : FName(TEXT("equip_right")), INDEX_NONE);
			SetAnimationSet(TEXT("fishing"));
		}
		else
		{
			GetEquipmentComponent()->UnequipWeapon();
			SetWeaponAnimationSet(INDEX_NONE, MountComponent && MountComponent->IsMounted());
		}

		const FMT2ItemSlot& BodySlot = Worn.IsValidIndex(WearBody) ? Worn[WearBody] : FMT2ItemSlot();
		const AMT2PlayerState* State = GetPlayerState<AMT2PlayerState>();
		const FMT2CharacterAppearanceAsset* DefaultAppearance = State
			? GetDefault<UMT2CharacterAppearanceSettings>()->FindAppearance(State->GetCharacterAppearance()) : nullptr;
		if (const UMT2ItemArmorTemplate* Armor = Cast<UMT2ItemArmorTemplate>(Registry->ResolveItemTemplateClass(BodySlot.Vnum).GetDefaultObject()))
		{
			const FMT2ArmorMeshVariant* ArmorVariant = State
				? Armor->ResolveArmorVariant(State->GetCharacterAppearance()) : nullptr;
			const TSoftObjectPtr<USkeletalMesh> ArmorMesh = ArmorVariant
				? ArmorVariant->Mesh : Armor->ArmorMesh;
			if (!ArmorMesh.IsNull())
			{
				static const TArray<FMT2ArmorMaterialOverride> NoMaterialOverrides;
				GetEquipmentComponent()->EquipArmor(
					ArmorMesh,
					ArmorVariant ? ArmorVariant->MaterialOverrides : NoMaterialOverrides,
					Armor->RefinementLevel);
			}
			else if (DefaultAppearance)
			{
				GetEquipmentComponent()->UnequipArmor(DefaultAppearance->Mesh);
			}
		}
		else if (DefaultAppearance)
		{
			GetEquipmentComponent()->UnequipArmor(DefaultAppearance->Mesh);
		}
	}

	RecalculateEquipmentStats();
	if (SkillVisualEffectComponent)
	{
		SkillVisualEffectComponent->RefreshEffects();
	}
}

void AMT2PlayerCharacter::HandleInventoryChanged()
{
	if (!HasAuthority()) return;
	if (AMT2PlayerState* State = GetPlayerState<AMT2PlayerState>())
	{
		if (UMT2PersistenceComponent* Persistence = State->GetPersistenceComponent())
		{
			Persistence->MarkDirty();
		}
	}
}

void AMT2PlayerCharacter::HandleStatusEffectsChanged()
{
	RecalculateEquipmentStats();
	if (SkillVisualEffectComponent)
	{
		SkillVisualEffectComponent->RefreshEffects();
	}
	HandleInventoryChanged();
}

void AMT2PlayerCharacter::RefreshDerivedStats()
{
	if (!HasAuthority())
	{
		return;
	}

	AMT2PlayerState* State = GetPlayerState<AMT2PlayerState>();
	UAbilitySystemComponent* AbilitySystem = GetAbilitySystemComponent();
	UMT2CombatStatsComponent* CombatStats = GetCombatStatsComponent();
	if (!State || !AbilitySystem || !CombatStats)
	{
		return;
	}

	const FMT2PlayerDerivedStats Derived = MT2PlayerStatFormula::Calculate(
		State->GetCharacterAppearance().Race, State->GetCharacterLevel(), GetCalculatedPrimaryStats());
	CombatStats->SetBaseStats(Derived.Combat);
	const FMT2CombatStats CalculatedCombat = CombatStats->GetCalculatedStats();

	auto ApplyMaximumPreservingRatio = [&](const FGameplayAttribute& CurrentAttribute,
		const FGameplayAttribute& MaximumAttribute, float NewMaximum)
	{
		const float OldMaximum = AbilitySystem->GetNumericAttribute(MaximumAttribute);
		const float OldCurrent = AbilitySystem->GetNumericAttribute(CurrentAttribute);
		const float Ratio = OldMaximum > UE_SMALL_NUMBER
			? FMath::Clamp(OldCurrent / OldMaximum, 0.0f, 1.0f) : 1.0f;
		AbilitySystem->SetNumericAttributeBase(MaximumAttribute, NewMaximum);
		AbilitySystem->SetNumericAttributeBase(CurrentAttribute, NewMaximum * Ratio);
	};

	ApplyMaximumPreservingRatio(UMT2CoreAttributeSet::GetHealthAttribute(),
		UMT2CoreAttributeSet::GetMaxHealthAttribute(), CalculatedCombat.MaxHealth);
	ApplyMaximumPreservingRatio(UMT2CoreAttributeSet::GetManaAttribute(),
		UMT2CoreAttributeSet::GetMaxManaAttribute(), Derived.MaxMana);
	ApplyMaximumPreservingRatio(UMT2CoreAttributeSet::GetStaminaAttribute(),
		UMT2CoreAttributeSet::GetMaxStaminaAttribute(), Derived.MaxStamina);

	GetCombatComponent()->ConfigureBasicAttack(
		CalculatedCombat.AttackRange, 65.0f, Derived.Combat.DamageMin,
		CombatStats->GetAttackInterval(), GetCombatComponent()->GetBasicAttackComboLength());
}

void AMT2PlayerCharacter::RecalculateEquipmentStats()
{
	if (!HasAuthority())
	{
		return;
	}

	// EApplyTypes ordinals from the old client's common/enums.h.
	enum
	{
		APPLY_MAX_HP = 1, APPLY_CON = 3, APPLY_INT = 4, APPLY_STR = 5, APPLY_DEX = 6,
		APPLY_ATT_SPEED = 7, APPLY_MOV_SPEED = 8, APPLY_ATT_GRADE_BONUS = 53,
		APPLY_DEF_GRADE_BONUS = 54, APPLY_MAGIC_ATT_GRADE = 55,
		APPLY_MAGIC_DEF_GRADE = 56, APPLY_DEF_GRADE = 83
	};

	FMT2CombatStatBonuses CombatBonuses;
	FMT2PrimaryStats PrimaryBonuses;
	ItemApplyBonuses.Reset();

	// One shared APPLY-ordinal mapping for everything that grants point bonuses: item applies AND
	// status effects (the old game routes both through the same PointChange(applyOn, value) call).
	auto ApplyBonus = [&](int32 ApplyType, int32 Value)
	{
		ItemApplyBonuses.FindOrAdd(ApplyType) += Value;
		switch (ApplyType)
		{
		case APPLY_MAX_HP: CombatBonuses.MaxHealth += Value; break;
		case APPLY_CON: PrimaryBonuses.Constitution += Value; break;
		case APPLY_INT: PrimaryBonuses.Intelligence += Value; break;
		case APPLY_STR: PrimaryBonuses.Strength += Value; break;
		case APPLY_DEX: PrimaryBonuses.Dexterity += Value; break;
		case APPLY_ATT_SPEED: CombatBonuses.AttackSpeed += Value; break;
		case APPLY_MOV_SPEED: CombatBonuses.MovementSpeed += Value; break;
		case APPLY_ATT_GRADE_BONUS:
			CombatBonuses.DamageMin += Value;
			CombatBonuses.DamageMax += Value;
			break;
		case APPLY_DEF_GRADE_BONUS:
		case APPLY_DEF_GRADE:
			CombatBonuses.Defense += Value;
			CombatBonuses.MagicDefense += Value * 0.5f;
			break;
		case APPLY_MAGIC_ATT_GRADE: CombatBonuses.MagicAttack += Value; break;
		case APPLY_MAGIC_DEF_GRADE: CombatBonuses.MagicDefense += Value; break;
		case 72: CombatBonuses.DamageMultiplier *= 1.0f + Value / 100.0f; break;
		default: break;
		}
	};

	auto ApplyItem = [&](const UMT2ItemTemplate* Template, const FMT2ItemSlot& Item)
	{
		if (!Template)
		{
			return;
		}

		// Use the same physical weapon-power mapping as the old client and server. Value5 is the
		// refinement power and is added to both ends of the weapon range.
		if (const UMT2ItemWeaponTemplate* Weapon = Cast<UMT2ItemWeaponTemplate>(Template))
		{
			CombatBonuses.DamageMin += Weapon->GetPhysicalDamageMin() * 2;
			CombatBonuses.DamageMax += Weapon->GetPhysicalDamageMax() * 2;
		}
		else if (const UMT2ItemArmorTemplate* ArmorTemplate =
			Cast<UMT2ItemArmorTemplate>(Template))
		{
			const int32 Armor =
				ArmorTemplate->Defense + ArmorTemplate->RefinementDefense * 2;
			CombatBonuses.Defense += Armor;
			CombatBonuses.MagicDefense += Armor * 0.5f;
		}

		for (const FMT2ItemApply& Apply : Template->Applies)
		{
			ApplyBonus(static_cast<int32>(Apply.Type), Apply.Value);
		}
		for (const FMT2MetinSocket& Socket : Item.MetinSockets)
		{
			const UMT2ItemMetinStoneTemplate* Stone = Socket.Stone.GetDefaultObject();
			if (!Stone)
			{
				continue;
			}
			for (const FMT2ItemApply& Apply : Stone->Applies)
			{
				ApplyBonus(static_cast<int32>(Apply.Type), Apply.Value);
			}
		}
		for (const FMT2ItemBonus& Bonus : Item.Bonuses)
		{
			if (Bonus.IsValid())
			{
				ApplyBonus(Bonus.GetTypeId(), Bonus.Value);
			}
		}
	};

	// Sum bonuses from every currently-worn item (resolved from the equipment slots by vnum).
	UGameInstance* GameInstance = GetGameInstance();
	UMT2VnumRegistrySubsystem* Registry = GameInstance ? GameInstance->GetSubsystem<UMT2VnumRegistrySubsystem>() : nullptr;
	if (Registry)
	{
		for (const FMT2ItemSlot& Worn : GetInventoryComponent()->GetEquipment())
		{
			if (!Worn.IsEmpty())
			{
				ApplyItem(Registry->ResolveItemTemplateClass(Worn.Vnum).GetDefaultObject(), Worn);
			}
		}
	}

	// ... plus every active buff/debuff (old ComputeAffect: PointChange(bApplyOn, lApplyValue)).
	if (const UMT2StatusEffectComponent* StatusEffects = GetStatusEffectComponent())
	{
		for (const FMT2StatusEffect& Effect : StatusEffects->GetEffects())
		{
			ApplyBonus(Effect.ApplyType, Effect.ApplyValue);
		}
	}

	if (UMT2CombatStatsComponent* CombatStats = GetCombatStatsComponent())
	{
		CombatStats->SetBonuses(CombatBonuses);
	}
	if (PrimaryStatsComponent)
	{
		PrimaryStatsComponent->SetBonusStats(PrimaryBonuses);
	}
	// POINT_MOV_SPEED defaults to 100 in the old game; the movement-speed % apply adds on top.
	if (UMT2MovementSpeedComponent* Movement = GetMovementSpeedComponent())
	{
		Movement->SetMovementSpeed(100.0f + CombatBonuses.MovementSpeed);
	}
	RefreshDerivedStats();

	UE_LOG(LogMT2Targeting, Warning,
		TEXT("[MT2Equip] Recomputed bonuses: Atk+%.0f..%.0f Def+%.0f AtkSpd+%d MovSpd+%d MaxHP+%.0f | STR+%d DEX+%d CON+%d INT+%d"),
		CombatBonuses.DamageMin, CombatBonuses.DamageMax, CombatBonuses.Defense,
		CombatBonuses.AttackSpeed, CombatBonuses.MovementSpeed, CombatBonuses.MaxHealth,
		PrimaryBonuses.Strength, PrimaryBonuses.Dexterity, PrimaryBonuses.Constitution, PrimaryBonuses.Intelligence);
}

int32 AMT2PlayerCharacter::GetItemApplyBonus(int32 ApplyType) const
{
	return ItemApplyBonuses.FindRef(ApplyType);
}
