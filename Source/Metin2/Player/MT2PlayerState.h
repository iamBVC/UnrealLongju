/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "AbilitySystemInterface.h"
#include "Components/MT2StatusEffectComponent.h"
#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "GameplayTagContainer.h"
#include "Items/MT2QuickSlotTypes.h"
#include "Persistence/MT2Persistable.h"
#include "Party/MT2PartyTypes.h"
#include "Items/MT2ItemTypes.h"
#include "Player/MT2PlayerTypes.h"
#include "Stats/MT2StatTypes.h"
#include "MT2PlayerState.generated.h"

class UAbilitySystemComponent;
class UCurveFloat;
class UGameplayAbility;
class UMT2CoreAttributeSet;
class UMT2QuestComponent;
class UMT2MessengerComponent;
class UMT2GuildComponent;
class UMT2QuestManagerComponent;
class UMT2SkillComponent;
class UMT2PersistenceComponent;
class UMT2PrimaryStatsComponent;
class UMT2TradeComponent;
class UMT2DuelComponent;
class AMT2PlayerCharacter;
class AMT2Party;
struct FMT2PrimaryStats;
struct FOnAttributeChangeData;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMT2LevelChangedSignature, int32, OldLevel, int32, NewLevel);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FMT2ExperienceChangedSignature, int64, OldExperience, int64, NewExperience);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FMT2ProgressionPointsChangedSignature, int32, OldPoints, int32, NewPoints);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FMT2CharacterNameChangedSignature, const FString&, CharacterName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FMT2AppearanceChangedSignature, const FMT2CharacterAppearance&, Appearance);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMT2EmpireChangedSignature, EMT2Empire, Empire);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FMT2GuildChangedSignature, int32, GuildId, const FString&, GuildName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMT2KarmaChangedSignature, int32, KarmaPoints);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMT2AggressiveModeChangedSignature, bool, bEnabled);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FMT2PartyChangedSignature, AMT2Party*, OldParty, AMT2Party*, NewParty);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FMT2PartyMembershipChangedSignature, bool, bInParty);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FMT2YangChangedSignature, int64, OldYang, int64, NewYang);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMT2QuickSlotsChangedSignature);

UCLASS(Blueprintable)
class METIN2_API AMT2PlayerState : public APlayerState, public IAbilitySystemInterface, public IMT2Persistable
{
	GENERATED_BODY()
	friend class FMT2FakePlayersTest;

public:
	AMT2PlayerState();

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual ELifetimeCondition AllowActorComponentToReplicate(const UActorComponent* ComponentToReplicate) const override;
	virtual void PostInitializeComponents() override;
	void InitializeAbilitySystem(AActor* AvatarActor);
	virtual FString CapturePersistentStateJson_Implementation() const override;
	virtual bool ApplyPersistentStateJson_Implementation(const FString& PayloadJson) override;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Persistence")
	void SetPersistenceIdentity(const FString& CharacterId, const FString& AccountId);

	UFUNCTION(BlueprintPure, Category = "Persistence")
	UMT2PersistenceComponent* GetPersistenceComponent() const { return PersistenceComponent; }
	UFUNCTION(BlueprintPure, Category="Trade") UMT2TradeComponent* GetTradeComponent() const { return TradeComponent; }
	UFUNCTION(BlueprintPure, Category="Combat|Duel") UMT2DuelComponent* GetDuelComponent() const { return DuelComponent; }

	UFUNCTION(BlueprintPure, Category = "Character")
	FString GetCharacterName() const { return GetPlayerName(); }

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Character")
	bool SetCharacterName(const FString& NewName);

	// True for GM/admin characters (admins database, or any player in PIE - the old game's
	// test_server rule). Only admins may execute chat commands; admins show the GM mark.
	UFUNCTION(BlueprintPure, Category = "Admin")
	bool IsAdmin() const { return bIsAdmin; }

	UFUNCTION(BlueprintPure, Category = "Progression")
	int32 GetCharacterLevel() const { return CharacterLevel; }

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Progression")
	bool SetCharacterLevel(int32 NewLevel);

	UFUNCTION(BlueprintPure, Category = "Progression")
	int64 GetExperience() const { return Experience; }

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Progression")
	bool SetExperience(int64 NewExperience);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Progression")
	bool AddExperience(int64 Amount);

	UFUNCTION(Client, Reliable)
	void ClientPlayProgressionSounds(int32 ExperienceSteps, int32 LevelsGained);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayProgressionEffects(int32 ExperienceSteps, int32 LevelsGained);

	UFUNCTION(BlueprintPure, Category = "Progression")
	int64 GetRequiredExperienceForLevel(int32 Level) const;

	UFUNCTION(BlueprintPure, Category = "Progression")
	int64 GetRequiredExperienceForNextLevel() const;

	UFUNCTION(BlueprintPure, Category = "Progression")
	float GetExperienceProgress() const;

	UFUNCTION(BlueprintPure, Category = "Progression")
	int32 GetMaximumCharacterLevel() const;

	UFUNCTION(BlueprintPure, Category = "Progression")
	int32 GetUnspentSkillPoints() const { return UnspentSkillPoints; }

	UFUNCTION(BlueprintPure, Category = "Progression")
	int32 GetUnspentStatPoints() const { return UnspentStatPoints; }

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Progression")
	void ServerSpendStatPoint(EMT2PrimaryStat Stat);

	// Consumes one unspent skill point; the skill component calls this when a point-up succeeds.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Progression")
	bool SpendSkillPoint();

	// Clears race-specific skills and restores the one point earned per completed level.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Progression")
	void ResetSkillsForRaceChange();

	UFUNCTION(BlueprintPure, Category = "Quick Slots")
	const TArray<FMT2QuickSlotAssignment>& GetQuickSlots() const { return QuickSlots; }

	// Owning-client request: bind (or clear with type None) one taskbar cell.
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Quick Slots")
	void ServerSetQuickSlot(int32 SlotIndex, EMT2QuickSlotType SlotType, int32 Vnum);

	UFUNCTION(BlueprintPure, Category = "Character")
	const FMT2CharacterAppearance& GetCharacterAppearance() const { return CharacterAppearance; }

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Character")
	bool SetCharacterAppearance(const FMT2CharacterAppearance& NewAppearance);

	UFUNCTION(BlueprintPure, Category = "Character")
	EMT2Empire GetEmpire() const { return Empire; }

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Character")
	bool SetEmpire(EMT2Empire NewEmpire);

	// Portal teleports stash a destination XY here so the destination map server spawns the player at
	// that spot (ground-snapped by RepairPlayerSpawn) instead of the town. Persisted so it survives the
	// cross-server transfer save/load, and consumed on the first arrival.
	void SetPendingSpawnLocation(const FVector2D& Location) { PendingSpawnLocation = Location; bHasPendingSpawnLocation = true; }
	bool HasPendingSpawnLocation() const { return bHasPendingSpawnLocation; }
	void ClearPendingSpawnLocation()
	{
		PendingSpawnLocation = FVector2D::ZeroVector;
		bHasPendingSpawnLocation = false;
	}
	bool ConsumePendingSpawnLocation(FVector2D& OutLocation)
	{
		if (!bHasPendingSpawnLocation)
		{
			return false;
		}
		OutLocation = PendingSpawnLocation;
		bHasPendingSpawnLocation = false;
		return true;
	}

	UFUNCTION(BlueprintPure, Category = "Guild")
	int32 GetGuildId() const { return GuildId; }

	UFUNCTION(BlueprintPure, Category = "Guild")
	const FString& GetGuildName() const { return GuildName; }

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Guild")
	bool SetGuild(int32 NewGuildId, const FString& NewGuildName);

	// Old PK ("aggressive") mode: while on, this player may also damage same-empire players.
	// It is off by default and changed authoritatively by the server.
	UFUNCTION(BlueprintPure, Category = "PvP")
	bool IsAggressiveMode() const { return bAggressiveMode; }

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "PvP")
	void SetAggressiveMode(bool bEnabled);

	// Owning-client request used by the settings menu. The server remains authoritative over PvP mode.
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "PvP")
	void ServerSetAggressiveMode(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Party")
	AMT2Party* GetParty() const { return Party; }

	UFUNCTION(BlueprintPure, Category = "Party")
	bool IsInParty() const { return bInParty; }

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Party")
	void SetParty(AMT2Party* NewParty);

	void SetPartyMemberSnapshot(const TArray<FMT2PartyMemberData>& NewMembers);
	const TArray<FMT2PartyMemberData>& GetPartyMemberSnapshot() const { return PartyMemberSnapshot; }

	UFUNCTION(BlueprintPure, Category = "Karma")
	int32 GetKarmaPoints() const { return GetRawAlignment() / 10; }

	UFUNCTION(BlueprintPure, Category = "Karma")
	int32 GetDisplayedKarmaPoints() const { return Alignment.RawPoints / 10; }
	int32 GetRawAlignment() const { return Alignment.bHidden ? RealAlignmentRaw : Alignment.RawPoints; }
	int32 GetDisplayedRawAlignment() const { return Alignment.RawPoints; }
	bool SetRawAlignment(int32 RawPoints);
	bool ChangeKarmaPoints(int32 Amount);
	bool ChangeLegacyAlignment(int32 RawAmount);
	void SetAlignmentHidden(bool bHideAlignment);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Karma")
	bool SetKarmaPoints(int32 NewKarmaPoints);

	UFUNCTION(BlueprintPure, Category = "Currency")
	int64 GetYang() const { return Yang; }

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Currency")
	bool SetYang(int64 NewYang);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Currency")
	bool AddYang(int64 Amount);

	void NotifyYangReceived(int64 Amount) const;

	UFUNCTION(BlueprintPure, Category = "Status Effects")
	bool HasStatusEffect(FGameplayTag StatusTag) const;

	UFUNCTION(BlueprintPure, Category = "Status Effects")
	FGameplayTagContainer GetStatusEffects() const;

	UFUNCTION(BlueprintPure, Category = "Attributes")
	UMT2CoreAttributeSet* GetCoreAttributes() const { return CoreAttributes; }

	UFUNCTION(BlueprintPure, Category = "Stats|Primary")
	UMT2PrimaryStatsComponent* GetPrimaryStatsComponent() const { return PrimaryStatsComponent; }

	UFUNCTION(BlueprintPure, Category = "Skills")
	UMT2SkillComponent* GetSkillComponent() const { return SkillComponent; }

	UFUNCTION(BlueprintPure, Category = "Quests")
	UMT2QuestComponent* GetQuestComponent() const { return QuestComponent; }

	UFUNCTION(BlueprintPure, Category = "Quests")
	UMT2QuestManagerComponent* GetQuestManagerComponent() const { return QuestManagerComponent; }

	UFUNCTION(BlueprintPure, Category = "Messenger")
	UMT2MessengerComponent* GetMessengerComponent() const { return MessengerComponent; }
	UFUNCTION(BlueprintPure, Category = "Guild")
	UMT2GuildComponent* GetGuildComponent() const { return GuildComponent; }

	// Buffs/debuffs restored from the persistent payload before the pawn existed; the character pulls
	// these into its UMT2StatusEffectComponent when it initializes (same staging pattern as the pawn
	// transform below). Server-only.
	bool ConsumePendingStatusEffects(TArray<FMT2StatusEffect>& OutEffects);
	bool ConsumePendingInventory(
		TArray<FMT2ItemSlot>& OutSlots, TArray<FMT2ItemSlot>& OutEquipment);

	// Called after the persistent inventory is restored. Grants Project Settings startup items once
	// and immediately persists both the items and the completion flag in the same player save.
	bool GrantStartingItemsIfNeeded(AMT2PlayerCharacter* Character);

	UPROPERTY(BlueprintAssignable, Category = "Character")
	FMT2CharacterNameChangedSignature OnCharacterNameChanged;

	UPROPERTY(BlueprintAssignable, Category = "Character")
	FMT2AppearanceChangedSignature OnCharacterAppearanceChanged;

	UPROPERTY(BlueprintAssignable, Category = "Character")
	FMT2EmpireChangedSignature OnEmpireChanged;

	UPROPERTY(BlueprintAssignable, Category = "Guild")
	FMT2GuildChangedSignature OnGuildChanged;

	UPROPERTY(BlueprintAssignable, Category = "Karma")
	FMT2KarmaChangedSignature OnKarmaChanged;

	UPROPERTY(BlueprintAssignable, Category = "PvP")
	FMT2AggressiveModeChangedSignature OnAggressiveModeChanged;

	UPROPERTY(BlueprintAssignable, Category = "Party")
	FMT2PartyChangedSignature OnPartyChanged;

	UPROPERTY(BlueprintAssignable, Category = "Party")
	FMT2PartyMembershipChangedSignature OnPartyMembershipChanged;

	UPROPERTY(BlueprintAssignable, Category = "Party")
	FMT2PartyMembersChangedSignature OnPartyMemberSnapshotChanged;

	UPROPERTY(BlueprintAssignable, Category = "Currency")
	FMT2YangChangedSignature OnYangChanged;

	UPROPERTY(BlueprintAssignable, Category = "Progression")
	FMT2LevelChangedSignature OnLevelChanged;

	UPROPERTY(BlueprintAssignable, Category = "Progression")
	FMT2ExperienceChangedSignature OnExperienceChanged;

	UPROPERTY(BlueprintAssignable, Category = "Progression")
	FMT2ProgressionPointsChangedSignature OnSkillPointsChanged;

	UPROPERTY(BlueprintAssignable, Category = "Progression")
	FMT2ProgressionPointsChangedSignature OnStatPointsChanged;

	UPROPERTY(BlueprintAssignable, Category = "Quick Slots")
	FMT2QuickSlotsChangedSignature OnQuickSlotsChanged;

protected:
	virtual void BeginPlay() override;
	virtual void OnRep_PlayerName() override;

private:
	// Portal destination spawn override (server-only, persisted). See SetPendingSpawnLocation.
	FVector2D PendingSpawnLocation = FVector2D::ZeroVector;
	bool bHasPendingSpawnLocation = false;

	// Authority: resolves bIsAdmin from the admin subsystem (PIE default / admins DB lookup by
	// character name). Re-run whenever the character name is (re)assigned.
	void ResolveAdminStatus();

	UPROPERTY(Replicated)
	bool bIsAdmin = false;

	UFUNCTION()
	void OnRep_QuickSlots();

	// MT2QuickSlots::TotalSlots entries; owner-only replication like the rest of the loadout.
	UPROPERTY(ReplicatedUsing = OnRep_QuickSlots)
	TArray<FMT2QuickSlotAssignment> QuickSlots;

	UFUNCTION()
	void OnRep_CharacterLevel(int32 OldLevel);

	UFUNCTION()
	void OnRep_Experience(int64 OldExperience);

	UFUNCTION()
	void OnRep_UnspentSkillPoints(int32 OldPoints);

	UFUNCTION()
	void OnRep_UnspentStatPoints(int32 OldPoints);

	UFUNCTION()
	void OnRep_CharacterAppearance(const FMT2CharacterAppearance& OldAppearance);

	UFUNCTION()
	void OnRep_Empire(EMT2Empire OldEmpire);

	UFUNCTION()
	void OnRep_Guild();

	UFUNCTION()
	void OnRep_KarmaPoints();

	UFUNCTION()
	void OnRep_AggressiveMode();

	UFUNCTION()
	void OnRep_Party(AMT2Party* OldParty);

	UFUNCTION()
	void OnRep_InParty();

	UFUNCTION()
	void OnRep_PartyMemberSnapshot();

	UFUNCTION()
	void OnRep_Yang(int64 OldYang);

	UFUNCTION()
	void HandleSkillLevelsChanged();

	UFUNCTION()
	void HandlePrimaryStatsChanged(FMT2PrimaryStats OldStats, FMT2PrimaryStats NewStats);

	void HandlePersistentAttributeChanged(const FOnAttributeChangeData& ChangeData);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Abilities", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Abilities", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2CoreAttributeSet> CoreAttributes;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Skills", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2SkillComponent> SkillComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Quests", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2QuestComponent> QuestComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Quests", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2QuestManagerComponent> QuestManagerComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Messenger", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2MessengerComponent> MessengerComponent;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Guild", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2GuildComponent> GuildComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Trade", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2TradeComponent> TradeComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat|Duel", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UMT2DuelComponent> DuelComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats|Primary", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2PrimaryStatsComponent> PrimaryStatsComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Persistence", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMT2PersistenceComponent> PersistenceComponent;

	UPROPERTY(EditDefaultsOnly, Category = "Abilities")
	TArray<TSubclassOf<UGameplayAbility>> DefaultAbilities;

	UPROPERTY(EditDefaultsOnly, Category = "Progression")
	TSoftObjectPtr<UCurveFloat> ExperienceCurve;

	UPROPERTY(ReplicatedUsing = OnRep_CharacterLevel)
	int32 CharacterLevel = 1;

	UPROPERTY(ReplicatedUsing = OnRep_Experience)
	int64 Experience = 0;

	UPROPERTY(ReplicatedUsing = OnRep_UnspentSkillPoints)
	int32 UnspentSkillPoints = 0;

	UPROPERTY(ReplicatedUsing = OnRep_UnspentStatPoints)
	int32 UnspentStatPoints = 0;

	// Highest 25% milestone already rewarded for the current level (0..3). It intentionally does
	// not decrease with EXP loss, preventing books/death penalties from making rewards repeatable.
	int32 ExperienceMilestoneStep = 0;

	UPROPERTY(ReplicatedUsing = OnRep_CharacterAppearance)
	FMT2CharacterAppearance CharacterAppearance;

	UPROPERTY(ReplicatedUsing = OnRep_Empire)
	EMT2Empire Empire = EMT2Empire::None;

	UPROPERTY(ReplicatedUsing = OnRep_Guild)
	int32 GuildId = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Guild)
	FString GuildName;

	UPROPERTY(ReplicatedUsing = OnRep_KarmaPoints)
	FMT2AlignmentState Alignment;

	// Hidden real alignment is not disclosed to other players. Displayed state remains public.
	UPROPERTY(ReplicatedUsing = OnRep_KarmaPoints)
	int32 RealAlignmentRaw = 0;

	UPROPERTY(ReplicatedUsing = OnRep_AggressiveMode)
	bool bAggressiveMode = false;

	UPROPERTY(ReplicatedUsing = OnRep_Party)
	TObjectPtr<AMT2Party> Party;

	UPROPERTY(ReplicatedUsing = OnRep_InParty)
	bool bInParty = false;

	// Private replicated view model for the owning client's party UI. This avoids making UI startup
	// depend on resolving the replicated AMT2Party actor pointer first.
	UPROPERTY(ReplicatedUsing = OnRep_PartyMemberSnapshot)
	TArray<FMT2PartyMemberData> PartyMemberSnapshot;

	UPROPERTY(ReplicatedUsing = OnRep_Yang)
	int64 Yang = 0;

	FTransform PendingPersistentPawnTransform = FTransform::Identity;
	bool bHasPendingPersistentPawnTransform = false;
	TArray<FMT2StatusEffect> PendingRestoredStatusEffects;
	bool bHasPendingRestoredStatusEffects = false;
	TArray<FMT2ItemSlot> PendingInventorySlots;
	TArray<FMT2ItemSlot> PendingEquipmentSlots;
	bool bHasPendingInventory = false;
	bool bStartingItemsGranted = false;
	TArray<FDelegateHandle> PersistentAttributeHandles;

};
