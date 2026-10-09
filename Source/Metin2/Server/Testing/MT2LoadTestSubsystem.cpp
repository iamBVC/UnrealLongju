#include "Server/Testing/MT2LoadTestSubsystem.h"

#include "Characters/MT2PlayerCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Core/MT2VnumRegistrySubsystem.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "FramePro/FramePro.h"
#include "GameFramework/GameModeBase.h"
#include "Items/MT2InventoryComponent.h"
#include "Player/MT2PlayerController.h"
#include "Player/MT2PlayerState.h"
#include "Server/Testing/MT2LoadTestController.h"
#include "Server/Testing/MT2LoadTestSettings.h"
#include "World/MT2MapAttributes.h"

namespace { constexpr int32 WarriorSwordVnum = 19; }

int32 UMT2LoadTestSubsystem::GetPlayerCount() const
{
	int32 Count = 0;
	for (const auto& Controller : Controllers) if (Controller.IsValid() && !Controller->IsActorBeingDestroyed()) ++Count;
	return Count;
}

int32 UMT2LoadTestSubsystem::GetQueuedCount() const
{
	int32 Count = 0; for (const auto& Request : Requests) Count += Request.Remaining; return Count;
}

bool UMT2LoadTestSubsystem::QueuePlayers(AMT2PlayerCharacter* Requester, int32 Count, float Radius, int32 Level, FString& OutMessage)
{
#if UE_BUILD_SHIPPING
	OutMessage = TEXT("Fake players are unavailable in Shipping builds."); return false;
#else
	const auto* State = Requester ? Requester->GetPlayerState<AMT2PlayerState>() : nullptr;
	const auto* Settings = GetDefault<UMT2LoadTestSettings>();
	if (!Requester || !Requester->HasAuthority() || Requester->GetWorld() != GetWorld() || !State || !State->IsAdmin() || State->IsABot())
	{
		OutMessage = TEXT("An authoritative admin player is required."); return false;
	}
	if (!Settings->bEnabled) { OutMessage = TEXT("Server load testing is disabled in Project Settings."); return false; }
	if (Count < 1 || Count > FMath::Clamp(Settings->MaximumPlayers, 1, 500) - GetPlayerCount() - GetQueuedCount() ||
		!FMath::IsFinite(Radius) || Radius < 100.f || Radius > 20000.f || Level < 1 || Level > State->GetMaximumCharacterLevel())
	{
		OutMessage = TEXT("Invalid count/radius/level, or the fake-player limit is exceeded."); return false;
	}
	const auto* Instance = GetWorld()->GetGameInstance();
	auto* Registry = Instance ? Instance->GetSubsystem<UMT2VnumRegistrySubsystem>() : nullptr;
	if (!Registry || !Registry->ResolveItemTemplateClass(WarriorSwordVnum))
	{
		OutMessage = TEXT("Sword VNUM 19 is not available in the item registry."); return false;
	}
	FRequest Request; Request.Requester = Requester; Request.Origin = Requester->GetActorLocation();
	Request.Radius = Radius; Request.Level = Level; Request.Remaining = Count;
	Requests.Add(Request);
	OutMessage = FString::Printf(TEXT("Queued %d warrior fake players with sword VNUM 19. These are not network clients."), Count);
	return true;
#endif
}

bool UMT2LoadTestSubsystem::SpawnPlayer(const FRequest& Request)
{
	AMT2LoadTestController* Controller = GetWorld()->SpawnActor<AMT2LoadTestController>();
	if (!Controller) return false;
	auto* State = Controller->GetPlayerState<AMT2PlayerState>();
	if (!State) { Controller->Destroy(); return false; }
	State->SetCharacterName(FString::Printf(TEXT("FakeWarrior_%llu"), NextIdentity++));
	State->SetCharacterAppearance(FMT2CharacterAppearance());
	State->SetCharacterLevel(Request.Level);
	if (const auto* CreatorState = Request.Requester->GetPlayerState<AMT2PlayerState>()) State->SetEmpire(CreatorState->GetEmpire());
	const auto* GameMode = GetWorld()->GetAuthGameMode();
	UClass* PawnClass = GameMode ? GameMode->DefaultPawnClass.Get() : AMT2PlayerCharacter::StaticClass();
	if (!PawnClass || !PawnClass->IsChildOf(AMT2PlayerCharacter::StaticClass())) { Controller->Destroy(); return false; }
	const auto* Capsule = PawnClass->GetDefaultObject<AMT2PlayerCharacter>()->GetCapsuleComponent();
	FVector Location; bool bFound = false;
	for (int32 Attempt = 0; Attempt < 12; ++Attempt)
	{
		const float Angle = FMath::FRand() * 2.f * UE_PI;
		const float Distance = FMath::Sqrt(FMath::FRand()) * Request.Radius;
		const FVector XY = Request.Origin + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Distance;
		FHitResult Ground;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MT2FakePlayerSpawn), false, Request.Requester.Get());
		if (!GetWorld()->LineTraceSingleByObjectType(Ground, XY + FVector(0,0,5000), XY - FVector(0,0,5000),
			FCollisionObjectQueryParams(ECC_WorldStatic), Params)) continue;
		Location = Ground.ImpactPoint + FVector(0,0,Capsule->GetScaledCapsuleHalfHeight() + 2.f);
		const auto* Attributes = GetWorld()->GetSubsystem<UMT2MapAttributeSubsystem>();
		if (Attributes && Attributes->IsBlocked(Location)) continue;
		if (GetWorld()->OverlapBlockingTestByProfile(Location, FQuat::Identity, Capsule->GetCollisionProfileName(),
			FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Params)) continue;
		bFound = true; break;
	}
	if (!bFound) { Controller->Destroy(); return false; }
	FActorSpawnParameters Params; Params.Owner = Controller; Params.ObjectFlags |= RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding;
	auto* Character = GetWorld()->SpawnActor<AMT2PlayerCharacter>(PawnClass, Location, FRotator::ZeroRotator, Params);
	if (!Character) { Controller->Destroy(); return false; }
	Controller->Possess(Character);
	Controller->InitializeBehavior(Request.Origin, Request.Radius);
	auto* Inventory = Character->GetInventoryComponent();
	bool bEquipped = false;
	if (Inventory->AddItemByVnum(WarriorSwordVnum, 1))
	{
		const auto& Slots = Inventory->GetSlots();
		for (int32 Index = 0; Index < Slots.Num(); ++Index)
		{
			if (Slots[Index].Vnum == WarriorSwordVnum && !Slots[Index].IsEmpty())
			{
				bEquipped = Inventory->EquipItemFromSlot(Index); break;
			}
		}
	}
	if (!bEquipped) { Controller->RemoveFakePlayer(); return false; }
	Controllers.Add(Controller);
	return true;
}

bool UMT2LoadTestSubsystem::IsTickable() const
{
	return !IsTemplate() && GetWorld() && GetWorld()->IsGameWorld() && GetWorld()->GetNetMode() != NM_Client &&
		(!Requests.IsEmpty() || !Controllers.IsEmpty());
}

TStatId UMT2LoadTestSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMT2LoadTestSubsystem, STATGROUP_Tickables);
}

void UMT2LoadTestSubsystem::Tick(float DeltaSeconds)
{
	FRAMEPRO_NAMED_SCOPE("MT2.LoadTest.Tick");
	const auto* Settings = GetDefault<UMT2LoadTestSettings>();
	for (int32 Batch = 0; Batch < FMath::Clamp(Settings->SpawnBatchSize, 1, 20) && !Requests.IsEmpty(); ++Batch)
	{
		auto& Request = Requests[0];
		const auto* Requester = Request.Requester.Get();
		const auto* State = Requester ? Requester->GetPlayerState<AMT2PlayerState>() : nullptr;
		if (!State || !State->IsAdmin() || !Settings->bEnabled) { Requests.RemoveAt(0); continue; }
		if (SpawnPlayer(Request)) ++Request.Spawned; else ++Request.Failed;
		if (--Request.Remaining == 0)
		{
			if (auto* PC = Cast<AMT2PlayerController>(Requester->GetController()))
			{
				PC->SendAdminCommandMessage(FString::Printf(TEXT("Fake players: %d spawned, %d failed (ground/equipment)."), Request.Spawned, Request.Failed));
			}
			Requests.RemoveAt(0);
		}
	}
	Snapshot.Reset(); Snapshot.Reserve(Controllers.Num());
	for (auto It = Controllers.CreateIterator(); It; ++It)
	{
		if (!It->IsValid()) It.RemoveCurrent(); else Snapshot.Add(*It);
	}
	const double Now = GetWorld()->GetTimeSeconds();
	for (const auto& Entry : Snapshot) if (auto* Controller = Entry.Get()) Controller->Think(Now);
}

void UMT2LoadTestSubsystem::ForgetController(AMT2LoadTestController* Controller) { Controllers.Remove(Controller); }

int32 UMT2LoadTestSubsystem::ClearPlayers()
{
	Requests.Reset();
	const auto Copy = Controllers.Array();
	int32 Removed = 0;
	for (const auto& Entry : Copy) if (auto* Controller = Entry.Get()) { Controller->RemoveFakePlayer(); ++Removed; }
	Controllers.Reset(); Snapshot.Reset();
	return Removed;
}

void UMT2LoadTestSubsystem::Deinitialize()
{
	Requests.Reset(); Controllers.Reset(); Snapshot.Reset();
	Super::Deinitialize(); // The world's actor teardown owns pawn/controller/PlayerState destruction.
}
