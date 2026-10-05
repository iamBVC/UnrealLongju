/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "AbilitySystemInterface.h"
#include "Characters/MT2CharacterBase.h"
#include "CoreMinimal.h"
#include "Mobs/MT2LootTypes.h"
#include "Mobs/MT2MobTypes.h"
#include "MT2Mob.generated.h"

class AMT2MetinStone;
class AMT2PlayerCharacter;
class UMT2QuestManagerComponent;
class UAbilitySystemComponent;
class UAnimMontage;
class UAnimSequence;
class UMT2CoreAttributeSet;
class UMT2ActorRelevanceComponent;
class UMT2MobAIComponent;
class UMT2MobLootComponent;
class UMT2MobLifecycleComponent;
class UMT2PrimaryStatsComponent;
class UMT2SkillComponent;

// All imported mob_proto data lives directly on this actor class and its components (combat stats on
// UMT2CombatStatsComponent, primary stats on UMT2PrimaryStatsComponent, AI tuning on
// UMT2MobAIComponent, regen/resurrection on UMT2MobLifecycleComponent, rewards on
// UMT2MobLootComponent) - the generated BP_Mob_* Blueprint IS the mob's data, the UE-native way.
UCLASS(Blueprintable)
class METIN2_API AMT2Mob : public AMT2CharacterBase, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AMT2Mob(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	bool TryAcquireQuestConversation(UMT2QuestManagerComponent* Manager, AMT2PlayerCharacter* Player);
	void ReleaseQuestConversation(UMT2QuestManagerComponent* Manager);

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void OnConstruction(const FTransform& Transform) override;

	// Old CRUSH rule: victims flagged AIFLAG_NOMOVE (stones, doors) are never pushed.
	virtual bool CanBeKnockedBack() const override;

	UFUNCTION(BlueprintPure, Category = "Mob")
	bool IsMetinStone() const { return MobType == EMT2MobType::Stone; }

	// Writes every imported mob_proto field onto this actor and its components. Called by the mob
	// importer on the Blueprint CDO (and safe at runtime on authority for dynamically-tuned mobs).
	virtual void ConfigureFromDefinition(const FMT2MobDefinition& Definition);

	void SetMotionSet(const FMT2MobMotionSet& NewMotionSet) { MotionSet = NewMotionSet; }

	UFUNCTION(BlueprintPure, Category = "Mob")
	int32 GetMobVnum() const { return MobVnum; }

	UFUNCTION(BlueprintPure, Category = "Mob")
	int32 GetMobLevel() const { return MobLevel; }

	UFUNCTION(BlueprintPure, Category = "Mob")
	int32 GetEmpire() const { return Empire; }

	UFUNCTION(BlueprintPure, Category = "Mob")
	EMT2MobType GetMobType() const { return MobType; }

	UFUNCTION(BlueprintPure, Category = "Mob")
	EMT2MobRank GetMobRank() const { return Rank; }

	// Localized display name with internal-name fallback, for nameplates/target info.
	UFUNCTION(BlueprintPure, Category = "Mob")
	FString GetMobDisplayName() const
	{
		return !DisplayName.IsEmpty() ? DisplayName : (!InternalName.IsEmpty() ? InternalName : GetName());
	}

	UFUNCTION(BlueprintPure, Category = "Mob|Skills")
	const TArray<FMT2MobSkillDefinition>& GetMobSkills() const { return MobSkills; }

	UFUNCTION(BlueprintPure, Category = "Mob|AI")
	UMT2MobAIComponent* GetMobAIComponent() const { return MobAIComponent; }

	UFUNCTION(BlueprintPure, Category = "Mob|Rewards")
	UMT2MobLootComponent* GetLootComponent() const { return LootComponent; }

	UFUNCTION(BlueprintPure, Category = "Mob|Stats")
	UMT2PrimaryStatsComponent* GetPrimaryStatsComponent() const { return PrimaryStatsComponent; }

	UFUNCTION(BlueprintPure, Category = "Mob|Lifecycle")
	UMT2MobLifecycleComponent* GetLifecycleComponent() const { return LifecycleComponent; }

	UFUNCTION(BlueprintPure, Category = "Mob|Rewards")
	TSubclassOf<class UMT2LootTable> GetLootTable() const;

	void SetLootTable(TSubclassOf<class UMT2LootTable> NewLootTable);

	UFUNCTION(BlueprintPure, Category = "Mob|Skills")
	UMT2SkillComponent* GetMobSkillComponent() const { return SkillComponent; }

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Mob|Combat")
	bool TryAttackTarget(AActor* TargetActor);

	UFUNCTION(BlueprintCallable, Category = "Mob|Animation")
	bool PlayMobMotion(EMT2MobMotion Motion);

	UFUNCTION(BlueprintCallable, Category = "Mob|Animation")
	bool PlayMobSkillAnimation(int32 SkillVnum);

	// Attack-motion play rate = AttackSpeed / 100, the old client's model (mob_proto ATTACK_SPEED is
	// a percent with 100 as baseline). See Docs/OldGameResearch/AttackSpeedAndMovement.md.
	UFUNCTION(BlueprintPure, Category = "Mob|Combat")
	float GetAttackMotionPlayRate() const;

	UFUNCTION(BlueprintPure, Category = "Mob|Combat")
	bool IsCombatMotionLocked() const { return bCombatMotionLocked; }

	void SetLastDamageInstigator(AActor* DamageInstigator);
	// Who last hit this mob; metin stones hand this to the mobs they summon (old SelectStone).
	AActor* GetLastDamageInstigator() const { return LastDamageInstigator.Get(); }

	// Old m_map_kDamage, filled by CHARACTER::Damage on NPC victims only. Drives who gets the exp
	// and who may own the drops. Amount is the final post-defense damage, like the old game's.
	virtual void RecordDamage(AActor* Attacker, float Amount);
	const TArray<FMT2DamageShare>& GetDamageShares() const { return DamageShares; }
	void SetSpawnGroupMembers(const TArray<AMT2Mob*>& Members);

	// Old SetStone/m_pkChrStone: mobs summoned by a metin stone remember it, so breaking the stone
	// wipes its wave and half their exp banks on the stone.
	void SetSpawningStone(AMT2MetinStone* Stone);
	AMT2MetinStone* GetSpawningStone() const;

	// Old Dead(NULL): dies on the spot with no killer, so it pays out no exp, gold or drops.
	void KillWithoutReward();

	// A spot near Origin where MobClass's capsule actually fits on the ground: rings outward from
	// StartRadius until a candidate traces down to ground with nothing overlapping its capsule, so
	// spawns never end up floating or buried in geometry. Falls back to Origin if nothing is free.
	static FVector FindGroundSpawnLocation(const UWorld* World, TSubclassOf<AMT2Mob> MobClass,
		const FVector& Origin, float StartRadius, const AActor* IgnoredActor);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	// ---- Imported mob_proto data (identity/flags/combat extras live here; stats, AI, regen and
	// rewards live on the matching components). Everything is editable on the mob Blueprint. ----

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Replicated, Category = "Mob|Identity")
	int32 MobVnum = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mob|Identity")
	FString InternalName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mob|Identity")
	FString DisplayName;

	// mob_proto folder/resource names, kept for re-import matching and debugging.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mob|Identity", AdvancedDisplay)
	FString ResourceName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mob|Identity", AdvancedDisplay)
	FString SourceFolder;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mob|Identity")
	EMT2MobRank Rank = EMT2MobRank::Pawn;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mob|Identity")
	EMT2MobType MobType = EMT2MobType::Monster;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mob|Identity")
	EMT2MobSize MobSize = EMT2MobSize::Medium;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Replicated, Category = "Mob|Identity", meta = (ClampMin = "1"))
	int32 MobLevel = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Replicated, Category = "Mob|Identity")
	int32 Empire = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mob|Visuals", meta = (ClampMin = "1"))
	int32 ScalePercent = 100;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mob|Flags")
	int32 RaceFlags = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mob|Flags")
	int32 ImmuneFlags = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mob|Combat")
	EMT2MobBattleType BattleType = EMT2MobBattleType::Melee;

	// mob_proto HIT_RANGE; not wired into combat traces yet.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mob|Combat", meta = (Units = "cm"))
	float HitRange = 0.0f;

	// Curse/slow/poison/stun/critical/penetrate enchant chances; not wired yet.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mob|Combat")
	TArray<int32> Enchants;

	// Sword/twohand/dagger/bell/fan/bow/fire/elect/magic/wind/poison resists; not wired yet.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mob|Combat")
	TArray<int32> Resistances;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mob|Combat")
	TArray<int32> ElementalAttackValues;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mob|Skills")
	TArray<FMT2MobSkillDefinition> MobSkills;

	// HP%-triggered special behaviors from mob_proto; not wired yet.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mob|Special", meta = (ClampMin = "0", ClampMax = "100"))
	int32 BerserkHealthPercent = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mob|Special", meta = (ClampMin = "0", ClampMax = "100"))
	int32 StoneSkinHealthPercent = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mob|Special", meta = (ClampMin = "0", ClampMax = "100"))
	int32 GodSpeedHealthPercent = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mob|Special", meta = (ClampMin = "0", ClampMax = "100"))
	int32 DeathBlowHealthPercent = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mob|Special", meta = (ClampMin = "0", ClampMax = "100"))
	int32 ReviveHealthPercent = 0;

	// Attack/damage/death/knockdown animation variants from the imported .msa motion lists.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mob|Animation")
	FMT2MobMotionSet MotionSet;

	UPROPERTY(EditDefaultsOnly, Category = "Mob|Death", meta = (ClampMin = "0.0", Units = "s"))
	float CorpseLifetime = 5.0f;

private:
	// Server-only, non-persistent leases. Weak references cannot keep a disconnected pawn alive.
	TWeakObjectPtr<UMT2QuestManagerComponent> QuestConversationOwner;
	TWeakObjectPtr<AMT2PlayerCharacter> QuestConversationPawn;
	void CalculateVisualCollision(float& OutRadius, float& OutHalfHeight,
		float& OutMeshRelativeZ) const;
	bool IsStationaryNpc() const;
	void ApplyVisualConfiguration();
	void ApplyStatsToRuntimeState();
	const FMT2MobMotionVariant* ChooseMotion(EMT2MobMotion Motion) const;
	void LockCombatMovement(float Duration);
	void UnlockCombatMovement();

	UFUNCTION()
	void HandleDeath();

	UFUNCTION()
	void HandleRevived();

	UFUNCTION()
	void OnRep_MotionSerial();

	UFUNCTION()
	void HandleAIStateChanged(EMT2MobAIState OldState, EMT2MobAIState NewState);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mob|Abilities", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mob|Optimization", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2ActorRelevanceComponent> RelevanceComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mob|Abilities", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2CoreAttributeSet> CoreAttributes;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mob|AI", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2MobAIComponent> MobAIComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mob|Rewards", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2MobLootComponent> LootComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mob|Stats", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2PrimaryStatsComponent> PrimaryStatsComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mob|Lifecycle", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2MobLifecycleComponent> LifecycleComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mob|Skills", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2SkillComponent> SkillComponent;

	UPROPERTY(Replicated)
	EMT2MobMotion ReplicatedMotion = EMT2MobMotion::Wait;

	UPROPERTY(ReplicatedUsing = OnRep_MotionSerial)
	uint8 MotionSerial = 0;

	FTimerHandle AttackCooldownTimer;
	FTimerHandle CombatMotionLockTimer;
	FTimerHandle DeathAnimFreezeTimer;
	TWeakObjectPtr<UAnimMontage> DeathAnimMontage;
	bool bAttackReady = true;
	bool bCombatMotionLocked = false;
	bool bRewardsDistributed = false;
	TWeakObjectPtr<AActor> LastDamageInstigator;
	TWeakObjectPtr<AMT2MetinStone> SpawningStone;

	UPROPERTY(Transient)
	TArray<FMT2DamageShare> DamageShares;
	TArray<TWeakObjectPtr<AMT2Mob>> SpawnGroupMembers;
};
