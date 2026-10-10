#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MT2DungeonSubsystem.generated.h"
class AMT2DungeonRoom;
class AMT2PlayerState;
class AMT2Mob;

// World-local logical stages, not additional worlds or private map processes.
UCLASS()
class METIN2_API UMT2DungeonSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	bool RegisterRoom(AMT2DungeonRoom* Room);
	void UnregisterRoom(AMT2DungeonRoom* Room);
	AMT2DungeonRoom* FindRoom(FName RoomId) const;
	AMT2DungeonRoom* GetPlayerRoom(const AMT2PlayerState* Player) const;
	void ReconcilePlayer(AMT2PlayerState* Player);
	bool AssignPlayer(AMT2PlayerState* Player, AMT2DungeonRoom* Room);
	void RemovePlayer(AMT2PlayerState* Player, AMT2DungeonRoom* Room);
	bool AssignEnemy(AMT2Mob* Mob, AMT2DungeonRoom* Room);
	void RemoveEnemy(AMT2Mob* Mob, AMT2DungeonRoom* Room);
	virtual void Deinitialize() override;
private:
	TMap<FName, TWeakObjectPtr<AMT2DungeonRoom>> Rooms;
	TMap<TWeakObjectPtr<AMT2PlayerState>, TWeakObjectPtr<AMT2DungeonRoom>> Players;
	TMap<TWeakObjectPtr<AMT2Mob>, TWeakObjectPtr<AMT2DungeonRoom>> Enemies;
};
