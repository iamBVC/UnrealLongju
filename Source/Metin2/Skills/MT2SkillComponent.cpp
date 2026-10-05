/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Skills/MT2SkillComponent.h"

#include "Characters/MT2CharacterBase.h"
#include "Components/MT2StatusEffectComponent.h"
#include "Components/MT2StatusEffectDefinition.h"
#include "Config/MT2GameplaySettings.h"
#include "Misc/DateTime.h"
#include "Net/UnrealNetwork.h"
#include "Persistence/MT2PersistenceComponent.h"
#include "Player/MT2PlayerState.h"
#include "Skills/MT2SkillDefinition.h"
#include "Skills/MT2SkillSet.h"
#include "Quests/MT2QuestManagerComponent.h"

UMT2SkillComponent::UMT2SkillComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	SkillLevels.SetOwner(this);
}

void UMT2SkillComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UMT2SkillComponent, SkillLevels, COND_OwnerOnly);
	DOREPLIFETIME(UMT2SkillComponent, SkillGroup);
}

EMT2CharacterRace UMT2SkillComponent::GetOwnerRace() const
{
	const AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner());
	return State ? State->GetCharacterAppearance().Race : EMT2CharacterRace::Warrior;
}

int32 UMT2SkillComponent::GetOwnerCharacterLevel() const
{
	const AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner());
	return State ? State->GetCharacterLevel() : 0;
}

bool UMT2SkillComponent::HasBookReadingEffect(int32 AffectType) const
{
	const AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner());
	const AMT2CharacterBase* Character = State ? Cast<AMT2CharacterBase>(State->GetPawn()) : nullptr;
	const UMT2StatusEffectComponent* Effects = Character ? Character->GetStatusEffectComponent() : nullptr;
	return Effects && Effects->HasEffectType(AffectType);
}

void UMT2SkillComponent::ConsumeBookReadingEffect(int32 AffectType)
{
	AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner());
	AMT2CharacterBase* Character = State ? Cast<AMT2CharacterBase>(State->GetPawn()) : nullptr;
	if (UMT2StatusEffectComponent* Effects = Character ? Character->GetStatusEffectComponent() : nullptr)
	{
		Effects->RemoveEffectsByType(AffectType);
	}
}

void UMT2SkillComponent::MarkPersistenceDirty() const
{
	if (AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner()))
	{
		if (UMT2PersistenceComponent* Persistence = State->GetPersistenceComponent())
		{
			Persistence->MarkDirty();
		}
	}
}

int32 UMT2SkillComponent::GetSkillLevel(int32 SkillVnum) const
{
	return SkillLevels.GetSkillLevel(SkillVnum);
}

EMT2SkillMastery UMT2SkillComponent::GetSkillMastery(int32 SkillVnum) const
{
	return MT2SkillMastery::FromLevel(GetSkillLevel(SkillVnum));
}

TArray<FMT2SkillLevelEntry> UMT2SkillComponent::GetSkillLevels() const
{
	return SkillLevels.GetEntries();
}

UMT2SkillSet* UMT2SkillComponent::GetSkillSet() const
{
	if (SkillGroup <= 0)
	{
		return nullptr;
	}
	const EMT2CharacterRace Race = GetOwnerRace();
	if (!CachedSkillSet || CachedSkillSetGroup != SkillGroup || CachedSkillSetRace != Race)
	{
		CachedSkillSet = UMT2SkillSet::FindSkillSet(Race, SkillGroup);
		CachedSkillSetGroup = SkillGroup;
		CachedSkillSetRace = Race;
	}
	return CachedSkillSet;
}

UMT2SkillDefinition* UMT2SkillComponent::FindSkillDefinition(int32 SkillVnum) const
{
	const UMT2SkillSet* SkillSet = GetSkillSet();
	return SkillSet ? SkillSet->FindSkill(SkillVnum) : nullptr;
}

bool UMT2SkillComponent::SelectSkillGroup(int32 NewSkillGroup)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}
	// Old SetSkillGroup: only groups 1/2, only once, only from character level 5.
	if (NewSkillGroup < 1 || NewSkillGroup > 2 || SkillGroup != 0 ||
		GetOwnerCharacterLevel() < MT2SkillLimits::SkillGroupSelectLevel)
	{
		return false;
	}

	SkillGroup = NewSkillGroup;
	OnRep_SkillGroup();
	if (AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner()))
	{
		State->ForceNetUpdate();
	}
	NotifySkillLevelsChanged();
	return true;
}

bool UMT2SkillComponent::CanLearnSkillByPoint(int32 SkillVnum) const
{
	const UMT2SkillDefinition* Skill = FindSkillDefinition(SkillVnum);
	if (!Skill || SkillGroup == 0)
	{
		return false;
	}
	// Only the job's combat skills take points; support/horse skills level through books and
	// quests only (the old client never showed the + button outside the active page).
	if (Skill->SkillType != EMT2SkillType::Active)
	{
		return false;
	}

	const int32 CurrentLevel = GetSkillLevel(SkillVnum);
	// Point-ups only while Normal mastery (Master+ requires books/quests), below the proto cap,
	// past the character level limit, with the prerequisite satisfied - IsLearnableSkill +
	// SkillLevelUp(BY_POINT) gates combined.
	if (MT2SkillMastery::FromLevel(CurrentLevel) != EMT2SkillMastery::Normal ||
		CurrentLevel >= Skill->MaxLevel ||
		Skill->HasSkillFlag(EMT2SkillFlag::DisableByPointUp) ||
		GetOwnerCharacterLevel() < Skill->LevelLimit)
	{
		return false;
	}
	if (Skill->PrerequisiteSkillVnum > 0 &&
		GetSkillLevel(Skill->PrerequisiteSkillVnum) < Skill->PrerequisiteSkillLevel &&
		GetSkillMastery(Skill->PrerequisiteSkillVnum) == EMT2SkillMastery::Normal)
	{
		return false;
	}

	const AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner());
	return State && State->GetUnspentSkillPoints() > 0;
}

bool UMT2SkillComponent::LearnSkillByPoint(int32 SkillVnum)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !CanLearnSkillByPoint(SkillVnum))
	{
		return false;
	}

	AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner());
	if (!State || !State->SpendSkillPoint())
	{
		return false;
	}

	int32 NewLevel = GetSkillLevel(SkillVnum) + 1;

	// The authentic Master breakthrough: from level 17 each point-up rolls 1/(21-level) to jump
	// straight to 20 (guaranteed at 20). Book/quest progression past Master comes later.
	if (NewLevel >= 17 && FMath::RandRange(1, 21 - FMath::Min(20, NewLevel)) == 1)
	{
		NewLevel = 20;
	}

	SkillLevels.SetSkillLevel(SkillVnum, NewLevel);
	NotifySkillLevelsChanged();
	return true;
}

bool UMT2SkillComponent::CanReadSkillBook(int32 SkillVnum) const
{
	if (!FindSkillDefinition(SkillVnum))
	{
		return false;
	}
	// Books only carry a skill through the Master grades (M1..M10 -> G1); G1+ needs the grand-master path.
	if (GetSkillMastery(SkillVnum) != EMT2SkillMastery::Master)
	{
		return false;
	}
	const AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner());
	const int64 Cost = FMath::Max<int64>(UMT2GameplaySettings::Get().SkillBookExperienceCost, 0);
	const bool bCooldownReady = GetSkillBookCooldownRemaining(SkillVnum) <= 0 ||
		HasBookReadingEffect(MT2AffectId::SkillBookNoCooldown);
	return State && State->GetExperience() >= Cost && bCooldownReady;
}

FText UMT2SkillComponent::GetSkillBookDenyReason(int32 SkillVnum) const
{
	if (SkillVnum <= 0)
	{
		return NSLOCTEXT("MT2SkillBook", "BadBook", "This book is not bound to a skill.");
	}

	// In the player's own class set: the only remaining gates are mastery grade and exp.
	if (FindSkillDefinition(SkillVnum))
	{
		const EMT2SkillMastery Mastery = GetSkillMastery(SkillVnum);
		if (Mastery == EMT2SkillMastery::Normal)
		{
			return NSLOCTEXT("MT2SkillBook", "NotMaster",
				"This skill must reach Master (M1) before a book can raise it.");
		}
		if (Mastery >= EMT2SkillMastery::GrandMaster)
		{
			return NSLOCTEXT("MT2SkillBook", "AlreadyGM",
				"This skill is already Grand Master; books no longer apply.");
		}
		const AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner());
		const int64 Cost = FMath::Max<int64>(UMT2GameplaySettings::Get().SkillBookExperienceCost, 0);
		if (!State || State->GetExperience() < Cost)
		{
			return NSLOCTEXT("MT2SkillBook", "NoExp", "You need more experience to read a skill book.");
		}
		const int64 Remaining = GetSkillBookCooldownRemaining(SkillVnum);
		if (Remaining > 0 && !HasBookReadingEffect(MT2AffectId::SkillBookNoCooldown))
		{
			const int64 TotalMinutes = (Remaining + 59) / 60;
			const int64 Hours = TotalMinutes / 60;
			const int64 Minutes = TotalMinutes % 60;
			return FText::Format(
				NSLOCTEXT("MT2SkillBook", "Cooldown",
					"You must wait {0}h {1}m before reading another book for this skill."),
				FText::AsNumber(Hours), FText::AsNumber(Minutes));
		}
		return FText::GetEmpty(); // eligible
	}

	// Not in the player's set: another class of the same race, or another race entirely?
	const EMT2CharacterRace MyRace = GetOwnerRace();
	for (int32 Group = 1; Group <= 2; ++Group)
	{
		const UMT2SkillSet* Set = UMT2SkillSet::FindSkillSet(MyRace, Group);
		if (Set && Set->ContainsSkill(SkillVnum))
		{
			return NSLOCTEXT("MT2SkillBook", "OtherClass", "This book is for another class.");
		}
	}
	for (int32 RaceIndex = 0; RaceIndex <= static_cast<int32>(EMT2CharacterRace::Shaman); ++RaceIndex)
	{
		const EMT2CharacterRace Race = static_cast<EMT2CharacterRace>(RaceIndex);
		if (Race == MyRace)
		{
			continue;
		}
		for (int32 Group = 1; Group <= 2; ++Group)
		{
			const UMT2SkillSet* Set = UMT2SkillSet::FindSkillSet(Race, Group);
			if (Set && Set->ContainsSkill(SkillVnum))
			{
				return NSLOCTEXT("MT2SkillBook", "OtherRace", "This book is for another race.");
			}
		}
	}
	return NSLOCTEXT("MT2SkillBook", "Unlearnable", "You cannot learn this skill.");
}

EMT2SkillBookResult UMT2SkillComponent::LearnSkillByBook(int32 SkillVnum, int32& OutReadsRemaining)
{
	OutReadsRemaining = 0;
	if (!GetOwner() || !GetOwner()->HasAuthority() || !CanReadSkillBook(SkillVnum))
	{
		return EMT2SkillBookResult::Denied;
	}
	AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner());

	// Every read spends the configured flat EXP cost, whether it succeeds or not.
	const int64 Cost = FMath::Max<int64>(UMT2GameplaySettings::Get().SkillBookExperienceCost, 0);
	State->AddExperience(-Cost);

	const bool bGuaranteedSuccess =
		HasBookReadingEffect(MT2AffectId::SkillBookGuaranteedSuccess);
	// Both one-shot items expire on the next accepted book read. The no-cooldown affect is consumed
	// even when this skill happened to be ready, matching the requested "next book" semantics.
	ConsumeBookReadingEffect(MT2AffectId::SkillBookNoCooldown);
	ConsumeBookReadingEffect(MT2AffectId::SkillBookGuaranteedSuccess);

	const int64 Now = FDateTime::UtcNow().ToUnixTimestamp();
	const int64 Cooldown = FMath::Max(UMT2GameplaySettings::Get().SkillBookCooldownSeconds, 0);
	SkillBookCooldownEnds.Add(SkillVnum, Now + Cooldown);

	const int32 Level = GetSkillLevel(SkillVnum);
	const int32 Need = Level - 20;                 // successful reads already banked needed before level up
	const int32 SuccessesForLevel = Need + 1;      // M1(20)=1, M2(21)=2, ... M10(29)=10
	int32& Count = MasteryReadCounts.FindOrAdd(SkillVnum);

	const float SuccessChance = FMath::Clamp(
		UMT2GameplaySettings::Get().SkillBookSuccessChance, 0.0f, 1.0f);
	if (!bGuaranteedSuccess && FMath::FRand() > SuccessChance)
	{
		OutReadsRemaining = SuccessesForLevel - Count;
		MarkPersistenceDirty();
		return EMT2SkillBookResult::Failed;
	}

	if (Count >= Need)
	{
		MasteryReadCounts.Remove(SkillVnum);
		SetSkillLevel(SkillVnum, Level + 1);       // M(n) -> M(n+1), or M10 -> G1
		MarkPersistenceDirty();
		return EMT2SkillBookResult::LeveledUp;
	}

	++Count;
	OutReadsRemaining = SuccessesForLevel - Count;
	MarkPersistenceDirty();
	return EMT2SkillBookResult::Progressed;
}

int32 UMT2SkillComponent::GetMasteryReadCount(int32 SkillVnum) const
{
	const int32* Count = MasteryReadCounts.Find(SkillVnum);
	return Count ? *Count : 0;
}

int32 UMT2SkillComponent::GetGrandMasterReadCount(int32 SkillVnum) const
{
	const AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner());
	const UMT2QuestManagerComponent* Manager = State ? State->GetQuestManagerComponent() : nullptr;
	const FName Flag(*FString::Printf(TEXT("training_grandmaster_skill.skill%d"), SkillVnum));
	if (Manager) { return Manager->GetQuestFlag(Flag); }
	return GrandMasterReadCounts.FindRef(SkillVnum);
}

bool UMT2SkillComponent::CanTrainGrandMasterSkill(int32 SkillVnum) const
{
	if (SkillVnum <= 0 || SkillVnum >= 255 || GetSkillMastery(SkillVnum) != EMT2SkillMastery::GrandMaster) { return false; }
	const UMT2SkillDefinition* Skill = FindSkillDefinition(SkillVnum);
	if (!Skill) { Skill = UMT2SkillSet::FindSkillAcrossSets(SkillVnum); }
	if (!Skill || Skill->LegacySkillType <= 0) { return false; }
	// IsLearnableSkill: the chosen group need not contain this skill; raw proto job owns eligibility.
	if (Skill->LegacySkillType == 5)
	{
		return SkillVnum != 140 || GetOwnerRace() == EMT2CharacterRace::Assassin;
	}
	if (SkillGroup == 0) { return false; }
	if (Skill->LegacySkillType - 1 == static_cast<int32>(GetOwnerRace())) { return true; }
	if (Skill->LegacySkillType == 6 && SkillVnum >= 112 && SkillVnum <= 119)
	{
		const int32 First = SkillVnum < 116 ? 112 : 116;
		for (int32 Vnum = First; Vnum < First + 4; ++Vnum)
		{
			if (Vnum != SkillVnum && GetSkillLevel(Vnum) != 0) { return false; }
		}
		return true;
	}
	return false;
}

bool UMT2SkillComponent::TrainGrandMasterSkill(int32 SkillVnum)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() ||
		!CanTrainGrandMasterSkill(SkillVnum))
	{
		return false;
	}

	const int32 Level = GetSkillLevel(SkillVnum);
	const int32 GradeIndex = FMath::Clamp(Level - 30, 0, 9);
	const UMT2GameplaySettings& Settings = UMT2GameplaySettings::Get();
	if (!Settings.GrandMasterSuccessDenominators.IsValidIndex(GradeIndex) ||
		!Settings.GrandMasterMinimumReads.IsValidIndex(GradeIndex) || !Settings.GrandMasterMaximumReads.IsValidIndex(GradeIndex) ||
		Settings.GrandMasterSuccessDenominators[GradeIndex] <= 0 || Settings.GrandMasterMinimumReads[GradeIndex] < 0 ||
		Settings.GrandMasterMaximumReads[GradeIndex] < Settings.GrandMasterMinimumReads[GradeIndex])
	{
		UE_LOG(LogTemp, Error, TEXT("Invalid grand-master configuration for grade %d"), GradeIndex + 1);
		return false;
	}
	int32 Denominator = Settings.GrandMasterSuccessDenominators[GradeIndex];
	const int32 MinimumReads = Settings.GrandMasterMinimumReads[GradeIndex];
	const int32 MaximumReads = Settings.GrandMasterMaximumReads[GradeIndex];

	const int32 PreviousReads = GetGrandMasterReadCount(SkillVnum);
	if (PreviousReads == MAX_int32) { return false; }
	const int32 TotalReads = PreviousReads + 1;
	GrandMasterReadCounts.Add(SkillVnum, TotalReads);
	if (AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner()))
	{
		State->GetQuestManagerComponent()->SetQuestFlag(
			FName(*FString::Printf(TEXT("training_grandmaster_skill.skill%d"), SkillVnum)), TotalReads);
	}
	if (HasBookReadingEffect(MT2AffectId::SkillBookGuaranteedSuccess))
	{
		Denominator = Denominator / 2 + Denominator % 2;
		ConsumeBookReadingEffect(MT2AffectId::SkillBookGuaranteedSuccess);
	}
	bool bSucceeded = FMath::RandRange(1, Denominator) == 2;
	if (TotalReads < MinimumReads)
	{
		bSucceeded = false;
	}
	else if (TotalReads > MaximumReads)
	{
		bSucceeded = true;
	}

	if (bSucceeded)
	{
		SetSkillLevel(SkillVnum, FMath::Min(Level + 1, MT2SkillLimits::MaxSkillLevel));
	}
	// Legacy LearnGrandMasterSkill does not enforce this deadline (its check is commented out).
	// The calling quest owns its separate next_time gate. Accepted failures still set the skill deadline.
	RestoreSkillBookCooldown(SkillVnum, FDateTime::UtcNow().ToUnixTimestamp() + FMath::RandRange(28800, 43200));
	MarkPersistenceDirty();
	return bSucceeded;
}

void UMT2SkillComponent::RestoreGrandMasterReadCount(int32 SkillVnum, int32 Count)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	if (Count > 0)
	{
		GrandMasterReadCounts.Add(SkillVnum, Count);
	}
	else
	{
		GrandMasterReadCounts.Remove(SkillVnum);
	}
	if (AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner()))
	{
		State->GetQuestManagerComponent()->SetQuestFlag(
			FName(*FString::Printf(TEXT("training_grandmaster_skill.skill%d"), SkillVnum)), FMath::Max(Count, 0));
	}
}

void UMT2SkillComponent::MigrateLegacyGrandMasterReadCounts()
{
	AMT2PlayerState* State = Cast<AMT2PlayerState>(GetOwner());
	UMT2QuestManagerComponent* Manager = State ? State->GetQuestManagerComponent() : nullptr;
	if (!Manager || !State->HasAuthority()) { return; }
	for (const auto& Entry : GrandMasterReadCounts)
	{
		const FName Flag(*FString::Printf(TEXT("training_grandmaster_skill.skill%d"), Entry.Key));
		if (!Manager->HasQuestFlag(Flag)) { Manager->SetQuestFlag(Flag, Entry.Value); }
	}
}

int64 UMT2SkillComponent::GetSkillBookCooldownEnd(int32 SkillVnum) const
{
	return SkillBookCooldownEnds.FindRef(SkillVnum);
}

int64 UMT2SkillComponent::GetSkillBookCooldownRemaining(int32 SkillVnum) const
{
	return FMath::Max<int64>(
		GetSkillBookCooldownEnd(SkillVnum) - FDateTime::UtcNow().ToUnixTimestamp(), 0);
}

void UMT2SkillComponent::RestoreMasteryReadCount(int32 SkillVnum, int32 Count)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	if (Count > 0)
	{
		MasteryReadCounts.Add(SkillVnum, Count);
	}
	else
	{
		MasteryReadCounts.Remove(SkillVnum);
	}
}

void UMT2SkillComponent::RestoreSkillBookCooldown(int32 SkillVnum, int64 CooldownEndUnix)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	if (CooldownEndUnix > FDateTime::UtcNow().ToUnixTimestamp())
	{
		SkillBookCooldownEnds.Add(SkillVnum, CooldownEndUnix);
	}
	else
	{
		SkillBookCooldownEnds.Remove(SkillVnum);
	}
}

bool UMT2SkillComponent::SetSkillLevel(int32 SkillVnum, int32 NewLevel)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !SkillLevels.SetSkillLevel(SkillVnum, NewLevel))
	{
		return false;
	}

	NotifySkillLevelsChanged();
	GetOwner()->ForceNetUpdate();
	return true;
}

void UMT2SkillComponent::ResetAllSkills()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	const TArray<FMT2SkillLevelEntry> ExistingSkills = SkillLevels.GetEntries();
	for (const FMT2SkillLevelEntry& Skill : ExistingSkills)
	{
		SkillLevels.SetSkillLevel(Skill.SkillVnum, 0);
	}
	SkillGroup = 0;
	MasteryReadCounts.Reset();
	GrandMasterReadCounts.Reset();
	SkillBookCooldownEnds.Reset();
	OnRep_SkillGroup();
	NotifySkillLevelsChanged();
	GetOwner()->ForceNetUpdate();
}

void UMT2SkillComponent::ServerLearnSkillByPoint_Implementation(int32 SkillVnum)
{
	LearnSkillByPoint(SkillVnum);
}

void UMT2SkillComponent::ServerSelectSkillGroup_Implementation(int32 NewSkillGroup)
{
	SelectSkillGroup(NewSkillGroup);
}

void UMT2SkillComponent::OnRep_SkillGroup()
{
	CachedSkillSet = nullptr;
	OnSkillGroupChanged.Broadcast(SkillGroup);
}

void UMT2SkillComponent::RestorePersistedSkillGroup(int32 SavedSkillGroup)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	const int32 ClampedGroup = FMath::Clamp(SavedSkillGroup, 0, 2);
	if (SkillGroup != ClampedGroup)
	{
		SkillGroup = ClampedGroup;
		OnRep_SkillGroup();
	}
}

void UMT2SkillComponent::NotifyCooldownStarted(int32 SkillVnum, float CooldownSeconds)
{
	if (SkillVnum <= 0 || CooldownSeconds <= 0.0f || !GetWorld())
	{
		return;
	}
	FClientCooldown& Cooldown = ClientCooldowns.FindOrAdd(SkillVnum);
	Cooldown.EndTime = GetWorld()->GetTimeSeconds() + CooldownSeconds;
	Cooldown.Duration = CooldownSeconds;
}

float UMT2SkillComponent::GetCooldownRemaining(int32 SkillVnum, float& OutDuration) const
{
	OutDuration = 0.0f;
	const FClientCooldown* Cooldown = ClientCooldowns.Find(SkillVnum);
	if (!Cooldown || !GetWorld())
	{
		return 0.0f;
	}
	const double Remaining = Cooldown->EndTime - GetWorld()->GetTimeSeconds();
	if (Remaining <= 0.0)
	{
		return 0.0f;
	}
	OutDuration = Cooldown->Duration;
	return static_cast<float>(Remaining);
}

void UMT2SkillComponent::NotifySkillLevelsChanged()
{
	OnSkillLevelsChanged.Broadcast();
}
