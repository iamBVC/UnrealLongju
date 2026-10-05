#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MT2QuestEntitySubsystem.generated.h"

class AMT2CharacterBase;

// Server-session entity identities, not template vnums or persistent character IDs.
UCLASS()
class METIN2_API UMT2QuestEntitySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	int32 GetEntityId(AMT2CharacterBase* Character);
	AMT2CharacterBase* FindEntity(int32 Id) const;
	virtual void Deinitialize() override;
private:
	UFUNCTION() void HandleEntityDestroyed(AActor* Actor);
	UFUNCTION() void HandleEntityEndPlay(AActor* Actor, EEndPlayReason::Type Reason);
	TMap<TWeakObjectPtr<AMT2CharacterBase>, int32> CharacterIds;
	TMap<int32, TWeakObjectPtr<AMT2CharacterBase>> Characters;
};
