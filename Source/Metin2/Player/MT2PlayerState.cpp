/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Player/MT2PlayerState.h"
#include "Config/MT2PathSettings.h"
#include "Audio/MT2SoundPlaybackSubsystem.h"

#include "Abilities/MT2GameplayTags.h"
#include "Abilities/MT2CoreAttributeSet.h"
#include "Abilities/MT2GameplayAbilityBasicAttack.h"
#include "AbilitySystemComponent.h"
#include "Duel/MT2DuelComponent.h"
#include "Characters/MT2CharacterBase.h"
#include "Characters/MT2PlayerCharacter.h"
#include "Components/MT2HealthComponent.h"
#include "Components/MT2ManaComponent.h"
#include "Components/MT2StaminaComponent.h"
#include "Config/MT2GameplaySettings.h"
#include "Core/MT2VnumRegistrySubsystem.h"
#include "Curves/CurveFloat.h"
#include "Dom/JsonObject.h"
#include "Effects/MT2ExperienceOrbActor.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "Items/MT2InventoryComponent.h"
#include "Items/MT2ItemTemplate.h"
#include "Items/MT2ItemUtils.h"
#include "Items/MT2WorldItem.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "Messenger/MT2MessengerComponent.h"
#include "Guild/MT2GuildComponent.h"
#include "Player/MT2PlayerController.h"
#include "Trade/MT2TradeComponent.h"
#include "Persistence/MT2PersistenceComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Server/MT2AdminSubsystem.h"
#include "Quests/MT2QuestComponent.h"
#include "Quests/MT2QuestManagerComponent.h"
#include "Skills/MT2SkillComponent.h"
#include "Stats/MT2PrimaryStatsComponent.h"
#include "Stats/MT2PlayerStatFormula.h"

namespace
{
	bool HasNoPrimaryStats(const FMT2PrimaryStats& Stats)
	{
		return Stats.Strength == 0 && Stats.Dexterity == 0 &&
			Stats.Constitution == 0 && Stats.Intelligence == 0;
	}
}

AMT2PlayerState::AMT2PlayerState()
{
	// Component conditions are evaluated before object replicators in the registered-list path.
	bReplicateUsingRegisteredSubObjectList = true;
	// Mutations request immediate replication; the baseline is for unchanged state/subobjects.
	SetNetUpdateFrequency(FMath::Clamp(UMT2GameplaySettings::Get().PlayerStateReplicationRate, 1.f, 30.f));

	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	CoreAttributes = CreateDefaultSubobject<UMT2CoreAttributeSet>(TEXT("CoreAttributes"));
	SkillComponent = CreateDefaultSubobject<UMT2SkillComponent>(TEXT("SkillComponent"));
	QuestComponent = CreateDefaultSubobject<UMT2QuestComponent>(TEXT("QuestComponent"));
	QuestManagerComponent = CreateDefaultSubobject<UMT2QuestManagerComponent>(TEXT("QuestManagerComponent"));
	PrimaryStatsComponent = CreateDefaultSubobject<UMT2PrimaryStatsComponent>(TEXT("PrimaryStatsComponent"));
	PersistenceComponent = CreateDefaultSubobject<UMT2PersistenceComponent>(TEXT("PersistenceComponent"));
	MessengerComponent = CreateDefaultSubobject<UMT2MessengerComponent>(TEXT("MessengerComponent"));
	GuildComponent = CreateDefaultSubobject<UMT2GuildComponent>(TEXT("GuildComponent"));
	TradeComponent = CreateDefaultSubobject<UMT2TradeComponent>(TEXT("TradeComponent"));
	DuelComponent = CreateDefaultSubobject<UMT2DuelComponent>(TEXT("DuelComponent"));
	DefaultAbilities.Add(UMT2GameplayAbilityBasicAttack::StaticClass());
	ExperienceCurve = TSoftObjectPtr<UCurveFloat>(
		FSoftObjectPath(UMT2PathSettings::Path(TEXT("ExperienceCurve"))));
	QuickSlots.SetNum(MT2QuickSlots::TotalSlots);
}

UAbilitySystemComponent* AMT2PlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AMT2PlayerState::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	// Enforce before BeginPlay builds component registration, including Blueprint-derived states.
	bReplicateUsingRegisteredSubObjectList = true;
}

ELifetimeCondition AMT2PlayerState::AllowActorComponentToReplicate(const UActorComponent* ComponentToReplicate) const
{
	if (ComponentToReplicate && ComponentToReplicate->GetIsReplicated() &&
		(ComponentToReplicate == QuestComponent || ComponentToReplicate == QuestManagerComponent ||
		 ComponentToReplicate == MessengerComponent || ComponentToReplicate == GuildComponent ||
		 ComponentToReplicate == TradeComponent))
	{
		return COND_OwnerOnly;
	}
	// ASC retains its own legacy subobject hook for attributes/abilities. SkillGroup, primary
	// stats and duels are public; do not gate their entire components to the owning connection.
	return Super::AllowActorComponentToReplicate(ComponentToReplicate);
}

void AMT2PlayerState::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		AbilitySystemComponent->RegisterGenericGameplayTagEvent().AddWeakLambda(this,
			[this](const FGameplayTag, int32) { ForceNetUpdate(); });
	}
	SkillComponent->OnSkillLevelsChanged.AddUniqueDynamic(this, &AMT2PlayerState::HandleSkillLevelsChanged);
	PrimaryStatsComponent->OnPrimaryStatsChanged.AddUniqueDynamic(
		this, &AMT2PlayerState::HandlePrimaryStatsChanged);
	if (HasAuthority() && HasNoPrimaryStats(PrimaryStatsComponent->GetBaseStats()))
	{
		PrimaryStatsComponent->SetBaseStats(
			MT2PlayerStatFormula::GetInitialPrimaryStats(CharacterAppearance.Race));
	}
	if (HasAuthority())
	{
		ResolveAdminStatus();
	}
}

void AMT2PlayerState::ResolveAdminStatus()
{
	// AI-owned test characters never acquire GM rights or query the account backend.
	if (IsABot()) return;
	UMT2AdminSubsystem* Admin = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMT2AdminSubsystem>() : nullptr;
	if (!Admin)
	{
		return;
	}
	Admin->QueryAdminStatus(GetWorld(), GetCharacterName(),
		[WeakThis = TWeakObjectPtr<AMT2PlayerState>(this)](bool bAdmin)
		{
			AMT2PlayerState* State = WeakThis.Get();
			if (State && State->bIsAdmin != bAdmin)
			{
				State->bIsAdmin = bAdmin;
				State->ForceNetUpdate();
			}
		});
}

void AMT2PlayerState::InitializeAbilitySystem(AActor* AvatarActor)
{
	if (!AvatarActor)
	{
		return;
	}

	AbilitySystemComponent->InitAbilityActorInfo(this, AvatarActor);
	if (HasAuthority() && PersistentAttributeHandles.IsEmpty())
	{
		const FGameplayAttribute PersistentAttributes[] = {
			UMT2CoreAttributeSet::GetHealthAttribute(),
			UMT2CoreAttributeSet::GetMaxHealthAttribute(),
			UMT2CoreAttributeSet::GetManaAttribute(),
			UMT2CoreAttributeSet::GetMaxManaAttribute(),
			UMT2CoreAttributeSet::GetStaminaAttribute(),
			UMT2CoreAttributeSet::GetMaxStaminaAttribute(),
			UMT2CoreAttributeSet::GetMovementSpeedAttribute()
		};
		for (const FGameplayAttribute& Attribute : PersistentAttributes)
		{
			PersistentAttributeHandles.Add(
				AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(Attribute).AddUObject(
					this, &AMT2PlayerState::HandlePersistentAttributeChanged));
		}
	}
	if (HasAuthority() && bHasPendingPersistentPawnTransform)
	{
		AvatarActor->SetActorTransform(PendingPersistentPawnTransform, false, nullptr, ETeleportType::TeleportPhysics);
		bHasPendingPersistentPawnTransform = false;
	}
	if (!HasAuthority())
	{
		return;
	}

	for (const TSubclassOf<UGameplayAbility>& AbilityClass : DefaultAbilities)
	{
		if (AbilityClass && !AbilitySystemComponent->FindAbilitySpecFromClass(AbilityClass))
		{
			AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(AbilityClass, 1, INDEX_NONE, this));
		}
	}
}

void AMT2PlayerState::SetPersistenceIdentity(const FString& CharacterId, const FString& AccountId)
{
	if (HasAuthority() && PersistenceComponent)
	{
		PersistenceComponent->ConfigurePersistence(TEXT("player"), CharacterId, AccountId);
	}
}

void AMT2PlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AMT2PlayerState, CharacterLevel);
	DOREPLIFETIME_CONDITION(AMT2PlayerState, Experience, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AMT2PlayerState, UnspentSkillPoints, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AMT2PlayerState, UnspentStatPoints, COND_OwnerOnly);
	DOREPLIFETIME(AMT2PlayerState, CharacterAppearance);
	DOREPLIFETIME(AMT2PlayerState, Empire);
	DOREPLIFETIME(AMT2PlayerState, GuildId);
	DOREPLIFETIME(AMT2PlayerState, GuildName);
	DOREPLIFETIME(AMT2PlayerState, Alignment);
	DOREPLIFETIME_CONDITION(AMT2PlayerState, RealAlignmentRaw, COND_OwnerOnly);
	DOREPLIFETIME(AMT2PlayerState, bAggressiveMode);
	DOREPLIFETIME_CONDITION(AMT2PlayerState, Party, COND_OwnerOnly);
	DOREPLIFETIME(AMT2PlayerState, bInParty);
	DOREPLIFETIME_CONDITION(AMT2PlayerState, PartyMemberSnapshot, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AMT2PlayerState, Yang, COND_OwnerOnly);
	DOREPLIFETIME(AMT2PlayerState, bIsAdmin);
	DOREPLIFETIME_CONDITION(AMT2PlayerState, QuickSlots, COND_OwnerOnly);
}

void AMT2PlayerState::OnRep_QuickSlots()
{
	OnQuickSlotsChanged.Broadcast();
}

void AMT2PlayerState::ServerSetQuickSlot_Implementation(
	int32 SlotIndex, EMT2QuickSlotType SlotType, int32 Vnum)
{
	if (!QuickSlots.IsValidIndex(SlotIndex))
	{
		return;
	}
	// A binding lives in exactly one cell: dropping it somewhere else moves it rather than cloning
	// it (both kinds are keyed by vnum here, so a second copy would just be a redundant duplicate).
	if (Vnum > 0)
	{
		for (int32 Index = 0; Index < QuickSlots.Num(); ++Index)
		{
			if (Index != SlotIndex && QuickSlots[Index].Type == SlotType && QuickSlots[Index].Vnum == Vnum)
			{
				QuickSlots[Index] = FMT2QuickSlotAssignment();
			}
		}
	}

	FMT2QuickSlotAssignment& Assignment = QuickSlots[SlotIndex];
	Assignment.Type = Vnum > 0 ? SlotType : EMT2QuickSlotType::None;
	Assignment.Vnum = Vnum > 0 ? Vnum : 0;
	OnRep_QuickSlots();
	PersistenceComponent->MarkDirty();
	ForceNetUpdate();
}

bool AMT2PlayerState::SetCharacterName(const FString& NewName)
{
	if (!HasAuthority())
	{
		return false;
	}

	FString CleanName = NewName;
	CleanName.TrimStartAndEndInline();
	if (CleanName.IsEmpty() || CleanName.Len() > MT2PlayerLimits::MaxCharacterNameLength ||
		CleanName == GetPlayerName())
	{
		return false;
	}

	SetPlayerName(CleanName);
	PersistenceComponent->MarkDirty();
	// Admin rights are keyed by character name (old-game gm_list behavior), so re-resolve.
	ResolveAdminStatus();
	return true;
}

bool AMT2PlayerState::SetCharacterLevel(int32 NewLevel)
{
	if (!HasAuthority())
	{
		return false;
	}

	const int32 MaximumLevel = GetMaximumCharacterLevel();
	const int32 ValidLevel = FMath::Clamp(NewLevel, 1, MaximumLevel);
	if (CharacterLevel == ValidLevel)
	{
		if (CharacterLevel >= MaximumLevel)
		{
			ExperienceMilestoneStep = 0;
			return SetExperience(0);
		}
		return false;
	}

	const int32 OldLevel = CharacterLevel;
	CharacterLevel = ValidLevel;
	OnRep_CharacterLevel(OldLevel);
	if (CharacterLevel >= MaximumLevel)
	{
		ExperienceMilestoneStep = 0;
	}
	if (CharacterLevel >= MaximumLevel && Experience != 0)
	{
		const int64 OldExperience = Experience;
		Experience = 0;
		OnRep_Experience(OldExperience);
	}
	PersistenceComponent->MarkDirty();
	return true;
}

bool AMT2PlayerState::SetExperience(int64 NewExperience)
{
	if (!HasAuthority())
	{
		return false;
	}

	const int64 ValidExperience = CharacterLevel >= GetMaximumCharacterLevel()
		? 0
		: FMath::Max<int64>(NewExperience, 0);
	if (Experience == ValidExperience)
	{
		return false;
	}

	const int64 OldExperience = Experience;
	Experience = ValidExperience;
	OnRep_Experience(OldExperience);
	PersistenceComponent->MarkDirty();
	return true;
}

bool AMT2PlayerState::AddExperience(int64 Amount)
{
	if (!HasAuthority() || Amount == 0)
	{
		return false;
	}

	if (Amount < 0)
	{
		return SetExperience(Amount <= -Experience ? 0 : Experience + Amount);
	}

	const int32 MaximumLevel = GetMaximumCharacterLevel();
	if (CharacterLevel >= MaximumLevel)
	{
		return SetExperience(0);
	}

	const int32 OldLevel = CharacterLevel;
	const int64 OldExperience = Experience;
	int64 RemainingExperience = Amount > MAX_int64 - Experience ? MAX_int64 : Experience + Amount;
	int32 NewLevel = CharacterLevel;
	int32 NewMilestoneStep = FMath::Clamp(ExperienceMilestoneStep, 0, 3);
	int32 EarnedSkillPoints = 0;
	int32 EarnedStatPoints = 0;

	// Consume one threshold at a time. This deliberately supports a single reward crossing any
	// number of levels, e.g. a boss reward taking a player directly from level 5 to level 8.
	while (NewLevel < MaximumLevel)
	{
		const int64 RequiredExperience = GetRequiredExperienceForLevel(NewLevel);
		if (RequiredExperience <= 0 || RemainingExperience < RequiredExperience)
		{
			int32 ReachedStep = 0;
			for (int32 Step = 1; Step <= 3; ++Step)
			{
				if (RemainingExperience >= (RequiredExperience * Step) / 4)
				{
					ReachedStep = Step;
				}
			}
			if (ReachedStep > NewMilestoneStep)
			{
				EarnedStatPoints += ReachedStep - NewMilestoneStep;
				NewMilestoneStep = ReachedStep;
			}
			break;
		}
		EarnedStatPoints += 4 - NewMilestoneStep;
		++EarnedSkillPoints;
		RemainingExperience -= RequiredExperience;
		++NewLevel;
		NewMilestoneStep = 0;
	}
	if (NewLevel >= MaximumLevel)
	{
		// Match the old server: experience beyond the configured level cap is discarded.
		RemainingExperience = 0;
		NewMilestoneStep = 0;
	}

	const int32 OldSkillPoints = UnspentSkillPoints;
	const int32 OldStatPoints = UnspentStatPoints;
	CharacterLevel = NewLevel;
	Experience = RemainingExperience;
	ExperienceMilestoneStep = NewMilestoneStep;
	UnspentSkillPoints = FMath::Min<int64>(
		static_cast<int64>(UnspentSkillPoints) + EarnedSkillPoints, MAX_int32);
	UnspentStatPoints = FMath::Min<int64>(
		static_cast<int64>(UnspentStatPoints) + EarnedStatPoints, MAX_int32);
	if (CharacterLevel != OldLevel)
	{
		OnRep_CharacterLevel(OldLevel);
	}
	if (Experience != OldExperience)
	{
		OnRep_Experience(OldExperience);
	}
	if (UnspentSkillPoints != OldSkillPoints)
	{
		OnRep_UnspentSkillPoints(OldSkillPoints);
	}
	if (UnspentStatPoints != OldStatPoints)
	{
		OnRep_UnspentStatPoints(OldStatPoints);
	}
	PersistenceComponent->MarkDirty();
	ForceNetUpdate();
	// The level-up sound replaces each completed level's fourth quarter sound. The remaining count
	// represents the 25/50/75% milestones crossed by this reward.
	const int32 EarnedExperienceSteps = FMath::Max(EarnedStatPoints - EarnedSkillPoints, 0);
	if (EarnedExperienceSteps > 0 || EarnedSkillPoints > 0)
	{
		if (AMT2PlayerCharacter* Character = Cast<AMT2PlayerCharacter>(GetPawn()))
		{
			if (UMT2HealthComponent* Health = Character->GetHealthComponent())
			{
				Health->SetHealth(Health->GetMaxHealth());
			}
			if (UMT2ManaComponent* Mana = Character->GetManaComponent())
			{
				Mana->SetMana(Mana->GetMaxMana());
			}
			if (UMT2StaminaComponent* Stamina = Character->GetStaminaComponent())
			{
				Stamina->SetStamina(Stamina->GetMaxStamina());
			}
		}
		ClientPlayProgressionSounds(EarnedExperienceSteps, EarnedSkillPoints);
		MulticastPlayProgressionEffects(EarnedExperienceSteps, EarnedSkillPoints);
	}
	const bool bChanged = CharacterLevel != OldLevel || Experience != OldExperience ||
		UnspentSkillPoints != OldSkillPoints || UnspentStatPoints != OldStatPoints;
	if (bChanged)
	{
		if (AMT2PlayerController* Controller = Cast<AMT2PlayerController>(GetOwner()))
		{
			Controller->ClientNotifyExperienceReceived(Amount);
		}
	}
	return bChanged;
}

void AMT2PlayerState::MulticastPlayProgressionEffects_Implementation(
	int32 ExperienceSteps, int32 LevelsGained)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	AMT2PlayerCharacter* Character = Cast<AMT2PlayerCharacter>(GetPawn());
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	if (!Mesh)
	{
		return;
	}

	const UMT2GameplaySettings& Settings = UMT2GameplaySettings::Get();
	auto SpawnProgressionEffect = [this, Mesh](const TSoftObjectPtr<UParticleSystem>& EffectAsset)
	{
		if (UParticleSystem* Effect = EffectAsset.LoadSynchronous())
		{
			UGameplayStatics::SpawnEmitterAttached(
				Effect, Mesh, NAME_None, FVector::ZeroVector, FRotator::ZeroRotator,
				FVector::OneVector, EAttachLocation::KeepRelativeOffset,
				true, EPSCPoolMethod::AutoRelease, true);
		}
	};

	if (ExperienceSteps > 0)
	{
		SpawnProgressionEffect(Settings.ExperienceStepEffect);
	}
	if (LevelsGained > 0)
	{
		SpawnProgressionEffect(Settings.LevelUpEffect);
	}
}

void AMT2PlayerState::ClientPlayProgressionSounds_Implementation(
	int32 ExperienceSteps, int32 LevelsGained)
{
	const UMT2GameplaySettings& Settings = UMT2GameplaySettings::Get();
	if (ExperienceSteps > 0)
	{
		if (USoundBase* Sound = Settings.ExperienceStepSound.LoadSynchronous())
		{
			UMT2SoundPlaybackSubsystem::PlayExclusive2D(this, Sound);
		}
	}
	if (LevelsGained > 0)
	{
		if (USoundBase* Sound = Settings.LevelUpSound.LoadSynchronous())
		{
			UMT2SoundPlaybackSubsystem::PlayExclusive2D(this, Sound);
		}
	}
}

bool AMT2PlayerState::SpendSkillPoint()
{
	if (!HasAuthority() || UnspentSkillPoints <= 0)
	{
		return false;
	}
	const int32 OldPoints = UnspentSkillPoints;
	--UnspentSkillPoints;
	OnRep_UnspentSkillPoints(OldPoints);
	PersistenceComponent->MarkDirty();
	ForceNetUpdate();
	return true;
}

void AMT2PlayerState::ResetSkillsForRaceChange()
{
	if (!HasAuthority() || !SkillComponent)
	{
		return;
	}
	const int32 OldPoints = UnspentSkillPoints;
	SkillComponent->ResetAllSkills();
	UnspentSkillPoints = FMath::Max(CharacterLevel - 1, 0);
	if (UnspentSkillPoints != OldPoints)
	{
		OnRep_UnspentSkillPoints(OldPoints);
	}
	PersistenceComponent->MarkDirty();
	ForceNetUpdate();
}

void AMT2PlayerState::ServerSpendStatPoint_Implementation(EMT2PrimaryStat Stat)
{
	if (UnspentStatPoints <= 0 || !PrimaryStatsComponent)
	{
		return;
	}

	constexpr int32 MaximumBaseStat = 90;
	FMT2PrimaryStats Stats = PrimaryStatsComponent->GetBaseStats();
	int32* SelectedStat = nullptr;
	switch (Stat)
	{
	case EMT2PrimaryStat::Constitution: SelectedStat = &Stats.Constitution; break;
	case EMT2PrimaryStat::Intelligence: SelectedStat = &Stats.Intelligence; break;
	case EMT2PrimaryStat::Strength: SelectedStat = &Stats.Strength; break;
	case EMT2PrimaryStat::Dexterity: SelectedStat = &Stats.Dexterity; break;
	default: return;
	}
	if (!SelectedStat || *SelectedStat >= MaximumBaseStat)
	{
		return;
	}

	const int32 OldPoints = UnspentStatPoints;
	++(*SelectedStat);
	if (!PrimaryStatsComponent->SetBaseStats(Stats))
	{
		return;
	}
	--UnspentStatPoints;
	OnRep_UnspentStatPoints(OldPoints);
	if (PersistenceComponent)
	{
		PersistenceComponent->MarkDirty();
	}
	ForceNetUpdate();
}

int64 AMT2PlayerState::GetRequiredExperienceForLevel(int32 Level) const
{
	if (Level < 1 || Level >= GetMaximumCharacterLevel())
	{
		return 0;
	}
	if (const UCurveFloat* Curve = ExperienceCurve.LoadSynchronous())
	{
		return FMath::Max<int64>(FMath::RoundToInt64(Curve->GetFloatValue(Level)), 1);
	}

	// Keeps dedicated servers functional if an asset is accidentally omitted from a build. The
	// generated curve contains these same original-server values and remains the normal data source.
	return FMath::Max<int64>(FMath::RoundToInt64(
		44318.12 * (FMath::Pow(1.1157, static_cast<double>(Level)) - 1.0)), 1);
}

int32 AMT2PlayerState::GetMaximumCharacterLevel() const
{
	if (const UCurveFloat* Curve = ExperienceCurve.LoadSynchronous();
		Curve && !Curve->FloatCurve.IsEmpty())
	{
		return FMath::Max(FMath::RoundToInt(Curve->FloatCurve.GetLastKey().Time), 1);
	}
	return 1;
}

int64 AMT2PlayerState::GetRequiredExperienceForNextLevel() const
{
	return GetRequiredExperienceForLevel(CharacterLevel);
}

float AMT2PlayerState::GetExperienceProgress() const
{
	const int64 RequiredExperience = GetRequiredExperienceForNextLevel();
	return RequiredExperience > 0
		? FMath::Clamp(static_cast<double>(Experience) / static_cast<double>(RequiredExperience), 0.0, 1.0)
		: 1.0f;
}

bool AMT2PlayerState::SetCharacterAppearance(const FMT2CharacterAppearance& NewAppearance)
{
	if (!HasAuthority())
	{
		return false;
	}

	const FMT2CharacterAppearance OldAppearance = CharacterAppearance;
	const FMT2PrimaryStats OldRaceDefaults =
		MT2PlayerStatFormula::GetInitialPrimaryStats(OldAppearance.Race);
	const FMT2PrimaryStats CurrentStats = PrimaryStatsComponent->GetBaseStats();
	const bool bUseNewRaceDefaults = HasNoPrimaryStats(CurrentStats) ||
		(CurrentStats == OldRaceDefaults && CharacterLevel == 1 && Experience == 0);
	if (CharacterAppearance == NewAppearance)
	{
		if (bUseNewRaceDefaults && HasNoPrimaryStats(CurrentStats))
		{
			PrimaryStatsComponent->SetBaseStats(
				MT2PlayerStatFormula::GetInitialPrimaryStats(NewAppearance.Race));
		}
		return false;
	}
	CharacterAppearance = NewAppearance;
	if (bUseNewRaceDefaults)
	{
		PrimaryStatsComponent->SetBaseStats(
			MT2PlayerStatFormula::GetInitialPrimaryStats(NewAppearance.Race));
	}
	OnRep_CharacterAppearance(OldAppearance);
	ForceNetUpdate();
	PersistenceComponent->MarkDirty();
	return true;
}

bool AMT2PlayerState::SetEmpire(EMT2Empire NewEmpire)
{
	if (!HasAuthority() || Empire == NewEmpire)
	{
		return false;
	}

	const EMT2Empire OldEmpire = Empire;
	Empire = NewEmpire;
	OnRep_Empire(OldEmpire);
	PersistenceComponent->MarkDirty();
	return true;
}

bool AMT2PlayerState::SetGuild(int32 NewGuildId, const FString& NewGuildName)
{
	if (!HasAuthority())
	{
		return false;
	}

	FString CleanName = NewGuildName;
	CleanName.TrimStartAndEndInline();
	CleanName.LeftInline(24);
	const int32 CleanId = FMath::Max(NewGuildId, 0);
	if (CleanId == 0)
	{
		CleanName.Reset();
	}
	if (GuildId == CleanId && GuildName == CleanName)
	{
		return false;
	}

	GuildId = CleanId;
	GuildName = MoveTemp(CleanName);
	OnRep_Guild();
	PersistenceComponent->MarkDirty();
	return true;
}

void AMT2PlayerState::SetAggressiveMode(bool bEnabled)
{
	if (HasAuthority() && bAggressiveMode != bEnabled)
	{
		bAggressiveMode = bEnabled;
		OnRep_AggressiveMode();
		ForceNetUpdate();
	}
}

void AMT2PlayerState::ServerSetAggressiveMode_Implementation(bool bEnabled)
{
	SetAggressiveMode(bEnabled);
}

void AMT2PlayerState::OnRep_AggressiveMode()
{
	OnAggressiveModeChanged.Broadcast(bAggressiveMode);
}

void AMT2PlayerState::SetParty(AMT2Party* NewParty)
{
	if (!HasAuthority() || Party == NewParty)
	{
		return;
	}
	AMT2Party* OldParty = Party;
	Party = NewParty;
	bInParty = NewParty != nullptr;
	if (!NewParty)
	{
		PartyMemberSnapshot.Reset();
		OnRep_PartyMemberSnapshot();
	}
	OnRep_Party(OldParty);
	OnRep_InParty();
	ForceNetUpdate();
}

void AMT2PlayerState::SetPartyMemberSnapshot(const TArray<FMT2PartyMemberData>& NewMembers)
{
	if (!HasAuthority() || PartyMemberSnapshot == NewMembers) return;
	PartyMemberSnapshot = NewMembers;
	OnRep_PartyMemberSnapshot();
	ForceNetUpdate();
}

void AMT2PlayerState::OnRep_Party(AMT2Party* OldParty)
{
	OnPartyChanged.Broadcast(OldParty, Party);
}

void AMT2PlayerState::OnRep_InParty()
{
	OnPartyMembershipChanged.Broadcast(bInParty);
}

void AMT2PlayerState::OnRep_PartyMemberSnapshot()
{
	OnPartyMemberSnapshotChanged.Broadcast();
}

bool AMT2PlayerState::SetKarmaPoints(int32 NewKarmaPoints)
{
	return SetRawAlignment(FMath::Clamp(NewKarmaPoints, MT2KarmaLimits::Minimum, MT2KarmaLimits::Maximum) * 10);
}

bool AMT2PlayerState::SetRawAlignment(int32 RawPoints)
{
	if (!HasAuthority())
	{
		return false;
	}
	const int32 ClampedPoints = FMath::Clamp(RawPoints, MT2KarmaLimits::Minimum * 10, MT2KarmaLimits::Maximum * 10);
	if (RealAlignmentRaw == ClampedPoints)
	{
		return false;
	}
	RealAlignmentRaw = ClampedPoints;
	Alignment.RawPoints = Alignment.bHidden ? 0 : ClampedPoints;
	OnRep_KarmaPoints();
	PersistenceComponent->MarkDirty();
	return true;
}

bool AMT2PlayerState::ChangeKarmaPoints(int32 Amount)
{
	return SetRawAlignment(static_cast<int32>(FMath::Clamp<int64>(int64(RealAlignmentRaw) + int64(Amount) * 10,
		MT2KarmaLimits::Minimum * 10, MT2KarmaLimits::Maximum * 10)));
}

bool AMT2PlayerState::ChangeLegacyAlignment(int32 RawAmount)
{
	return SetRawAlignment(static_cast<int32>(FMath::Clamp<int64>(int64(RealAlignmentRaw) + RawAmount,
		MT2KarmaLimits::LegacyRawMinimum, MT2KarmaLimits::LegacyRawMaximum)));
}

void AMT2PlayerState::SetAlignmentHidden(bool bHideAlignment)
{
	if (!HasAuthority() || Alignment.bHidden == bHideAlignment) { return; }
	Alignment.bHidden = bHideAlignment;
	Alignment.RawPoints = bHideAlignment ? 0 : RealAlignmentRaw;
	OnRep_KarmaPoints();
}

bool AMT2PlayerState::SetYang(int64 NewYang)
{
	if (!HasAuthority())
	{
		return false;
	}

	const int64 ValidYang = FMath::Max<int64>(NewYang, 0);
	if (Yang == ValidYang)
	{
		return false;
	}

	const int64 OldYang = Yang;
	Yang = ValidYang;
	OnRep_Yang(OldYang);
	PersistenceComponent->MarkDirty();
	return true;
}

bool AMT2PlayerState::AddYang(int64 Amount)
{
	if (!HasAuthority() || Amount == 0)
	{
		return false;
	}

	const int64 NewYang = Amount > 0 && Yang > MAX_int64 - Amount
		? MAX_int64
		: FMath::Max<int64>(Yang + Amount, 0);
	const int64 OldYang = Yang;
	const bool bChanged = SetYang(NewYang);
	if (bChanged && Yang > OldYang)
	{
		NotifyYangReceived(Yang - OldYang);
	}
	return bChanged;
}

void AMT2PlayerState::NotifyYangReceived(int64 Amount) const
{
	if (Amount > 0)
	{
		if (AMT2PlayerController* Controller = Cast<AMT2PlayerController>(GetOwner()))
		{
			Controller->ClientNotifyYangReceived(Amount);
		}
	}
}

FString AMT2PlayerState::CapturePersistentStateJson_Implementation() const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("version"), 5);
	Root->SetStringField(TEXT("character_name"), GetCharacterName());
	Root->SetNumberField(TEXT("level"), CharacterLevel);
	Root->SetStringField(TEXT("experience"), LexToString(Experience));
	Root->SetNumberField(TEXT("experience_milestone_step"), ExperienceMilestoneStep);
	Root->SetNumberField(TEXT("unspent_skill_points"), UnspentSkillPoints);
	Root->SetNumberField(TEXT("unspent_stat_points"), UnspentStatPoints);
	Root->SetNumberField(TEXT("empire"), static_cast<uint8>(Empire));
	Root->SetNumberField(TEXT("guild_id"), GuildId);
	Root->SetStringField(TEXT("guild_name"), GuildName);
	Root->SetNumberField(TEXT("karma"), GetRawAlignment() / 10.0);
	Root->SetStringField(TEXT("yang"), LexToString(Yang));
	Root->SetBoolField(TEXT("startup_items_granted"), bStartingItemsGranted);

	TSharedRef<FJsonObject> Appearance = MakeShared<FJsonObject>();
	Appearance->SetNumberField(TEXT("race"), static_cast<uint8>(CharacterAppearance.Race));
	Appearance->SetNumberField(TEXT("sex"), static_cast<uint8>(CharacterAppearance.Sex));
	Appearance->SetNumberField(TEXT("style"), static_cast<uint8>(CharacterAppearance.Style));
	Root->SetObjectField(TEXT("appearance"), Appearance);

	const FMT2PrimaryStats& BaseStats = PrimaryStatsComponent->GetBaseStats();
	TSharedRef<FJsonObject> PrimaryStats = MakeShared<FJsonObject>();
	PrimaryStats->SetNumberField(TEXT("st"), BaseStats.Strength);
	PrimaryStats->SetNumberField(TEXT("dx"), BaseStats.Dexterity);
	PrimaryStats->SetNumberField(TEXT("ht"), BaseStats.Constitution);
	PrimaryStats->SetNumberField(TEXT("iq"), BaseStats.Intelligence);
	Root->SetObjectField(TEXT("primary_stats"), PrimaryStats);

	TSharedRef<FJsonObject> Attributes = MakeShared<FJsonObject>();
	Attributes->SetNumberField(TEXT("health"), CoreAttributes->GetHealth());
	Attributes->SetNumberField(TEXT("max_health"), CoreAttributes->GetMaxHealth());
	Attributes->SetNumberField(TEXT("mana"), CoreAttributes->GetMana());
	Attributes->SetNumberField(TEXT("max_mana"), CoreAttributes->GetMaxMana());
	Attributes->SetNumberField(TEXT("stamina"), CoreAttributes->GetStamina());
	Attributes->SetNumberField(TEXT("max_stamina"), CoreAttributes->GetMaxStamina());
	Attributes->SetNumberField(TEXT("movement_speed"), CoreAttributes->GetMovementSpeed());
	Root->SetObjectField(TEXT("attributes"), Attributes);

	TArray<TSharedPtr<FJsonValue>> SkillValues;
	for (const FMT2SkillLevelEntry& Skill : SkillComponent->GetSkillLevels())
	{
		TSharedRef<FJsonObject> SkillObject = MakeShared<FJsonObject>();
		SkillObject->SetNumberField(TEXT("vnum"), Skill.SkillVnum);
		SkillObject->SetNumberField(TEXT("level"), Skill.Level);
		// Successful skill-book reads banked toward the next mastery level (0 for most skills).
		SkillObject->SetNumberField(TEXT("read_count"), SkillComponent->GetMasteryReadCount(Skill.SkillVnum));
		SkillObject->SetNumberField(
			TEXT("grandmaster_read_count"),
			SkillComponent->GetGrandMasterReadCount(Skill.SkillVnum));
		SkillObject->SetNumberField(
			TEXT("cooldown_end"), SkillComponent->GetSkillBookCooldownEnd(Skill.SkillVnum));
		SkillValues.Add(MakeShared<FJsonValueObject>(SkillObject));
	}
	Root->SetArrayField(TEXT("skills"), SkillValues);
	Root->SetNumberField(TEXT("skill_group"), SkillComponent->GetSkillGroup());

	TArray<TSharedPtr<FJsonValue>> QuickSlotValues;
	for (const FMT2QuickSlotAssignment& Assignment : QuickSlots)
	{
		TSharedRef<FJsonObject> SlotObject = MakeShared<FJsonObject>();
		SlotObject->SetNumberField(TEXT("type"), static_cast<int32>(Assignment.Type));
		SlotObject->SetNumberField(TEXT("vnum"), Assignment.Vnum);
		QuickSlotValues.Add(MakeShared<FJsonValueObject>(SlotObject));
	}
	Root->SetArrayField(TEXT("quick_slots"), QuickSlotValues);

	// Items live on the pawn's inventory, and the pawn is destroyed BEFORE the PlayerState during
	// logout/shutdown - so the final EndPlay save can run with no pawn. In that case the "items"
	// field must be OMITTED entirely (= "no information, keep what the DB has"), never written as an
	// empty array, or the backend's rewrite would wipe every stored item. An empty array is only
	// correct when the inventory was actually readable and genuinely empty.
	TArray<TSharedPtr<FJsonValue>> ItemValues;
	bool bInventoryReadable = false;
	if (const AMT2PlayerCharacter* Player = Cast<AMT2PlayerCharacter>(GetPawn()))
	{
		if (const UMT2InventoryComponent* Inventory = Player->GetInventoryComponent())
		{
			bInventoryReadable = true;
			auto AppendItems = [this, &ItemValues](const TArray<FMT2ItemSlot>& Items, int32 Container)
			{
				for (int32 SlotIndex = 0; SlotIndex < Items.Num(); ++SlotIndex)
				{
					if (Items[SlotIndex].IsEmpty()) continue;
					TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
					Item->SetNumberField(TEXT("container"), Container);
					Item->SetNumberField(TEXT("slot"), SlotIndex);
					Item->SetNumberField(TEXT("vnum"), Items[SlotIndex].Vnum);
					Item->SetNumberField(TEXT("count"), Items[SlotIndex].Count);
					const UMT2ItemTemplate* Template = MT2ItemUtils::ResolveTemplate(this, Items[SlotIndex].Vnum);
					const bool bFishingSockets = Template && (Template->IsA<UMT2ItemRodTemplate>() || Template->IsA<UMT2ItemFishTemplate>());
					for (int32 SocketIndex = 0; SocketIndex < 3; ++SocketIndex)
					{
						int32 StoredSocket = 0;
						if (bFishingSockets)
						{
							StoredSocket = Items[SlotIndex].MetinSockets.IsValidIndex(SocketIndex) ? Items[SlotIndex].MetinSockets[SocketIndex].Value : 0;
						}
						else if (Items[SlotIndex].AutoRecoveryMaximumAmount > 0)
						{
							StoredSocket = SocketIndex == 0
								? (Items[SlotIndex].bAutoRecoveryActive ? 1 : 0)
								: SocketIndex == 1
								? Items[SlotIndex].AutoRecoveryMaximumAmount -
									Items[SlotIndex].AutoRecoveryRemainingAmount
								: Items[SlotIndex].AutoRecoveryMaximumAmount;
						}
						else if (SocketIndex == 0)
						{
							StoredSocket = Items[SlotIndex].SkillVnum;
						}
						if (!bFishingSockets && StoredSocket == 0 &&
							Items[SlotIndex].MetinSockets.IsValidIndex(SocketIndex))
						{
							const FMT2MetinSocket& Socket =
								Items[SlotIndex].MetinSockets[SocketIndex];
							const UMT2ItemMetinStoneTemplate* Stone =
								Socket.Stone.GetDefaultObject();
							StoredSocket = Stone ? Stone->Vnum
								: (Socket.Type == EMT2MetinSocketType::Gold ? 2 : 1);
							if (Stone && Socket.Type == EMT2MetinSocketType::Gold)
							{
								StoredSocket = -StoredSocket;
							}
						}
						Item->SetNumberField(
							FString::Printf(TEXT("socket_%d"), SocketIndex),
							StoredSocket);
					}
					int32 NormalIndex = 0;
					int32 RareIndex = 0;
					for (const FMT2ItemBonus& Bonus : Items[SlotIndex].Bonuses)
					{
						if (!Bonus.IsValid()) continue;
						const bool bRare = Bonus.Kind == EMT2ItemBonusKind::Rare;
						int32& BonusIndex = bRare ? RareIndex : NormalIndex;
						const int32 Limit = bRare ? 2 : 5;
						if (BonusIndex >= Limit) continue;
						const FString Prefix = bRare ? TEXT("rare_bonus") : TEXT("bonus");
						Item->SetNumberField(
							FString::Printf(TEXT("%s_type_%d"), *Prefix, BonusIndex),
							Bonus.GetTypeId());
						Item->SetNumberField(FString::Printf(TEXT("%s_value_%d"), *Prefix, BonusIndex), Bonus.Value);
						++BonusIndex;
					}
					ItemValues.Add(MakeShared<FJsonValueObject>(Item));
				}
			};
			AppendItems(Inventory->GetSlots(), 0);
			AppendItems(Inventory->GetEquipment(), 1);
		}
	}
	if (bInventoryReadable)
	{
		Root->SetArrayField(TEXT("items"), ItemValues);
	}

	const APawn* Pawn = GetPawn();
	if (Pawn)
	{
		TSharedRef<FJsonObject> WorldState = MakeShared<FJsonObject>();
		WorldState->SetStringField(TEXT("map"), GetWorld() ? GetWorld()->GetMapName() : FString());
		const FVector Location = Pawn->GetActorLocation();
		const FRotator Rotation = Pawn->GetActorRotation();
		WorldState->SetNumberField(TEXT("x"), Location.X);
		WorldState->SetNumberField(TEXT("y"), Location.Y);
		WorldState->SetNumberField(TEXT("z"), Location.Z);
		WorldState->SetNumberField(TEXT("pitch"), Rotation.Pitch);
		WorldState->SetNumberField(TEXT("yaw"), Rotation.Yaw);
		WorldState->SetNumberField(TEXT("roll"), Rotation.Roll);
		Root->SetObjectField(TEXT("world"), WorldState);
	}

	// A pending portal destination survives the transfer save so the destination map can place the
	// player there instead of the town.
	if (bHasPendingSpawnLocation)
	{
		TSharedRef<FJsonObject> PendingSpawn = MakeShared<FJsonObject>();
		PendingSpawn->SetNumberField(TEXT("x"), PendingSpawnLocation.X);
		PendingSpawn->SetNumberField(TEXT("y"), PendingSpawnLocation.Y);
		Root->SetObjectField(TEXT("pending_spawn"), PendingSpawn);
	}

	// Quest states + flags (the old quest DB tables) travel with the character.
	if (QuestManagerComponent)
	{
		QuestManagerComponent->SaveTo(Root);
	}

	// Buffs/debuffs survive logout, mirroring the old game's `affect` DB table
	// (see Docs/OldGameResearch/AffectSystem.md).
	if (const AMT2CharacterBase* Character = Cast<AMT2CharacterBase>(Pawn))
	{
		if (const UMT2StatusEffectComponent* StatusEffects = Character->GetStatusEffectComponent())
		{
			TArray<TSharedPtr<FJsonValue>> AffectValues;
			for (const FMT2StatusEffect& Effect : StatusEffects->GetEffects())
			{
				TSharedRef<FJsonObject> AffectObject = MakeShared<FJsonObject>();
				AffectObject->SetNumberField(TEXT("kind"), static_cast<int32>(Effect.Kind));
				AffectObject->SetNumberField(TEXT("type"), Effect.Type);
				AffectObject->SetNumberField(TEXT("source_item"), Effect.SourceItemVnum);
				AffectObject->SetNumberField(TEXT("apply_on"), Effect.ApplyType);
				AffectObject->SetNumberField(TEXT("value"), Effect.ApplyValue);
				AffectObject->SetNumberField(TEXT("flag"), Effect.Flags);
				AffectObject->SetNumberField(TEXT("duration"), Effect.RemainingSeconds);
				AffectObject->SetNumberField(TEXT("sp_cost"), Effect.SPCostPerTick);
				AffectObject->SetNumberField(TEXT("remove_on_death"), Effect.bRemoveOnDeath ? 1 : 0);
				AffectValues.Add(MakeShared<FJsonValueObject>(AffectObject));
			}
			Root->SetArrayField(TEXT("affects"), AffectValues);
		}
	}

	FString Payload;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Payload);
	FJsonSerializer::Serialize(Root, Writer);
	return Payload;
}

bool AMT2PlayerState::ApplyPersistentStateJson_Implementation(const FString& PayloadJson)
{
	if (!HasAuthority())
	{
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PayloadJson);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return false;
	}

	FString Name;
	if (Root->TryGetStringField(TEXT("character_name"), Name))
	{
		SetCharacterName(Name);
	}
	double Number = 0.0;
	if (Root->TryGetNumberField(TEXT("level"), Number))
	{
		SetCharacterLevel(FMath::Max(FMath::RoundToInt(Number), 1));
	}
	FString ExperienceString;
	if (Root->TryGetStringField(TEXT("experience"), ExperienceString))
	{
		SetExperience(FCString::Atoi64(*ExperienceString));
	}
	const int32 OldSkillPoints = UnspentSkillPoints;
	const int32 OldStatPoints = UnspentStatPoints;
	if (Root->TryGetNumberField(TEXT("unspent_skill_points"), Number))
	{
		UnspentSkillPoints = FMath::Max(FMath::RoundToInt(Number), 0);
	}
	if (Root->TryGetNumberField(TEXT("unspent_stat_points"), Number))
	{
		UnspentStatPoints = FMath::Max(FMath::RoundToInt(Number), 0);
	}
	if (Root->TryGetNumberField(TEXT("experience_milestone_step"), Number))
	{
		ExperienceMilestoneStep = FMath::Clamp(FMath::RoundToInt(Number), 0, 3);
	}
	else
	{
		// Legacy payload: mark already-reached quarters as consumed so lowering and regaining EXP
		// cannot manufacture points after migration.
		ExperienceMilestoneStep = 0;
		const int64 RequiredExperience = GetRequiredExperienceForNextLevel();
		for (int32 Step = 1; Step <= 3 && RequiredExperience > 0; ++Step)
		{
			if (Experience >= (RequiredExperience * Step) / 4) ExperienceMilestoneStep = Step;
		}
	}
	if (CharacterLevel >= GetMaximumCharacterLevel())
	{
		Experience = 0;
		ExperienceMilestoneStep = 0;
	}
	if (UnspentSkillPoints != OldSkillPoints) OnRep_UnspentSkillPoints(OldSkillPoints);
	if (UnspentStatPoints != OldStatPoints) OnRep_UnspentStatPoints(OldStatPoints);
	if (Root->TryGetNumberField(TEXT("empire"), Number))
	{
		SetEmpire(static_cast<EMT2Empire>(FMath::Clamp(FMath::RoundToInt(Number), 0, 3)));
	}
	FString PersistentGuildName;
	Root->TryGetStringField(TEXT("guild_name"), PersistentGuildName);
	if (Root->TryGetNumberField(TEXT("guild_id"), Number))
	{
		SetGuild(FMath::Max(FMath::RoundToInt(Number), 0), PersistentGuildName);
	}
	if (Root->TryGetNumberField(TEXT("karma"), Number))
	{
		if (FMath::IsFinite(Number))
		{
			SetRawAlignment(FMath::RoundToInt(FMath::Clamp(Number,
				double(MT2KarmaLimits::Minimum), double(MT2KarmaLimits::Maximum)) * 10.0));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("Cannot restore non-finite player karma"));
			return false;
		}
	}
	FString YangString;
	if (Root->TryGetStringField(TEXT("yang"), YangString))
	{
		SetYang(FCString::Atoi64(*YangString));
	}
	else if (Root->TryGetNumberField(TEXT("yang"), Number))
	{
		SetYang(FMath::Max<int64>(static_cast<int64>(Number), 0));
	}
	Root->TryGetBoolField(TEXT("startup_items_granted"), bStartingItemsGranted);

	const TSharedPtr<FJsonObject>* AppearanceObject = nullptr;
	if (Root->TryGetObjectField(TEXT("appearance"), AppearanceObject) && AppearanceObject && AppearanceObject->IsValid())
	{
		FMT2CharacterAppearance Appearance = CharacterAppearance;
		if ((*AppearanceObject)->TryGetNumberField(TEXT("race"), Number))
		{
			Appearance.Race = static_cast<EMT2CharacterRace>(FMath::Clamp(FMath::RoundToInt(Number), 0, 3));
		}
		if ((*AppearanceObject)->TryGetNumberField(TEXT("sex"), Number))
		{
			Appearance.Sex = static_cast<EMT2CharacterSex>(FMath::Clamp(FMath::RoundToInt(Number), 0, 1));
		}
		if ((*AppearanceObject)->TryGetNumberField(TEXT("style"), Number))
		{
			Appearance.Style = static_cast<EMT2CharacterStyle>(FMath::Clamp(FMath::RoundToInt(Number), 0, 1));
		}
		SetCharacterAppearance(Appearance);
	}

	const TSharedPtr<FJsonObject>* PrimaryStatsObject = nullptr;
	if (Root->TryGetObjectField(TEXT("primary_stats"), PrimaryStatsObject) &&
		PrimaryStatsObject && PrimaryStatsObject->IsValid())
	{
		FMT2PrimaryStats Stats = PrimaryStatsComponent->GetBaseStats();
		if ((*PrimaryStatsObject)->TryGetNumberField(TEXT("st"), Number))
		{
			Stats.Strength = FMath::Max(FMath::RoundToInt(Number), 0);
		}
		if ((*PrimaryStatsObject)->TryGetNumberField(TEXT("dx"), Number))
		{
			Stats.Dexterity = FMath::Max(FMath::RoundToInt(Number), 0);
		}
		if ((*PrimaryStatsObject)->TryGetNumberField(TEXT("ht"), Number))
		{
			Stats.Constitution = FMath::Max(FMath::RoundToInt(Number), 0);
		}
		if ((*PrimaryStatsObject)->TryGetNumberField(TEXT("iq"), Number))
		{
			Stats.Intelligence = FMath::Max(FMath::RoundToInt(Number), 0);
		}
		if (HasNoPrimaryStats(Stats))
		{
			Stats = MT2PlayerStatFormula::GetInitialPrimaryStats(CharacterAppearance.Race);
		}
		PrimaryStatsComponent->SetBaseStats(Stats);
	}
	else if (HasNoPrimaryStats(PrimaryStatsComponent->GetBaseStats()))
	{
		PrimaryStatsComponent->SetBaseStats(
			MT2PlayerStatFormula::GetInitialPrimaryStats(CharacterAppearance.Race));
	}

	const TSharedPtr<FJsonObject>* AttributeObject = nullptr;
	if (Root->TryGetObjectField(TEXT("attributes"), AttributeObject) && AttributeObject && AttributeObject->IsValid())
	{
		auto RestoreAttribute = [this, &AttributeObject](const TCHAR* Name, const FGameplayAttribute& Attribute)
		{
			double Value = 0.0;
			if ((*AttributeObject)->TryGetNumberField(Name, Value))
			{
				AbilitySystemComponent->SetNumericAttributeBase(Attribute, static_cast<float>(Value));
			}
		};
		RestoreAttribute(TEXT("max_health"), UMT2CoreAttributeSet::GetMaxHealthAttribute());
		RestoreAttribute(TEXT("max_mana"), UMT2CoreAttributeSet::GetMaxManaAttribute());
		RestoreAttribute(TEXT("max_stamina"), UMT2CoreAttributeSet::GetMaxStaminaAttribute());
		RestoreAttribute(TEXT("health"), UMT2CoreAttributeSet::GetHealthAttribute());
		RestoreAttribute(TEXT("mana"), UMT2CoreAttributeSet::GetManaAttribute());
		RestoreAttribute(TEXT("stamina"), UMT2CoreAttributeSet::GetStaminaAttribute());
		RestoreAttribute(TEXT("movement_speed"), UMT2CoreAttributeSet::GetMovementSpeedAttribute());
	}

	double SavedSkillGroup = 0.0;
	if (Root->TryGetNumberField(TEXT("skill_group"), SavedSkillGroup))
	{
		SkillComponent->RestorePersistedSkillGroup(FMath::RoundToInt(SavedSkillGroup));
	}

	const TArray<TSharedPtr<FJsonValue>>* Skills = nullptr;
	if (Root->TryGetArrayField(TEXT("skills"), Skills) && Skills)
	{
		for (const TSharedPtr<FJsonValue>& SkillValue : *Skills)
		{
			const TSharedPtr<FJsonObject> SkillObject = SkillValue.IsValid() ? SkillValue->AsObject() : nullptr;
			if (!SkillObject.IsValid())
			{
				continue;
			}
			double SkillVnum = 0.0;
			double SkillLevel = 0.0;
			if (SkillObject->TryGetNumberField(TEXT("vnum"), SkillVnum) &&
				SkillObject->TryGetNumberField(TEXT("level"), SkillLevel))
			{
				SkillComponent->SetSkillLevel(
					FMath::RoundToInt(SkillVnum), FMath::RoundToInt(SkillLevel));
				double ReadCount = 0.0;
				if (SkillObject->TryGetNumberField(TEXT("read_count"), ReadCount))
				{
					SkillComponent->RestoreMasteryReadCount(
						FMath::RoundToInt(SkillVnum), FMath::RoundToInt(ReadCount));
				}
				double GrandMasterReadCount = 0.0;
				if (SkillObject->TryGetNumberField(
					TEXT("grandmaster_read_count"), GrandMasterReadCount))
				{
					SkillComponent->RestoreGrandMasterReadCount(
						FMath::RoundToInt(SkillVnum),
						FMath::RoundToInt(GrandMasterReadCount));
				}
				double CooldownEnd = 0.0;
				if (SkillObject->TryGetNumberField(TEXT("cooldown_end"), CooldownEnd))
				{
					SkillComponent->RestoreSkillBookCooldown(
						FMath::RoundToInt(SkillVnum), FMath::RoundToInt64(CooldownEnd));
				}
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* QuickSlotArray = nullptr;
	if (Root->TryGetArrayField(TEXT("quick_slots"), QuickSlotArray) && QuickSlotArray)
	{
		for (int32 ArrayIndex = 0; ArrayIndex < QuickSlotArray->Num(); ++ArrayIndex)
		{
			const TSharedPtr<FJsonObject> SlotObject = (*QuickSlotArray)[ArrayIndex].IsValid()
				? (*QuickSlotArray)[ArrayIndex]->AsObject() : nullptr;
			double SlotIndex = ArrayIndex, SlotType = 0.0, SlotVnum = 0.0;
			if (SlotObject.IsValid() &&
				(!SlotObject->HasField(TEXT("index")) || SlotObject->TryGetNumberField(TEXT("index"), SlotIndex)) &&
				SlotObject->TryGetNumberField(TEXT("type"), SlotType) &&
				SlotObject->TryGetNumberField(TEXT("vnum"), SlotVnum))
			{
				const int32 Index = FMath::RoundToInt(SlotIndex);
				if (!QuickSlots.IsValidIndex(Index)) continue;
				QuickSlots[Index].Type = static_cast<EMT2QuickSlotType>(
					FMath::Clamp(FMath::RoundToInt(SlotType), 0, 2));
				QuickSlots[Index].Vnum = FMath::RoundToInt(SlotVnum);
			}
		}
		OnRep_QuickSlots();
	}

	const TSharedPtr<FJsonObject>* WorldObject = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (Root->TryGetArrayField(TEXT("items"), Items) && Items)
	{
		PendingInventorySlots.SetNum(UMT2InventoryComponent::SlotCount);
		PendingEquipmentSlots.SetNum(UMT2InventoryComponent::EquipmentCount);
		for (const TSharedPtr<FJsonValue>& ItemValue : *Items)
		{
			const TSharedPtr<FJsonObject> Item = ItemValue.IsValid() ? ItemValue->AsObject() : nullptr;
			if (!Item.IsValid()) continue;
			auto ReadInteger = [&Item](const TCHAR* Field, int32 Default = 0)
			{
				double Value = Default;
				return Item->TryGetNumberField(Field, Value) ? FMath::RoundToInt(Value) : Default;
			};
			const int32 Container = ReadInteger(TEXT("container"));
			const int32 SlotIndex = ReadInteger(TEXT("slot"), INDEX_NONE);
			FMT2ItemSlot Slot;
			Slot.Vnum = ReadInteger(TEXT("vnum"));
			Slot.Count = ReadInteger(TEXT("count"));
			UMT2VnumRegistrySubsystem* Registry = GetGameInstance()
				? GetGameInstance()->GetSubsystem<UMT2VnumRegistrySubsystem>() : nullptr;
			const TSubclassOf<UMT2ItemTemplate> ItemTemplateClass = Registry
				? Registry->ResolveItemTemplateClass(Slot.Vnum) : nullptr;
			const UMT2ItemTemplate* ItemTemplate = ItemTemplateClass.GetDefaultObject();
			if (Cast<UMT2ItemSkillBookTemplate>(ItemTemplate))
			{
				Slot.SkillVnum = ReadInteger(TEXT("socket_0"));
				ItemTemplate->InitializeGeneratedInstance(Slot);
			}
			else if (const UMT2ItemAutoRecoveryTemplate* AutoRecovery =
				Cast<UMT2ItemAutoRecoveryTemplate>(ItemTemplate))
			{
				Slot.AutoRecoveryMaximumAmount = ReadInteger(TEXT("socket_2"), AutoRecovery->RecoveryCapacity);
				if (Slot.AutoRecoveryMaximumAmount <= 0)
				{
					Slot.AutoRecoveryMaximumAmount = AutoRecovery->RecoveryCapacity;
				}
				Slot.AutoRecoveryRemainingAmount = FMath::Clamp(
					Slot.AutoRecoveryMaximumAmount - ReadInteger(TEXT("socket_1")),
					0, Slot.AutoRecoveryMaximumAmount);
				Slot.bAutoRecoveryActive = ReadInteger(TEXT("socket_0")) != 0 &&
					Slot.AutoRecoveryRemainingAmount > 0;
			}
			else if (ItemTemplate && (ItemTemplate->IsA<UMT2ItemRodTemplate>() || ItemTemplate->IsA<UMT2ItemFishTemplate>()))
			{
				Slot.MetinSockets.SetNum(3);
				for (int32 Index = 0; Index < 3; ++Index) { Slot.MetinSockets[Index].Value = ReadInteger(*FString::Printf(TEXT("socket_%d"), Index)); }
			}
			else for (int32 SocketIndex = 0; SocketIndex < 3; ++SocketIndex)
			{
				const int32 Stored =
					ReadInteger(*FString::Printf(TEXT("socket_%d"), SocketIndex));
				if (Stored == 0)
				{
					continue;
				}
				FMT2MetinSocket& Socket = Slot.MetinSockets.AddDefaulted_GetRef();
				Socket.Type = (Stored < 0 || Stored == 2)
					? EMT2MetinSocketType::Gold : EMT2MetinSocketType::Silver;
				const int32 StoneVnum = FMath::Abs(Stored);
				if (StoneVnum > 2 && Registry)
				{
					const TSubclassOf<UMT2ItemTemplate> TemplateClass =
						Registry->ResolveItemTemplateClass(StoneVnum);
					if (TemplateClass && TemplateClass->IsChildOf(
						UMT2ItemMetinStoneTemplate::StaticClass()))
					{
						Socket.Stone =
							TSubclassOf<UMT2ItemMetinStoneTemplate>(TemplateClass.Get());
						const UMT2ItemMetinStoneTemplate* Stone =
							Socket.Stone.GetDefaultObject();
						if (Stored > 0 && Stone && Stone->MetinSubType == 1)
						{
							Socket.Type = EMT2MetinSocketType::Gold;
						}
					}
				}
			}
			for (int32 BonusIndex = 0; BonusIndex < 5; ++BonusIndex)
			{
				FMT2ItemBonus Bonus;
				Bonus.Type = static_cast<EMT2ItemBonusType>(FMath::Clamp(
					ReadInteger(*FString::Printf(TEXT("bonus_type_%d"), BonusIndex)), 0, 91));
				Bonus.Value = ReadInteger(*FString::Printf(TEXT("bonus_value_%d"), BonusIndex));
				if (Bonus.IsValid()) Slot.Bonuses.Add(Bonus);
			}
			for (int32 BonusIndex = 0; BonusIndex < 2; ++BonusIndex)
			{
				FMT2ItemBonus Bonus;
				Bonus.Type = static_cast<EMT2ItemBonusType>(FMath::Clamp(
					ReadInteger(*FString::Printf(TEXT("rare_bonus_type_%d"), BonusIndex)), 0, 91));
				Bonus.Value = ReadInteger(*FString::Printf(TEXT("rare_bonus_value_%d"), BonusIndex));
				Bonus.Kind = EMT2ItemBonusKind::Rare;
				if (Bonus.IsValid()) Slot.Bonuses.Add(Bonus);
			}
			if (Container == 0 && PendingInventorySlots.IsValidIndex(SlotIndex)) PendingInventorySlots[SlotIndex] = Slot;
			else if (Container == 1 && PendingEquipmentSlots.IsValidIndex(SlotIndex)) PendingEquipmentSlots[SlotIndex] = Slot;
		}
		bHasPendingInventory = true;
	}

	if (Root->TryGetObjectField(TEXT("world"), WorldObject) && WorldObject && WorldObject->IsValid())
	{
		double X = 0.0, Y = 0.0, Z = 0.0, Pitch = 0.0, Yaw = 0.0, Roll = 0.0;
		(*WorldObject)->TryGetNumberField(TEXT("x"), X);
		(*WorldObject)->TryGetNumberField(TEXT("y"), Y);
		(*WorldObject)->TryGetNumberField(TEXT("z"), Z);
		(*WorldObject)->TryGetNumberField(TEXT("pitch"), Pitch);
		(*WorldObject)->TryGetNumberField(TEXT("yaw"), Yaw);
		(*WorldObject)->TryGetNumberField(TEXT("roll"), Roll);
		PendingPersistentPawnTransform = FTransform(
			FRotator(Pitch, Yaw, Roll), FVector(X, Y, Z), FVector::OneVector);
		bHasPendingPersistentPawnTransform = true;
		if (APawn* Pawn = GetPawn())
		{
			Pawn->SetActorTransform(PendingPersistentPawnTransform, false, nullptr, ETeleportType::TeleportPhysics);
			bHasPendingPersistentPawnTransform = false;
		}
	}

	if (QuestManagerComponent)
	{
		QuestManagerComponent->LoadFrom(Root.ToSharedRef());
		SkillComponent->MigrateLegacyGrandMasterReadCounts();
	}

	// A portal destination stashed before a transfer; the game mode ground-snaps the player here on arrival.
	const TSharedPtr<FJsonObject>* PendingSpawnObject = nullptr;
	if (Root->TryGetObjectField(TEXT("pending_spawn"), PendingSpawnObject) && PendingSpawnObject && PendingSpawnObject->IsValid())
	{
		double PendingX = 0.0, PendingY = 0.0;
		(*PendingSpawnObject)->TryGetNumberField(TEXT("x"), PendingX);
		(*PendingSpawnObject)->TryGetNumberField(TEXT("y"), PendingY);
		PendingSpawnLocation = FVector2D(PendingX, PendingY);
		bHasPendingSpawnLocation = true;
	}

	const TArray<TSharedPtr<FJsonValue>>* Affects = nullptr;
	if (Root->TryGetArrayField(TEXT("affects"), Affects) && Affects)
	{
		PendingRestoredStatusEffects.Reset();
		for (const TSharedPtr<FJsonValue>& AffectValue : *Affects)
		{
			const TSharedPtr<FJsonObject> AffectObject = AffectValue.IsValid() ? AffectValue->AsObject() : nullptr;
			if (!AffectObject.IsValid())
			{
				continue;
			}
			FMT2StatusEffect Effect;
			double Value = 0.0;
			if (AffectObject->TryGetNumberField(TEXT("kind"), Value)) Effect.Kind = static_cast<EMT2StatusEffectKind>(FMath::RoundToInt(Value));
			if (AffectObject->TryGetNumberField(TEXT("type"), Value)) Effect.Type = FMath::RoundToInt(Value);
			if (AffectObject->TryGetNumberField(TEXT("source_item"), Value)) Effect.SourceItemVnum = FMath::RoundToInt(Value);
			if (AffectObject->TryGetNumberField(TEXT("apply_on"), Value)) Effect.ApplyType = FMath::RoundToInt(Value);
			if (AffectObject->TryGetNumberField(TEXT("value"), Value)) Effect.ApplyValue = FMath::RoundToInt(Value);
			if (AffectObject->TryGetNumberField(TEXT("flag"), Value)) Effect.Flags = FMath::RoundToInt(Value);
			if (AffectObject->TryGetNumberField(TEXT("duration"), Value)) Effect.RemainingSeconds = FMath::RoundToInt(Value);
			if (AffectObject->TryGetNumberField(TEXT("sp_cost"), Value)) Effect.SPCostPerTick = FMath::RoundToInt(Value);
			if (!AffectObject->TryGetBoolField(TEXT("remove_on_death"), Effect.bRemoveOnDeath) &&
				AffectObject->TryGetNumberField(TEXT("remove_on_death"), Value))
			{
				Effect.bRemoveOnDeath = !FMath::IsNearlyZero(Value);
			}
			PendingRestoredStatusEffects.Add(Effect);
		}
		// Stage even when empty: the pawn may not exist yet (it consumes these on initialize), and an
		// empty restore must still clear any stale effects.
		bHasPendingRestoredStatusEffects = true;
		if (AMT2CharacterBase* Character = Cast<AMT2CharacterBase>(GetPawn()))
		{
			if (UMT2StatusEffectComponent* StatusEffects = Character->GetStatusEffectComponent())
			{
				StatusEffects->RestoreEffects(PendingRestoredStatusEffects);
				bHasPendingRestoredStatusEffects = false;
			}
		}
	}
	return true;
}

bool AMT2PlayerState::ConsumePendingStatusEffects(TArray<FMT2StatusEffect>& OutEffects)
{
	if (!bHasPendingRestoredStatusEffects)
	{
		return false;
	}
	OutEffects = PendingRestoredStatusEffects;
	PendingRestoredStatusEffects.Reset();
	bHasPendingRestoredStatusEffects = false;
	return true;
}

bool AMT2PlayerState::ConsumePendingInventory(
	TArray<FMT2ItemSlot>& OutSlots, TArray<FMT2ItemSlot>& OutEquipment)
{
	if (!bHasPendingInventory) return false;
	OutSlots = MoveTemp(PendingInventorySlots);
	OutEquipment = MoveTemp(PendingEquipmentSlots);
	bHasPendingInventory = false;
	return true;
}

bool AMT2PlayerState::GrantStartingItemsIfNeeded(AMT2PlayerCharacter* Character)
{
	if (!HasAuthority() || bStartingItemsGranted || !Character)
	{
		return false;
	}

	UMT2InventoryComponent* Inventory = Character->GetInventoryComponent();
	if (!Inventory)
	{
		return false;
	}

	const UMT2GameplaySettings& Settings = UMT2GameplaySettings::Get();
	const FString OwnerId = AMT2WorldItem::ResolvePlayerIdentity(this);
	for (const FMT2StartingItem& StartingItem : Settings.StartingItems)
	{
		const UMT2ItemTemplate* Template = StartingItem.Template.GetDefaultObject();
		if (!Template || Template->Vnum <= 0 || StartingItem.InstanceData.Count <= 0)
		{
			continue;
		}

		FMT2ItemSlot Item(Template->Vnum, StartingItem.InstanceData);

		const int32 Added = Inventory->AddItemSlotPartial(Item);
		Item.Count -= Added;
		if (Item.Count > 0)
		{
			AMT2WorldItem::SpawnWorldItemInstance(
				GetWorld(), Character->GetActorLocation(), Item, Character, OwnerId,
				GetCharacterName(), Settings.LootCrateOverflowOwnershipSeconds);
		}
	}

	bStartingItemsGranted = true;
	// PIE uses temporary player identities and often has no coordinator persistence connection.
	// Keep the one-time guard on this PlayerState, but never create/save a fake PIE player record.
	if (!GetWorld() || GetWorld()->WorldType != EWorldType::PIE)
	{
		PersistenceComponent->MarkDirty();
		PersistenceComponent->RequestSave(true);
	}
	return true;
}

bool AMT2PlayerState::HasStatusEffect(FGameplayTag StatusTag) const
{
	return StatusTag.IsValid() && StatusTag.MatchesTag(MT2GameplayTags::Status) &&
		AbilitySystemComponent->HasMatchingGameplayTag(StatusTag);
}

FGameplayTagContainer AMT2PlayerState::GetStatusEffects() const
{
	FGameplayTagContainer OwnedTags;
	FGameplayTagContainer StatusTags;
	AbilitySystemComponent->GetOwnedGameplayTags(OwnedTags);

	for (const FGameplayTag& Tag : OwnedTags)
	{
		if (Tag.MatchesTag(MT2GameplayTags::Status))
		{
			StatusTags.AddTag(Tag);
		}
	}

	return StatusTags;
}

void AMT2PlayerState::OnRep_PlayerName()
{
	if (HasAuthority()) ForceNetUpdate();
	Super::OnRep_PlayerName();
	OnCharacterNameChanged.Broadcast(GetPlayerName());
}

void AMT2PlayerState::OnRep_CharacterLevel(int32 OldLevel)
{
	if (HasAuthority()) ForceNetUpdate();
	OnLevelChanged.Broadcast(OldLevel, CharacterLevel);
	// Quest gates are commonly `when levelup with pc.level >= N`, so tell the quest system (server side;
	// OnRep also runs here on a listen server, hence the authority check).
	if (HasAuthority() && QuestManagerComponent && CharacterLevel > OldLevel)
	{
		QuestManagerComponent->DispatchEvent(EMT2QuestEvent::LevelUp);
		QuestManagerComponent->RefreshQuestJournal();
	}
}

void AMT2PlayerState::OnRep_Experience(int64 OldExperience)
{
	if (HasAuthority()) ForceNetUpdate();
	OnExperienceChanged.Broadcast(OldExperience, Experience);
}

void AMT2PlayerState::OnRep_UnspentSkillPoints(int32 OldPoints)
{
	if (HasAuthority()) ForceNetUpdate();
	OnSkillPointsChanged.Broadcast(OldPoints, UnspentSkillPoints);
}

void AMT2PlayerState::OnRep_UnspentStatPoints(int32 OldPoints)
{
	if (HasAuthority()) ForceNetUpdate();
	OnStatPointsChanged.Broadcast(OldPoints, UnspentStatPoints);
}

void AMT2PlayerState::OnRep_CharacterAppearance(const FMT2CharacterAppearance& OldAppearance)
{
	if (HasAuthority()) ForceNetUpdate();
	OnCharacterAppearanceChanged.Broadcast(CharacterAppearance);
}

void AMT2PlayerState::OnRep_Empire(EMT2Empire OldEmpire)
{
	if (HasAuthority()) ForceNetUpdate();
	OnEmpireChanged.Broadcast(Empire);
}

void AMT2PlayerState::OnRep_Guild()
{
	if (HasAuthority()) ForceNetUpdate();
	OnGuildChanged.Broadcast(GuildId, GuildName);
}

void AMT2PlayerState::OnRep_KarmaPoints()
{
	if (HasAuthority()) ForceNetUpdate();
	OnKarmaChanged.Broadcast(GetKarmaPoints());
}

void AMT2PlayerState::OnRep_Yang(int64 OldYang)
{
	if (HasAuthority()) ForceNetUpdate();
	OnYangChanged.Broadcast(OldYang, Yang);
}

void AMT2PlayerState::HandleSkillLevelsChanged()
{
	if (HasAuthority()) ForceNetUpdate();
	if (PersistenceComponent)
	{
		PersistenceComponent->MarkDirty();
	}
}

void AMT2PlayerState::HandlePrimaryStatsChanged(
	FMT2PrimaryStats OldStats, FMT2PrimaryStats NewStats)
{
	if (HasAuthority()) ForceNetUpdate();
	if (PersistenceComponent)
	{
		PersistenceComponent->MarkDirty();
	}
}

void AMT2PlayerState::HandlePersistentAttributeChanged(const FOnAttributeChangeData& ChangeData)
{
	if (HasAuthority()) ForceNetUpdate();
	if (PersistenceComponent)
	{
		PersistenceComponent->MarkDirty();
	}
}
