/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Player/MT2PlayerTypes.h"
#include "Skills/MT2SkillTypes.h"
#include "MT2SkillComponent.generated.h"

class UMT2SkillDefinition;
class UMT2SkillSet;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMT2SkillLevelsChangedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMT2SkillGroupChangedSignature, int32, NewSkillGroup);

// Lives on the PlayerState (survives respawns; replication and persistence ride the PlayerState
// pipeline). Recreates the old CHARACTER skill handling: skill group selection at level 5,
// point-based learning with the old gates (level limit, prerequisite, Normal-mastery only) and the
// authentic random 17..20 Master breakthrough, mastery derived from level, vnum-keyed levels.
UCLASS(BlueprintType, ClassGroup = "MT2", meta = (BlueprintSpawnableComponent))
class METIN2_API UMT2SkillComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMT2SkillComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Skills")
	int32 GetSkillLevel(int32 SkillVnum) const;

	UFUNCTION(BlueprintPure, Category = "Skills")
	EMT2SkillMastery GetSkillMastery(int32 SkillVnum) const;

	UFUNCTION(BlueprintPure, Category = "Skills")
	TArray<FMT2SkillLevelEntry> GetSkillLevels() const;

	// 0 = no group chosen yet (the old skill window shows the group-selection page).
	UFUNCTION(BlueprintPure, Category = "Skills")
	int32 GetSkillGroup() const { return SkillGroup; }

	// Resolved skill set for the owner's race + chosen group; null until a group is selected.
	UFUNCTION(BlueprintPure, Category = "Skills")
	UMT2SkillSet* GetSkillSet() const;

	UFUNCTION(BlueprintPure, Category = "Skills")
	UMT2SkillDefinition* FindSkillDefinition(int32 SkillVnum) const;

	// Old SetSkillGroup: one-shot choice, requires character level >= 5 and group 1..2.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Skills")
	bool SelectSkillGroup(int32 NewSkillGroup);

	// Old SkillLevelUp(SKILL_UP_BY_POINT): spends one PlayerState skill point, all gates included.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Skills")
	bool LearnSkillByPoint(int32 SkillVnum);

	// Whether a skill book may be read for this skill right now: it must be at Master mastery (M1-M10)
	// and the player must have exp to spend. G1+ uses the soulstone grand-master path.
	UFUNCTION(BlueprintPure, Category = "Skills")
	bool CanReadSkillBook(int32 SkillVnum) const;

	// A specific reason the skill can't be raised by book right now (wrong race, wrong class, not yet
	// Master, already Grand Master, no exp...), or empty text when it is eligible. For chat feedback.
	FText GetSkillBookDenyReason(int32 SkillVnum) const;

	// Old SkillLevelUp(SKILL_UP_BY_BOOK), master path: spends the configured flat EXP cost, then a 35% roll makes
	// progress. M(n) -> M(n+1) needs n successful reads (M1->M2 = 1 ... M10->G1 = 10). OutReadsRemaining
	// is the number of *successful* reads still needed for the next level.
	EMT2SkillBookResult LearnSkillByBook(int32 SkillVnum, int32& OutReadsRemaining);

	// Accumulated successful reads toward the next mastery level (for persistence/UI).
	int32 GetMasteryReadCount(int32 SkillVnum) const;
	int32 GetGrandMasterReadCount(int32 SkillVnum) const;
	// Soul Stone path: one accepted read either fails or advances G(n) to G(n+1)/P.
	bool CanTrainGrandMasterSkill(int32 SkillVnum) const;
	bool TrainGrandMasterSkill(int32 SkillVnum);
	int64 GetSkillBookCooldownEnd(int32 SkillVnum) const;
	int64 GetSkillBookCooldownRemaining(int32 SkillVnum) const;
	// Persistence restore (authority only).
	void RestoreMasteryReadCount(int32 SkillVnum, int32 Count);
	void RestoreGrandMasterReadCount(int32 SkillVnum, int32 Count);
	// Called after quest flags load: migrate pre-quest-binding save tallies only when no flag exists.
	void MigrateLegacyGrandMasterReadCounts();
	void RestoreSkillBookCooldown(int32 SkillVnum, int64 CooldownEndUnix);

	// Whether the + button should show for this skill (mirrors uicharacter CanShowPlusButton).
	UFUNCTION(BlueprintPure, Category = "Skills")
	bool CanLearnSkillByPoint(int32 SkillVnum) const;

	// Raw authority setter for persistence restore and GM commands; no gates besides clamping.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Skills")
	bool SetSkillLevel(int32 SkillVnum, int32 NewLevel);

	// Clears the selected class and every learned skill. Used when changing character race.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Skills")
	void ResetAllSkills();

	// Owning-client requests (the + button and the group-selection page call these).
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Skills")
	void ServerLearnSkillByPoint(int32 SkillVnum);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Skills")
	void ServerSelectSkillGroup(int32 NewSkillGroup);

	UPROPERTY(BlueprintAssignable, Category = "Skills")
	FMT2SkillLevelsChangedSignature OnSkillLevelsChanged;

	UPROPERTY(BlueprintAssignable, Category = "Skills")
	FMT2SkillGroupChangedSignature OnSkillGroupChanged;

	void NotifySkillLevelsChanged();

	// Persistence restore: applies a saved group without the level-5/once-only gates.
	void RestorePersistedSkillGroup(int32 SavedSkillGroup);

	// Client-side cooldown mirror (fed by the character's Client RPC when a use succeeds) so the
	// UI can draw the draining overlay without asking the server every frame.
	void NotifyCooldownStarted(int32 SkillVnum, float CooldownSeconds);

	// Remaining seconds (0 when ready); OutDuration = the full cooldown it started from.
	UFUNCTION(BlueprintPure, Category = "Skills")
	float GetCooldownRemaining(int32 SkillVnum, float& OutDuration) const;

private:
	friend class FMT2QuestGrandMasterTrainingTest;
	UFUNCTION()
	void OnRep_SkillGroup();

	EMT2CharacterRace GetOwnerRace() const;
	int32 GetOwnerCharacterLevel() const;
	bool HasBookReadingEffect(int32 AffectType) const;
	void ConsumeBookReadingEffect(int32 AffectType);
	void MarkPersistenceDirty() const;

	UPROPERTY(Replicated)
	FMT2SkillLevelContainer SkillLevels;

	// Per-skill successful-read tally toward the next mastery level. Server-only state (the client learns
	// results through the read RPC); persisted via the PlayerState skills array. Reset to 0 on level up.
	UPROPERTY()
	TMap<int32, int32> MasteryReadCounts;

	UPROPERTY()
	TMap<int32, int32> GrandMasterReadCounts;

	// Absolute UTC Unix timestamp. Wall-clock storage prevents reconnecting or server downtime from
	// resetting the one-day, per-skill reading lock.
	UPROPERTY()
	TMap<int32, int64> SkillBookCooldownEnds;

	UPROPERTY(ReplicatedUsing = OnRep_SkillGroup)
	int32 SkillGroup = 0;

	// Cache for GetSkillSet; rebuilt when the group changes or the race differs.
	UPROPERTY(Transient)
	mutable TObjectPtr<UMT2SkillSet> CachedSkillSet;

	mutable int32 CachedSkillSetGroup = 0;
	mutable EMT2CharacterRace CachedSkillSetRace = EMT2CharacterRace::Warrior;

	struct FClientCooldown
	{
		double EndTime = 0.0;
		float Duration = 0.0f;
	};
	TMap<int32, FClientCooldown> ClientCooldowns;
};
