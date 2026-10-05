#include "Quests/MT2QuestEntitySubsystem.h"
#include "Characters/MT2CharacterBase.h"
#include "Engine/World.h"

namespace
{
	// Game-thread-only and never reused across world transitions or simultaneous PIE worlds.
	int64 NextQuestEntityId = 1;
}

int32 UMT2QuestEntitySubsystem::GetEntityId(AMT2CharacterBase* Character)
{
	check(IsInGameThread());
	if (!IsValid(Character) || Character->IsActorBeingDestroyed() || !Character->HasAuthority() ||
		Character->GetWorld() != GetWorld() || (GetWorld()->HasBegunPlay() && !Character->HasActorBegunPlay())) { return 0; }
	if (const int32* Existing = CharacterIds.Find(Character)) { return *Existing; }
	if (NextQuestEntityId > MAX_int32)
	{
		UE_LOG(LogTemp, Error, TEXT("Quest entity ID space exhausted; IDs cannot be reused."));
		return 0;
	}
	const int32 Id = static_cast<int32>(NextQuestEntityId++);
	CharacterIds.Add(Character, Id);
	Characters.Add(Id, Character);
	Character->OnDestroyed.AddUniqueDynamic(this, &UMT2QuestEntitySubsystem::HandleEntityDestroyed);
	Character->OnEndPlay.AddUniqueDynamic(this, &UMT2QuestEntitySubsystem::HandleEntityEndPlay);
	return Id;
}

AMT2CharacterBase* UMT2QuestEntitySubsystem::FindEntity(int32 Id) const
{
	check(IsInGameThread());
	const TWeakObjectPtr<AMT2CharacterBase>* Found = Characters.Find(Id);
	AMT2CharacterBase* Character = Found ? Found->Get() : nullptr;
	return IsValid(Character) && !Character->IsActorBeingDestroyed() && Character->HasAuthority() &&
		Character->GetWorld() == GetWorld() && (!GetWorld()->HasBegunPlay() || Character->HasActorBegunPlay()) ? Character : nullptr;
}

void UMT2QuestEntitySubsystem::HandleEntityDestroyed(AActor* Actor)
{
	AMT2CharacterBase* Character = Cast<AMT2CharacterBase>(Actor);
	if (const int32* Id = CharacterIds.Find(Character)) { Characters.Remove(*Id); }
	CharacterIds.Remove(Character);
	if (Character)
	{
		Character->OnDestroyed.RemoveDynamic(this, &UMT2QuestEntitySubsystem::HandleEntityDestroyed);
		Character->OnEndPlay.RemoveDynamic(this, &UMT2QuestEntitySubsystem::HandleEntityEndPlay);
	}
}

void UMT2QuestEntitySubsystem::HandleEntityEndPlay(AActor* Actor, EEndPlayReason::Type Reason)
{
	HandleEntityDestroyed(Actor);
}

void UMT2QuestEntitySubsystem::Deinitialize()
{
	for (const auto& Pair : CharacterIds)
	{
		if (AMT2CharacterBase* Character = Pair.Key.Get())
		{
			Character->OnDestroyed.RemoveDynamic(this, &UMT2QuestEntitySubsystem::HandleEntityDestroyed);
			Character->OnEndPlay.RemoveDynamic(this, &UMT2QuestEntitySubsystem::HandleEntityEndPlay);
		}
	}
	CharacterIds.Reset();
	Characters.Reset();
	Super::Deinitialize();
}
