/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "AbilitySystemInterface.h"
#include "CoreMinimal.h"
#include "Characters/MT2CharacterBase.h"
#include "Combat/MT2DamageTypes.h"
#include "Items/MT2ItemTypes.h"
#include "Player/MT2PlayerTypes.h"
#include "Skills/MT2SkillTypes.h"
#include "Stats/MT2StatTypes.h"
#include "TimerManager.h"
#include "MT2PlayerCharacter.generated.h"

class UCameraComponent;
class UInputComponent;
class USpringArmComponent;
class UAbilitySystemComponent;
class AMT2Npc;
class AMT2PlayerState;
class AMT2WorldItem;
class UMT2AdminCommandComponent;
class UMT2GMMarkComponent;
class UMT2RegenerationComponent;
class UMT2SkillCastComponent;
class UMT2SkillVisualEffectComponent;
class UMT2TargetIndicatorComponent;
class UMT2InventoryComponent;
class UMT2Item;
class UMT2PrimaryStatsComponent;
class UMT2MountComponent;
class UMT2RespawnWidget;
class UMT2RefinementDialogWidget;
class UMT2SkillDefinition;
class UMT2ShopWidget;
struct FInputActionValue;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FMT2InventoryActionConfirmedSignature, int32, Vnum, bool, bUseSound);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FMT2InventoryItemActorInteractionSignature, AActor*, TargetActor, int32, InventorySlot, int32, ItemVnum);

UCLASS(Blueprintable)
class METIN2_API AMT2PlayerCharacter : public AMT2CharacterBase, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AMT2PlayerCharacter();
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool IsPlayerFaction() const override { return true; }
	virtual bool IsPvPEnabledAgainst(const AMT2CharacterBase* Other) const override;
	UFUNCTION(BlueprintPure, Category = "PvP")
	bool IsInSafeZone() const;

	// Whether an active duel exists between this player and Other. Stub until the duel subsystem
	// exists; IsPvPEnabledAgainst already consumes it so duels will just work once implemented.
	bool IsDuelingWith(const AMT2CharacterBase* Other) const { return false; }
#if WITH_EDITOR
	// Lets FirstPersonCameraOffset be dialled in live: edits in the details panel move the camera
	// straight away instead of waiting for the next toggle.
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UFUNCTION(BlueprintCallable, Category = "Input")
	void Move(const FVector2D& MovementInput);

	UFUNCTION(BlueprintCallable, Category = "Input")
	void Look(const FVector2D& LookInput);

	UFUNCTION(BlueprintCallable, Category = "Input")
	bool Attack();

	UFUNCTION(BlueprintCallable, Category = "Movement")
	void RequestClickMove();

	UFUNCTION(BlueprintCallable, Category = "Targeting")
	void RequestInspectMob();

	UFUNCTION(BlueprintPure, Category = "Camera")
	USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

	UFUNCTION(BlueprintPure, Category = "Camera")
	UCameraComponent* GetFollowCamera() const { return FollowCamera; }

	UFUNCTION(BlueprintPure, Category = "Camera")
	UCameraComponent* GetFirstPersonCamera() const { return FirstPersonCamera; }

	// Swaps between the follow camera and the eye-level first-person one. Purely local - it changes
	// nothing the server or other clients can see.
	UFUNCTION(BlueprintCallable, Category = "Camera")
	void SetFirstPersonEnabled(bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "Camera")
	void ToggleFirstPerson() { SetFirstPersonEnabled(!bFirstPersonActive); }

	// Moves the first-person camera relative to the head bone, so it can be lined up with the eyes.
	// Applies immediately, so it can be nudged while first person is running.
	UFUNCTION(BlueprintCallable, Category = "Camera")
	void SetFirstPersonCameraOffset(const FVector& Offset);

	UFUNCTION(BlueprintPure, Category = "Camera")
	FVector GetFirstPersonCameraOffset() const { return FirstPersonCameraOffset; }

	UFUNCTION(BlueprintPure, Category = "Camera")
	bool IsFirstPersonActive() const { return bFirstPersonActive; }

	// Called from UMT2RespawnWidget's buttons. bRespawnInTown false = respawn where the character
	// died, true = respawn at a town/city PlayerStart.
	UFUNCTION(BlueprintCallable, Category = "Health")
	void RequestRespawn(bool bRespawnInTown);

	UFUNCTION(BlueprintPure, Category = "Inventory")
	UMT2InventoryComponent* GetInventoryComponent() const { return InventoryComponent; }

	UFUNCTION(BlueprintPure, Category = "Stats|Primary")
	UMT2PrimaryStatsComponent* GetPrimaryStatsComponent() const { return PrimaryStatsComponent; }

	UFUNCTION(BlueprintPure, Category = "Mount")
	UMT2MountComponent* GetMountComponent() const { return MountComponent; }

	UFUNCTION(BlueprintPure, Category = "Stats|Primary")
	FMT2PrimaryStats GetCalculatedPrimaryStats() const;

	// Sum of every currently equipped item apply, including the seven random bonus slots.
	UFUNCTION(BlueprintPure, Category = "Stats|Item Bonuses")
	int32 GetItemApplyBonus(int32 ApplyType) const;

	// Parses and executes chat slash commands (e.g. "/m vnum count" to spawn a mob, "/i vnum count"
	// to receive an item), called from UMT2ChatWidget via the player controller. Server-authoritative
	// since it spawns actors and grants items.
	UFUNCTION(Server, Reliable, Category = "Chat")
	void ServerExecuteChatCommand(const FString& Command);

	// Inventory actions requested by the UI. Server-authoritative; the component mutates and replicates
	// the item data, and the character reacts to equipment changes to apply meshes + stat bonuses.
	UFUNCTION(Server, Reliable, Category = "Inventory")
	void ServerMoveInventoryItem(int32 FromSlot, int32 ToSlot);

	UFUNCTION(Server, Reliable, Category = "Inventory")
	void ServerApplyInventoryBonusItem(int32 SourceSlot, int32 TargetSlot, bool bTargetEquipped);

	UFUNCTION(Server, Reliable, Category = "Inventory")
	void ServerApplyMetinStone(int32 SourceSlot, int32 TargetSlot, bool bTargetEquipped);

	UFUNCTION(Client, Reliable, Category = "Inventory")
	void ClientNotifyMetinSocketResult(EMT2MetinSocketApplyResult Result);

	UFUNCTION(Server, Reliable, Category = "Inventory")
	void ServerOpenInventoryLootCrate(int32 CrateSlot, int32 KeySlot);

	UFUNCTION(Client, Reliable, Category = "Inventory")
	void ClientNotifyLootCrateResult(EMT2LootCrateOpenResult Result);

	UFUNCTION(Client, Reliable, Category = "Inventory")
	void ClientNotifyInventoryBonusResult(bool bRandomizer, bool bSucceeded);

	UFUNCTION(Server, Reliable, Category = "Inventory")
	void ServerEquipInventoryItem(int32 InventorySlot);

	UFUNCTION(Server, Reliable, Category = "Inventory")
	void ServerUnequipItem(int32 WearPosition);

	UFUNCTION(Server, Reliable, Category = "Inventory")
	void ServerUseInventoryItem(int32 InventorySlot);

	bool BeginGrandMasterTraining(int32 InventorySlot);

	UFUNCTION(Server, Reliable, Category = "Inventory")
	void ServerDropInventoryItem(int32 InventorySlot, int32 Count);

	UFUNCTION(Client, Reliable, Category = "Inventory")
	void ClientNotifyItemDropFailed(const FString& Message);

	// Uses a learned skill: toggles buff skills on/off, applies timed buffs, plays the grade's
	// cast animation, pays SP and starts the cooldown. Attack-skill damage lands later with the
	// full ComputeSkill pipeline.
	UFUNCTION(Server, Reliable, Category = "Skills")
	void ServerUseSkill(int32 SkillVnum);

	// Convenience for the quickslot bar: uses the first inventory stack of the given item vnum.
	UFUNCTION(Server, Reliable, Category = "Inventory")
	void ServerUseInventoryItemByVnum(int32 Vnum);

	// Reads the skill book at InventorySlot onto its own skill (book Value0). Flat EXP cost, 35% success,
	// carries M1->G1. Consumes the book only when the target skill is eligible; posts a chat result.
	UFUNCTION(Server, Reliable, Category = "Skills")
	void ServerReadSkillBook(int32 InventorySlot);

	// Forces the skill UI (character window, quickslot) to rebuild after a book level-up, so the icon
	// and grade update immediately without waiting on the next replication tick.
	UFUNCTION(Client, Reliable, Category = "Skills")
	void ClientRefreshSkillsUI();

	// bFaceTarget/TargetYaw: the old client turns the caster to face the victim before a targeted
	// skill (NEW_LookAtDestInstance). The yaw is computed once on the server and applied on every
	// client here, so the facing (and the motion's forward advance) is consistent for everyone.
	UFUNCTION(NetMulticast, Unreliable, Category = "Skills")
	void MulticastPlaySkillAnimation(int32 SkillVnum, int32 MasteryGrade, bool bFaceTarget, float TargetYaw);

	UFUNCTION(Client, Reliable, Category = "Skills")
	void ClientNotifySkillCooldown(int32 SkillVnum, float CooldownSeconds);

	// Old OnCannotUseSkill: tell the caster why the cast was refused instead of failing silently.
	UFUNCTION(Client, Reliable, Category = "Skills")
	void ClientNotifySkillDenied(int32 SkillVnum, EMT2SkillDenyReason Reason);

	// NPC interaction: the client walks into range then asks the server to interact; shop NPCs
	// answer with ClientOpenNpcShop, talk NPCs go to the quest system.
	UFUNCTION(Server, Reliable, Category = "NPC")
	void ServerInteractNpc(AMT2Npc* Npc);

	// Server-authoritative entry point used when a carried inventory item is clicked onto a player
	// or NPC. Trade, quest, and NPC systems bind this rather than trusting a client-supplied vnum.
	UFUNCTION(Server, Reliable, Category = "Inventory")
	void ServerUseInventoryItemOnActor(AActor* TargetActor, int32 InventorySlot);

	UFUNCTION(Server, Reliable, Category = "Inventory|Refinement")
	void ServerRefineInventoryItem(
		int32 TargetSlot, int32 ScrollSlot, AMT2Npc* Blacksmith);

	UFUNCTION(Client, Reliable, Category = "Inventory|Refinement")
	void ClientOpenRefinementDialog(
		int32 TargetSlot, int32 ScrollSlot, AMT2Npc* Blacksmith);

	UFUNCTION(Client, Reliable, Category = "Inventory|Refinement")
	void ClientNotifyRefinementResult(EMT2RefinementResult Result);

	void OpenRefinementDialogLocal(
		int32 TargetSlot, int32 ScrollSlot, AMT2Npc* Blacksmith);

	UFUNCTION(Client, Reliable, Category = "NPC")
	void ClientOpenNpcShop(AMT2Npc* Npc);

	// Blocks keyboard/click movement while an interaction window (dialog/shop) is open, like the
	// old client freezing the character during a conversation. Ref-counted for nested windows.
	void SetInteractionUIOpen(bool bOpen);
	bool IsInteractionUIOpen() const { return InteractionUIOpenCount > 0; }

	// The owning client's shop window (null until first opened). The inventory queries it to route
	// right-clicks to selling while a shop is open in Sell mode.
	UMT2ShopWidget* GetShopWidget() const { return ShopWidget; }

	UFUNCTION(Server, Reliable, Category = "NPC")
	void ServerShopBuy(AMT2Npc* Npc, int32 EntryIndex, int32 Count);

	UFUNCTION(Server, Reliable, Category = "NPC")
	void ServerShopSell(AMT2Npc* Npc, int32 InventorySlot, int32 Count);

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void RequestPickupNearbyItems();
	void RequestPickupWorldItem(AMT2WorldItem* WorldItem);

	UFUNCTION(Client, Unreliable, Category = "Combat|Feedback")
	void ClientShowDamageNumber(AActor* TargetActor, float Damage, EMT2DamageDisplayType DamageType);

	void SetLastPlayerDamageInstigator(AMT2PlayerCharacter* Attacker)
	{
		LastPlayerDamageInstigator = Attacker;
	}

	UPROPERTY(BlueprintAssignable, Category = "Inventory")
	FMT2InventoryActionConfirmedSignature OnInventoryActionConfirmed;

	UPROPERTY(BlueprintAssignable, Category = "Inventory")
	FMT2InventoryItemActorInteractionSignature OnInventoryItemActorInteractionRequested;

protected:
	virtual void BeginPlay() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	friend class FMT2QuestAlignmentApiTest;
	UFUNCTION(Client, Reliable)
	void ClientConfirmInventoryAction(int32 Vnum, bool bUseSound);

	UFUNCTION(Server, Reliable)
	void ServerPickupNearbyItems();

	UFUNCTION(Server, Reliable)
	void ServerPickupWorldItem(AMT2WorldItem* WorldItem);

	void TryPickupWorldItemOnServer(AMT2WorldItem* WorldItem);

	void HandleMoveInput(const FInputActionValue& Value);
	void HandleLookInput(const FInputActionValue& Value);
	void HandleAttackPressed();
	void HandleAttackReleased();
	void HandleWalkPressed();
	void HandleWalkReleased();
	void HandleCameraRotatePressed();
	void HandleCameraRotateReleased();
	// In first person, glues the pawn's yaw to the camera while the right button is held, so the
	// player can turn on the spot. No-op in third person.
	void UpdatePawnYawFollowsCamera();
	void HandleKeyboardCameraYaw(const FInputActionValue& Value);
	void HandleKeyboardCameraZoom(const FInputActionValue& Value);
	void HandleKeyboardCameraPitch(const FInputActionValue& Value);
	void HandleClickMovePressed();
	void StartAutoMoveToLocation(const FVector& TargetLocation);
	void StartAutoMoveToActor(AActor* TargetActor);
	void StopAutoMove();
	void UpdateAutoMove();
	void UpdateCombatEngagement(float DeltaSeconds);
	void RotateTowardsTarget(const AActor* Target, float DeltaSeconds);
	bool IsAttackMovementLocked() const;
	bool IsDead() const;
	FVector FindTownRespawnLocation() const;
	void InitializePlayerState();
	FString BuildUnarmedAttackAnimationPath(int32 ComboIndex) const;
	// Rebuilds all equipment-derived stat bonuses (weapon attack, armor defense, and every item
	// Apply) from the currently equipped items and pushes them into the stat/movement components.
	// Faithful to the old game's model where equip/unequip recomputes the character's bonus points.
	void RecalculateEquipmentStats();

	UPROPERTY(Transient)
	TMap<int32, int32> ItemApplyBonuses;
	void RefreshDerivedStats();
	UFUNCTION()
	void HandleBasicAttackPerformed(int32 ComboIndex);

	UFUNCTION()
	void HandleDeath();
	void ResolveGrandMasterTrainingSelection(
		int32 InventorySlot, const TArray<int32>& SkillVnums, int32 ChoiceIndex);

	UFUNCTION()
	void HandleRevived();

	UFUNCTION()
	void HandleHealthChanged(float OldValue, float NewValue);

	// Fires when equipment slots change (equip/unequip); reapplies weapon/armor meshes and recomputes
	// all equipment-derived stat bonuses from what's currently worn.
	UFUNCTION()
	void HandleEquipmentChanged();

	UFUNCTION()
	void HandleInventoryChanged();

	// Fires when buffs/debuffs are added, expire, or are removed; recomputes the same bonus set since
	// affects ride the identical apply pipeline as item bonuses.
	UFUNCTION()
	void HandleStatusEffectsChanged();

	UFUNCTION(Server, Reliable)
	void ServerRequestRespawn(bool bRespawnInTown);

	UFUNCTION(Server, Reliable)
	void ServerSetWalkRequested(bool bNewWalkRequested);

	UFUNCTION(Server, Unreliable)
	void ServerSetAttackFacingYaw(float NewYaw);

	UFUNCTION()
	void HandleCharacterAppearanceChanged(const FMT2CharacterAppearance& Appearance);

	UFUNCTION()
	void HandleCharacterAppearanceApplied(const FMT2CharacterAppearance& Appearance);

	UFUNCTION()
	void HandlePlayerLevelChanged(int32 OldLevel, int32 NewLevel);

	UFUNCTION()
	void HandlePrimaryStatsChanged(FMT2PrimaryStats OldStats, FMT2PrimaryStats NewStats);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> FollowCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	// Head bone of the old player skeletons (verified on every SK_*_Skeleton under season1/pc).
	UPROPERTY(EditAnywhere, Category = "Camera|First Person")
	FName FirstPersonSocketName = TEXT("Bip01_Head");

	// Offset from the head bone to the eyes, in the *bone's* local space - it rotates with the head,
	// so the camera stays on the eyes as the animation moves. This is the single source of truth for
	// the camera's placement: it is re-applied on every attach, so dragging the FirstPersonCamera
	// component in the viewport would just be overwritten - set this instead. Editing it while
	// playing applies immediately (see PostEditChangeProperty), so it can be dialled in live.
	// These Bip01 rigs don't use intuitive axes, so expect to find the eyes by trial rather than by
	// assuming +X is forward.
	UPROPERTY(EditAnywhere, Category = "Camera|First Person", meta = (Units = "cm"))
	FVector FirstPersonCameraOffset = FVector::ZeroVector;

	// Used only if the equipped mesh has no FirstPersonSocketName bone: height above the capsule
	// centre (the capsule is 90 half-height, so 75 sits just under the top of the head).
	UPROPERTY(EditDefaultsOnly, Category = "Camera|First Person", meta = (Units = "cm"))
	float FirstPersonEyeHeight = 75.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Camera|First Person", meta = (ClampMin = "60.0", ClampMax = "140.0", Units = "deg"))
	float FirstPersonFieldOfView = 100.0f;

	bool bFirstPersonActive = false;

	UPROPERTY(EditDefaultsOnly, Category = "Camera|Keyboard", meta = (ClampMin = "0.0", Units = "deg/s"))
	float KeyboardCameraYawSpeed = 90.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Camera|Keyboard", meta = (ClampMin = "0.0", Units = "deg/s"))
	float KeyboardCameraPitchSpeed = 60.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Camera|Keyboard", meta = (ClampMin = "0.0", Units = "cm/s"))
	float KeyboardCameraZoomSpeed = 700.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Camera|Zoom", meta = (ClampMin = "0.0", Units = "cm"))
	float MinimumCameraDistance = 200.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Camera|Zoom", meta = (ClampMin = "0.0", Units = "cm"))
	float MaximumCameraDistance = 2000.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2InventoryComponent> InventoryComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats|Primary", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2PrimaryStatsComponent> PrimaryStatsComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mount", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2MountComponent> MountComponent;

	// Old-game GM mark (the red YMIR logo) floating over admin heads; hides itself for non-admins.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Admin", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2GMMarkComponent> GMMarkComponent;

	// Draws the rotating ring on the current target (old EFFECT_SELECT).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2TargetIndicatorComponent> TargetIndicatorComponent;

	// Server-side HP/SP recovery (old recovery_event / DistributeSP), driven from spawn/death/revive.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2RegenerationComponent> RegenerationComponent;

	// Server-side skill execution (gates, cost, buffs, damage); ServerUseSkill delegates to it.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skills", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2SkillCastComponent> SkillCastComponent;

	// Local visual mirror of replicated toggle-skill affects (body/weapon loops on every client).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skills", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2SkillVisualEffectComponent> SkillVisualEffectComponent;

	// GM/admin chat command table; ServerExecuteChatCommand delegates to it after the admin check.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Admin", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2AdminCommandComponent> AdminCommandComponent;

	// Puts the first-person camera on the head bone of whatever mesh is currently equipped.
	void AttachFirstPersonCamera();

	// Cursor pick for click-to-move and targeting. Ignores this character, which would otherwise
	// block its own clicks (and every click in first person).
	bool TraceCursorTarget(FHitResult& OutHit) const;

public:
	// Old CPoly variable set for a skill's formulas, shared by the cast and the tooltip so the
	// numbers shown are the numbers dealt. ValueType forces the weapon roll low/high for the
	// tooltip's "min - max" range (client CPoly RANDOM_TYPE_FORCE_MIN/MAX).
	TMap<FString, double> BuildSkillFormulaVariables(
		const UMT2SkillDefinition* Definition, int32 Level, EMT2SkillFormulaValue ValueType) const;

private:

	TWeakObjectPtr<AMT2PlayerState> BoundPlayerState;
	TWeakObjectPtr<AActor> AutoMoveTargetActor;
	// NPC we're walking toward to interact with; consumed by Tick when in range.
	TWeakObjectPtr<AMT2Npc> PendingInteractNpc;
	// Player/NPC receiving the inventory item currently carried on the cursor.
	TWeakObjectPtr<AActor> PendingItemInteractionTarget;
	int32 PendingItemInteractionSlot = INDEX_NONE;
	FVector AutoMoveTargetLocation = FVector::ZeroVector;
	bool bHasAutoMoveTarget = false;
	float AutoMoveAcceptanceRadius = 5.0f;
	bool bCameraRotateHeld = false;
	float RightMouseDragDistance = 0.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Camera", meta = (ClampMin = "0.0"))
	float RightClickInspectDragThreshold = 3.0f;
	// Set when clicking a combat-capable actor (has an AbilitySystemComponent). While true, the
	// character chases the selected target when out of basic-attack range and auto-attacks it once
	// in range, instead of the click only ever walking to a single point and stopping.
	bool bAutoEngageTarget = false;
	bool bAttackFacingLocked = false;
	float AttackFacingYaw = 0.0f;
	FVector AttackAdvanceDirection = FVector::ZeroVector;
	float AttackAdvanceSpeed = 0.0f;
	double AttackMotionEndTime = -DBL_MAX;

	UPROPERTY(EditDefaultsOnly, Category = "Combat", meta = (ClampMin = "0.0", Units = "deg/s"))
	float AttackSteeringRate = 240.0f;

	FVector DeathLocation = FVector::ZeroVector;
	FTimerHandle DeathAnimFreezeTimer;
	TWeakObjectPtr<AMT2PlayerCharacter> LastPlayerDamageInstigator;
	TWeakObjectPtr<UMT2RespawnWidget> RespawnWidget;

	UPROPERTY(Transient)
	TObjectPtr<UMT2ShopWidget> ShopWidget;

	UPROPERTY(Transient)
	TObjectPtr<UMT2RefinementDialogWidget> RefinementDialogWidget;

	int32 InteractionUIOpenCount = 0;
	void UpdateSafeZoneNotification();
	bool bSafeZoneNotified = false;
	bool bLastSafeZone = false;
	TWeakObjectPtr<AController> SafeZoneNotifiedController;

};
