#include "Dungeons/MT2DungeonSubsystem.h"
#include "Dungeons/MT2DungeonRoom.h"
#include "Player/MT2PlayerState.h"
#include "Mobs/MT2Mob.h"
#include "Components/BoxComponent.h"
#include "GameFramework/Pawn.h"

bool UMT2DungeonSubsystem::RegisterRoom(AMT2DungeonRoom* Room)
{
	check(IsInGameThread());
	if (!IsValid(Room) || !Room->HasAuthority() || Room->GetWorld() != GetWorld() || Room->RoomId.IsNone()) return false;
	if (AMT2DungeonRoom* Existing = FindRoom(Room->RoomId); Existing && Existing != Room) return false;
	Rooms.Add(Room->RoomId, Room); return true;
}
void UMT2DungeonSubsystem::UnregisterRoom(AMT2DungeonRoom* Room)
{
	if (!Room) return;
	const auto* Existing = Rooms.Find(Room->RoomId);
	if (Existing && Existing->Get() == Room) Rooms.Remove(Room->RoomId);
	for (auto It = Players.CreateIterator(); It; ++It) if (!It.Value().IsValid() || It.Value() == Room) It.RemoveCurrent();
	for (auto It = Enemies.CreateIterator(); It; ++It) if (!It.Value().IsValid() || It.Value() == Room) It.RemoveCurrent();
}
AMT2DungeonRoom* UMT2DungeonSubsystem::FindRoom(FName Id) const
{
	const auto* Value = Rooms.Find(Id); AMT2DungeonRoom* Room = Value ? Value->Get() : nullptr;
	return IsValid(Room) && !Room->IsActorBeingDestroyed() ? Room : nullptr;
}
AMT2DungeonRoom* UMT2DungeonSubsystem::GetPlayerRoom(const AMT2PlayerState* Player) const
{
	const auto* Value = Players.Find(Player); AMT2DungeonRoom* Room = Value ? Value->Get() : nullptr;
	return IsValid(Player) && !Player->IsActorBeingDestroyed() && IsValid(Room) && !Room->IsActorBeingDestroyed() ? Room : nullptr;
}
bool UMT2DungeonSubsystem::AssignPlayer(AMT2PlayerState* Player, AMT2DungeonRoom* Room)
{
	if (!IsValid(Room) || !IsValid(Player) || Player->GetWorld() != GetWorld() || FindRoom(Room->RoomId) != Room) return false;
	if (AMT2DungeonRoom* Existing = GetPlayerRoom(Player); Existing && Existing != Room) return false;
	Players.Add(Player, Room); return true;
}
void UMT2DungeonSubsystem::ReconcilePlayer(AMT2PlayerState* Player)
{
	if (!IsValid(Player) || !Player->HasAuthority() || Player->GetWorld() != GetWorld()) return;
	APawn* Pawn = Player->GetPawn();
	if (!IsValid(Pawn)) return;
	if (AMT2DungeonRoom* Existing = GetPlayerRoom(Player))
	{
		if (Existing->Bounds->IsOverlappingActor(Pawn)) return;
		Existing->LeavePlayer(Player);
	}
	TArray<AActor*> Overlaps; Pawn->GetOverlappingActors(Overlaps, AMT2DungeonRoom::StaticClass());
	// Prefer the room containing the pawn center when its capsule touches adjacent volumes.
	for (AActor* Actor : Overlaps)
	{
		auto* Room = Cast<AMT2DungeonRoom>(Actor);
		if (Room && Room->ContainsLocation(Pawn->GetActorLocation()) && Room->JoinPlayer(Player)) return;
	}
	for (AActor* Actor : Overlaps) if (auto* Room = Cast<AMT2DungeonRoom>(Actor); Room && Room->JoinPlayer(Player)) return;
}
void UMT2DungeonSubsystem::RemovePlayer(AMT2PlayerState* Player, AMT2DungeonRoom* Room)
{
	const auto* Existing = Players.Find(Player); if (Existing && Existing->Get() == Room) Players.Remove(Player);
}
bool UMT2DungeonSubsystem::AssignEnemy(AMT2Mob* Mob, AMT2DungeonRoom* Room)
{
	if (!IsValid(Room) || !IsValid(Mob) || Mob->GetWorld() != GetWorld() || FindRoom(Room->RoomId) != Room) return false;
	const auto* Existing = Enemies.Find(Mob);
	if (Existing && Existing->IsValid() && Existing->Get() != Room) return false;
	Enemies.Add(Mob, Room); return true;
}
void UMT2DungeonSubsystem::RemoveEnemy(AMT2Mob* Mob, AMT2DungeonRoom* Room)
{
	const auto* Existing = Enemies.Find(Mob); if (Existing && Existing->Get() == Room) Enemies.Remove(Mob);
}
void UMT2DungeonSubsystem::Deinitialize()
{
	Rooms.Reset(); Players.Reset(); Enemies.Reset(); Super::Deinitialize();
}
