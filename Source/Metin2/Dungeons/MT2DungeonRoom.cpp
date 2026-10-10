#include "Dungeons/MT2DungeonRoom.h"
#include "Dungeons/MT2DungeonSubsystem.h"
#include "Characters/MT2PlayerCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/ArrowComponent.h"
#include "Components/MT2HealthComponent.h"
#include "Core/MT2VnumRegistrySubsystem.h"
#include "Engine/GameInstance.h"
#include "GameFramework/GameStateBase.h"
#include "Mobs/MT2Mob.h"
#include "Mobs/MT2MobAIComponent.h"
#include "Net/UnrealNetwork.h"
#include "Persistence/MT2PersistenceComponent.h"
#include "Player/MT2PlayerState.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogMT2Dungeon, Log, All);

AMT2DungeonRoom::AMT2DungeonRoom()
{
	PrimaryActorTick.bCanEverTick = false; bReplicates = true;
	SetNetUpdateFrequency(2.f); SetMinNetUpdateFrequency(1.f);
	Bounds = CreateDefaultSubobject<UBoxComponent>(TEXT("RoomBounds")); SetRootComponent(Bounds);
	Bounds->SetBoxExtent(FVector(1000,1000,500)); Bounds->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Bounds->SetCollisionResponseToAllChannels(ECR_Ignore); Bounds->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Bounds->SetGenerateOverlapEvents(true);
	Entrance = CreateDefaultSubobject<UArrowComponent>(TEXT("Entrance")); Entrance->SetupAttachment(Bounds);
	Entrance->SetRelativeLocation(FVector(0,0,100));
}
void AMT2DungeonRoom::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority()) { ApplyDoors(); return; }
	auto* Registry = GetWorld()->GetSubsystem<UMT2DungeonSubsystem>();
	bRegistered = Registry && Registry->RegisterRoom(this);
	if (!bRegistered) { UE_LOG(LogMT2Dungeon, Error, TEXT("Dungeon room %s requires a unique nonempty RoomId."), *GetPathName()); return; }
	ApplyDoors();
	SetNetCullDistanceSquared(FMath::Max(GetNetCullDistanceSquared(), FMath::Square(Bounds->GetScaledBoxExtent().Size() + 5000.0)));
	Bounds->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::OnOverlap);
	Bounds->OnComponentEndOverlap.AddDynamic(this, &ThisClass::OnEndOverlap);
	TArray<AActor*> Overlapping; Bounds->GetOverlappingActors(Overlapping, AMT2PlayerCharacter::StaticClass());
	for (AActor* Actor : Overlapping) if (auto* Player = Cast<AMT2PlayerCharacter>(Actor)) JoinPlayer(Player->GetPlayerState<AMT2PlayerState>());
}
void AMT2DungeonRoom::EndPlay(const EEndPlayReason::Type Reason)
{
	bRegistered = false;
	GetWorld()->GetTimerManager().ClearAllTimersForObject(this);
	Bounds->OnComponentBeginOverlap.RemoveDynamic(this, &ThisClass::OnOverlap);
	Bounds->OnComponentEndOverlap.RemoveDynamic(this, &ThisClass::OnEndOverlap);
	for (auto Member : Members) if (Member.IsValid()) Member->OnEndPlay.RemoveDynamic(this, &ThisClass::OnMemberEndPlay);
	for (const auto& Pair : MemberPawns) if (Pair.Key.IsValid()) Pair.Key->OnEndPlay.RemoveDynamic(this, &ThisClass::OnPawnEndPlay);
	const bool bLocalRemoval = Reason == EEndPlayReason::Destroyed || Reason == EEndPlayReason::RemovedFromWorld;
	ClearEnemies(HasAuthority() && bLocalRemoval && !GetWorld()->bIsTearingDown);
	if (auto* Registry = GetWorld()->GetSubsystem<UMT2DungeonSubsystem>()) Registry->UnregisterRoom(this);
	Members.Reset(); MemberPawns.Reset(); bRegistered = false; Super::EndPlay(Reason);
}
void AMT2DungeonRoom::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMT2DungeonRoom, Doors);
	DOREPLIFETIME(AMT2DungeonRoom, Phase); DOREPLIFETIME(AMT2DungeonRoom, RunSerial);
	DOREPLIFETIME(AMT2DungeonRoom, RemainingEnemies); DOREPLIFETIME(AMT2DungeonRoom, Participants); DOREPLIFETIME(AMT2DungeonRoom, Deadline);
}
bool AMT2DungeonRoom::ContainsLocation(FVector Location) const
{
	const FVector Local = Bounds->GetComponentTransform().InverseTransformPosition(Location), Extent = Bounds->GetUnscaledBoxExtent();
	return FMath::Abs(Local.X) <= Extent.X && FMath::Abs(Local.Y) <= Extent.Y && FMath::Abs(Local.Z) <= Extent.Z;
}
FString AMT2DungeonRoom::PlayerIdentity(const AMT2PlayerState* Player) const
{
	const auto* Persistence = IsValid(Player) ? Player->GetPersistenceComponent() : nullptr;
	return Persistence ? Persistence->GetEntityId() : FString();
}
bool AMT2DungeonRoom::JoinPlayer(AMT2PlayerState* Player)
{
	if (!HasAuthority() || !bRegistered || Phase == EMT2DungeonRoomPhase::Failed || !IsValid(Player) || Player->IsActorBeingDestroyed()) return false;
	APawn* Pawn = Player->GetPawn();
	if (!IsValid(Pawn) || Pawn->GetWorld() != GetWorld() ||
		(!ContainsLocation(Pawn->GetActorLocation()) && !Bounds->IsOverlappingActor(Pawn))) return false;
	auto* Registry = GetWorld()->GetSubsystem<UMT2DungeonSubsystem>();
	if (!Registry || !Registry->AssignPlayer(Player, this)) return false;
	if (!Members.Contains(Player)) { Members.Add(Player); Player->OnEndPlay.AddDynamic(this, &ThisClass::OnMemberEndPlay); }
	for (auto It = MemberPawns.CreateIterator(); It; ++It) if (It.Value().Get() == Player && It.Key().Get() != Pawn)
	{
		if (It.Key().IsValid()) It.Key()->OnEndPlay.RemoveDynamic(this, &ThisClass::OnPawnEndPlay);
		It.RemoveCurrent();
	}
	MemberPawns.Add(Pawn, Player); Pawn->OnEndPlay.AddUniqueDynamic(this, &ThisClass::OnPawnEndPlay);
	if (Phase == EMT2DungeonRoomPhase::Active)
	{
		const FString Id = PlayerIdentity(Player);
		if (!Id.IsEmpty()) EligiblePlayers.Add(Id);
	}
	Participants = Members.Num(); GetWorld()->GetTimerManager().ClearTimer(ResetTimer); ForceNetUpdate(); return true;
}
void AMT2DungeonRoom::LeavePlayer(AMT2PlayerState* Player)
{
	if (!HasAuthority() || !Player || !Members.Remove(Player)) return;
	Player->OnEndPlay.RemoveDynamic(this, &ThisClass::OnMemberEndPlay);
	for (auto It = MemberPawns.CreateIterator(); It; ++It) if (It.Value().Get() == Player)
	{
		if (It.Key().IsValid()) It.Key()->OnEndPlay.RemoveDynamic(this, &ThisClass::OnPawnEndPlay);
		It.RemoveCurrent();
	}
	if (auto* Registry = GetWorld()->GetSubsystem<UMT2DungeonSubsystem>()) Registry->RemovePlayer(Player, this);
	Participants = Members.Num(); ForceNetUpdate(); ScheduleEmptyReset();
}
void AMT2DungeonRoom::OnOverlap(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent*, int32, bool, const FHitResult&)
{
	if (auto* Player = Cast<AMT2PlayerCharacter>(Other))
		if (auto* Registry = GetWorld()->GetSubsystem<UMT2DungeonSubsystem>()) Registry->ReconcilePlayer(Player->GetPlayerState<AMT2PlayerState>());
}
void AMT2DungeonRoom::OnEndOverlap(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent*, int32)
{
	if (auto* Player = Cast<AMT2PlayerCharacter>(Other); Player && !Bounds->IsOverlappingActor(Player))
	{
		auto* State = Player->GetPlayerState<AMT2PlayerState>();
		if (State && State->GetPawn() == Player)
			if (auto* Registry = GetWorld()->GetSubsystem<UMT2DungeonSubsystem>()) Registry->ReconcilePlayer(State);
		ScheduleEmptyReset();
	}
}
void AMT2DungeonRoom::OnMemberEndPlay(AActor* Actor, EEndPlayReason::Type) { LeavePlayer(Cast<AMT2PlayerState>(Actor)); }
void AMT2DungeonRoom::OnPawnEndPlay(AActor* Actor, EEndPlayReason::Type)
{
	const auto* Member = MemberPawns.Find(Actor);
	if (Member) LeavePlayer(Member->Get());
}
bool AMT2DungeonRoom::StartEncounter()
{
	if (!HasAuthority() || !bRegistered || bResetting || Phase != EMT2DungeonRoomPhase::Idle || Members.IsEmpty() ||
		!FMath::IsFinite(EncounterSeconds) || EncounterSeconds < 0.f) return false;
	RunSerial = RunSerial == MAX_int32 ? 1 : RunSerial + 1;
	EligiblePlayers.Reset(); ClaimedPlayers.Reset();
	for (auto Member : Members) { const FString Id = PlayerIdentity(Member.Get()); if (!Id.IsEmpty()) EligiblePlayers.Add(Id); }
	SetPhase(EMT2DungeonRoomPhase::Active); Deadline = EncounterSeconds > 0.f ? GetWorld()->GetTimeSeconds() + EncounterSeconds : 0.0;
	const int32 Epoch = RunSerial;
	if (EncounterSeconds > 0.f) GetWorld()->GetTimerManager().SetTimer(EncounterTimer, FTimerDelegate::CreateWeakLambda(this, [this, Epoch]()
		{ if (RunSerial == Epoch) FailEncounter(); }), EncounterSeconds, false);
	bSpawning = true;
	auto* GI = GetGameInstance(); auto* Vnums = GI ? GI->GetSubsystem<UMT2VnumRegistrySubsystem>() : nullptr;
	TMap<int32, TArray<AMT2Mob*>> Groups;
	for (const auto& Spawn : Spawns)
	{
		const auto MobClass = Vnums ? Vnums->ResolveMobClass(Spawn.Vnum) : TSubclassOf<AMT2Mob>();
		const FTransform Transform = Spawn.LocalTransform * GetActorTransform();
		FActorSpawnParameters Parameters; Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding;
		AMT2Mob* Mob = MobClass && ContainsLocation(Transform.GetLocation()) ? GetWorld()->SpawnActor<AMT2Mob>(MobClass, Transform, Parameters) : nullptr;
		if (!Mob || !RegisterEnemy(Mob))
		{
			if (Mob) Mob->Destroy();
			bSpawning = false; UE_LOG(LogMT2Dungeon, Error, TEXT("Room %s failed encounter spawn vnum=%d."), *RoomId.ToString(), Spawn.Vnum);
			ClearEnemies(true); FailEncounter(); return false;
		}
		if (Spawn.bForceAggressive) Mob->GetMobAIComponent()->SetForceAggressive(true);
		if (Spawn.GroupId > 0) Groups.FindOrAdd(Spawn.GroupId).Add(Mob);
	}
	for (const auto& Pair : Groups) for (AMT2Mob* Mob : Pair.Value) Mob->SetSpawnGroupMembers(Pair.Value);
	bSpawning = false; EncounterStarted(); return Phase == EMT2DungeonRoomPhase::Active;
}
bool AMT2DungeonRoom::RegisterEnemy(AMT2Mob* Mob)
{
	if (!HasAuthority() || !bRegistered || bResetting || Phase != EMT2DungeonRoomPhase::Active || !IsValid(Mob) || Mob->IsActorBeingDestroyed() ||
		!ContainsLocation(Mob->GetActorLocation()) || !Mob->GetHealthComponent() || Mob->GetHealthComponent()->IsDead()) return false;
	auto* Registry = GetWorld()->GetSubsystem<UMT2DungeonSubsystem>();
	if (!Registry || !Registry->AssignEnemy(Mob, this)) return false;
	if (!Enemies.Contains(Mob))
	{
		Enemies.Add(Mob); AliveEnemies.Add(Mob);
		Mob->GetHealthComponent()->OnDeath.AddDynamic(this, &ThisClass::OnEnemyDeath);
		Mob->OnEndPlay.AddDynamic(this, &ThisClass::OnEnemyEndPlay);
	}
	RemainingEnemies = AliveEnemies.Num(); ForceNetUpdate(); return true;
}
void AMT2DungeonRoom::OnEnemyDeath()
{
	if (Phase != EMT2DungeonRoomPhase::Active) return;
	for (auto It = AliveEnemies.CreateIterator(); It; ++It) if (It->IsValid() && (*It)->GetHealthComponent()->IsDead()) It.RemoveCurrent();
	RemainingEnemies = AliveEnemies.Num(); ForceNetUpdate();
	if (!bSpawning && RemainingEnemies == 0 && !Enemies.IsEmpty()) CompleteEncounter();
}
void AMT2DungeonRoom::OnEnemyEndPlay(AActor* Actor, EEndPlayReason::Type Reason)
{
	AMT2Mob* Mob = Cast<AMT2Mob>(Actor);
	const bool bTracked = Enemies.Contains(Mob);
	const auto* Health = Mob ? Mob->GetHealthComponent() : nullptr;
	const bool bAlive = AliveEnemies.Contains(Mob) && (!Health || !Health->IsInitialized() || !Health->IsDead());
	AliveEnemies.Remove(Mob); Enemies.Remove(Mob); RemainingEnemies = AliveEnemies.Num();
	if (Mob)
	{
		if (auto* Component = Mob->GetHealthComponent()) Component->OnDeath.RemoveDynamic(this, &ThisClass::OnEnemyDeath);
		Mob->OnEndPlay.RemoveDynamic(this, &ThisClass::OnEnemyEndPlay);
	}
	if (auto* Registry = GetWorld()->GetSubsystem<UMT2DungeonSubsystem>()) Registry->RemoveEnemy(Mob, this);
	if (GetWorld()->bIsTearingDown || (Reason != EEndPlayReason::Destroyed && Reason != EEndPlayReason::RemovedFromWorld)) return;
	// Despawning a live objective must not count as a kill or open progression doors.
	if (bAlive && Phase == EMT2DungeonRoomPhase::Active) FailEncounter();
	else if (bTracked && Phase == EMT2DungeonRoomPhase::Active && !bSpawning && AliveEnemies.IsEmpty()) CompleteEncounter();
}
void AMT2DungeonRoom::SetPhase(EMT2DungeonRoomPhase NewPhase) { Phase = NewPhase; ApplyDoors(); ForceNetUpdate(); }
void AMT2DungeonRoom::ApplyDoors()
{
	const bool bOpen = Phase != EMT2DungeonRoomPhase::Active;
	for (AActor* Door : Doors) if (IsValid(Door) && Door != this && Door->GetWorld() == GetWorld())
	{ Door->SetActorHiddenInGame(bOpen); Door->SetActorEnableCollision(!bOpen); }
}
void AMT2DungeonRoom::CompleteEncounter()
{
	if (!HasAuthority() || !bRegistered || Phase != EMT2DungeonRoomPhase::Active) return;
	GetWorld()->GetTimerManager().ClearTimer(EncounterTimer); Deadline = 0.0;
	SetPhase(EMT2DungeonRoomPhase::Completed); OnEncounterCompleted(); EncounterCompleted(); ScheduleEmptyReset();
}
void AMT2DungeonRoom::FailEncounter()
{
	if (!HasAuthority() || !bRegistered || Phase != EMT2DungeonRoomPhase::Active) return;
	GetWorld()->GetTimerManager().ClearTimer(EncounterTimer); Deadline = 0.0;
	SetPhase(EMT2DungeonRoomPhase::Failed); EncounterFailed(); ScheduleEmptyReset();
}
void AMT2DungeonRoom::ClearEnemies(bool bDestroy)
{
	const auto Snapshot = Enemies.Array(); Enemies.Reset(); AliveEnemies.Reset(); RemainingEnemies = 0;
	for (auto Enemy : Snapshot) if (AMT2Mob* Mob = Enemy.Get())
	{
		if (auto* Health = Mob->GetHealthComponent()) Health->OnDeath.RemoveDynamic(this, &ThisClass::OnEnemyDeath);
		Mob->OnEndPlay.RemoveDynamic(this, &ThisClass::OnEnemyEndPlay);
		if (auto* Registry = GetWorld()->GetSubsystem<UMT2DungeonSubsystem>()) Registry->RemoveEnemy(Mob, this);
		if (bDestroy) Mob->Destroy();
	}
}
void AMT2DungeonRoom::ScheduleEmptyReset()
{
	if (!HasAuthority() || !bRegistered || !Members.IsEmpty() || Phase == EMT2DungeonRoomPhase::Idle) return;
	if (!FMath::IsFinite(EmptyResetSeconds) || EmptyResetSeconds <= 0.f)
	{ UE_LOG(LogMT2Dungeon, Error, TEXT("Room %s has invalid empty-reset delay."), *RoomId.ToString()); return; }
	GetWorld()->GetTimerManager().SetTimer(ResetTimer, FTimerDelegate::CreateWeakLambda(this, [this]() { ResetWhenEmpty(); }), EmptyResetSeconds, false);
}
bool AMT2DungeonRoom::ResetWhenEmpty()
{
	if (!HasAuthority() || !bRegistered || bResetting || !Members.IsEmpty()) return false;
	TArray<AActor*> Occupants; Bounds->GetOverlappingActors(Occupants, AMT2PlayerCharacter::StaticClass());
	for (AActor* Actor : Occupants) if (IsValid(Actor) && !Actor->IsActorBeingDestroyed()) return false;
	bResetting = true;
	GetWorld()->GetTimerManager().ClearTimer(EncounterTimer); GetWorld()->GetTimerManager().ClearTimer(ResetTimer);
	EligiblePlayers.Reset(); ClaimedPlayers.Reset(); Deadline = 0.0;
	ClearEnemies(true); SetPhase(EMT2DungeonRoomPhase::Idle); bResetting = false; ForceNetUpdate(); return true;
}
bool AMT2DungeonRoom::TryClaimCompletion(AMT2PlayerState* Player)
{
	const FString Id = PlayerIdentity(Player);
	if (!HasAuthority() || Phase != EMT2DungeonRoomPhase::Completed || !Members.Contains(Player) ||
		Id.IsEmpty() || !EligiblePlayers.Contains(Id) || ClaimedPlayers.Contains(Id)) return false;
	if (!IsValid(Player->GetPawn()) || !ContainsLocation(Player->GetPawn()->GetActorLocation())) return false;
	ClaimedPlayers.Add(Id); return true;
}
bool AMT2DungeonRoom::AdvancePlayer(AMT2PlayerState* Player)
{
	if (!HasAuthority() || Phase != EMT2DungeonRoomPhase::Completed || !Members.Contains(Player) || !IsValid(NextRoom) ||
		NextRoom == this || !NextRoom->bRegistered || NextRoom->GetWorld() != GetWorld() || NextRoom->GetPhase() == EMT2DungeonRoomPhase::Failed) return false;
	APawn* Pawn = IsValid(Player) ? Player->GetPawn() : nullptr;
	const FVector Location = NextRoom->Entrance->GetComponentLocation();
	if (!IsValid(Pawn) || !NextRoom->ContainsLocation(Location)) return false;
	const FVector OldLocation = Pawn->GetActorLocation(); const FRotator OldRotation = Pawn->GetActorRotation();
	bool bTeleported = false;
	for (int32 Attempt = 0; Attempt < 25 && !bTeleported; ++Attempt)
	{
		// Several members can advance together. Avoid stacking every capsule on the entrance.
		const FVector Offset(100.f * (Attempt % 5 - 2), 100.f * (Attempt / 5 - 2), 0);
		const FVector Candidate = Attempt == 0 ? Location : Location + Offset;
		if (NextRoom->ContainsLocation(Candidate))
			bTeleported = Pawn->TeleportTo(Candidate, NextRoom->Entrance->GetComponentRotation(), false, false);
	}
	if (!bTeleported) return false;
	LeavePlayer(Player);
	if (NextRoom->JoinPlayer(Player)) return true;
	Pawn->TeleportTo(OldLocation, OldRotation, false, true); JoinPlayer(Player); return false;
}
double AMT2DungeonRoom::GetSecondsRemaining() const
{
	const AGameStateBase* State = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	const double Now = State ? State->GetServerWorldTimeSeconds() : GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	return Phase == EMT2DungeonRoomPhase::Active && Deadline > 0.0 ? FMath::Max(Deadline - Now, 0.0) : 0.0;
}
