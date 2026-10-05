/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Persistence/MT2PersistenceComponent.h"

#include "Engine/World.h"
#include "Persistence/MT2Persistable.h"
#include "Persistence/MT2PersistenceManager.h"
#include "Server/MT2ServerRuntimeSubsystem.h"
#include "TimerManager.h"

UMT2PersistenceComponent::UMT2PersistenceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

void UMT2PersistenceComponent::BeginPlay()
{
	Super::BeginPlay();
	bBegunPlay = true;
	AActor* Owner = GetOwner();
	UMT2PersistenceManager* Manager = UMT2PersistenceManager::Get(this);
	if (!Owner || !Owner->HasAuthority() || !Manager || !Manager->IsPersistenceEnabled())
	{
		State = EMT2PersistenceState::Disabled;
		return;
	}
	State = EMT2PersistenceState::Ready;
	StartAutosave();
	if (bAutoLoad && !EntityId.IsEmpty())
	{
		RequestLoad();
	}
}

void UMT2PersistenceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(AutosaveTimer);
	}
	if (bSaveOnEndPlay && bDirty && State != EMT2PersistenceState::Disabled)
	{
		RequestSave(true);
	}
	bBegunPlay = false;
	Super::EndPlay(EndPlayReason);
}

void UMT2PersistenceComponent::ConfigurePersistence(
	FName InEntityType, const FString& InEntityId, const FString& InOwnerId)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || InEntityType.IsNone() || InEntityId.IsEmpty())
	{
		return;
	}
	EntityType = InEntityType;
	EntityId = InEntityId;
	OwnerId = InOwnerId;
	bDirty = true;
	++DirtyGeneration;
	if (bBegunPlay)
	{
		UMT2PersistenceManager* Manager = UMT2PersistenceManager::Get(this);
		State = Manager && Manager->IsPersistenceEnabled()
			? EMT2PersistenceState::Ready
			: EMT2PersistenceState::Disabled;
		StartAutosave();
		if (bAutoLoad && State == EMT2PersistenceState::Ready)
		{
			RequestLoad();
		}
	}
}

void UMT2PersistenceComponent::RequestLoad()
{
	AActor* Owner = GetOwner();
	UMT2PersistenceManager* Manager = UMT2PersistenceManager::Get(this);
	if (!Owner || !Owner->HasAuthority() || !Manager || !Manager->IsPersistenceEnabled() ||
		EntityId.IsEmpty() || State == EMT2PersistenceState::Loading || State == EMT2PersistenceState::Saving)
	{
		return;
	}

	State = EMT2PersistenceState::Loading;
	TWeakObjectPtr<UMT2PersistenceComponent> WeakThis(this);
	Manager->LoadRecord(EntityType, EntityId,
		[WeakThis](FMT2PersistenceLoadResult&& Result)
		{
			UMT2PersistenceComponent* Component = WeakThis.Get();
			if (!Component)
			{
				return;
			}
			if (!Result.bSucceeded)
			{
				Component->OnLoadFinishedNative.Broadcast(false, false, false, Result.Error);
				Component->SetError(Result.Error);
				return;
			}
			if (!Result.bFound)
			{
				Component->State = EMT2PersistenceState::Ready;
				Component->OnLoadFinishedNative.Broadcast(true, false, false, FString());
				if (Component->bCreateIfMissing)
				{
					Component->RequestSave(true);
				}
				return;
			}

			AActor* PersistentOwner = Component->GetOwner();
			if (!PersistentOwner || !PersistentOwner->GetClass()->ImplementsInterface(UMT2Persistable::StaticClass()) ||
				!IMT2Persistable::Execute_ApplyPersistentStateJson(PersistentOwner, Result.Record.PayloadJson))
			{
				const FString Error = TEXT("Persistent payload could not be applied to its owner.");
				Component->OnLoadFinishedNative.Broadcast(false, true, Result.Record.bHasWorldPosition, Error);
				Component->SetError(Error);
				return;
			}
			Component->OwnerId = Result.Record.OwnerId;
			Component->SchemaVersion = Result.Record.SchemaVersion;
			Component->Revision = Result.Record.Revision;
			Component->bDirty = false;
			Component->DirtyGeneration = 0;
			Component->State = EMT2PersistenceState::Ready;
			Component->OnLoaded.Broadcast();
			Component->OnLoadFinishedNative.Broadcast(
				true, true, Result.Record.bHasWorldPosition, FString());
			if (!Component->PendingFlushes.IsEmpty()) Component->RequestSave(true);
		});
}

void UMT2PersistenceComponent::RequestSave(bool bForce)
{
	AActor* Owner = GetOwner();
	UMT2PersistenceManager* Manager = UMT2PersistenceManager::Get(this);
	if (!Owner || !Owner->HasAuthority() || !Manager || !Manager->IsPersistenceEnabled() ||
		EntityId.IsEmpty() || (!bDirty && !bForce) ||
		State == EMT2PersistenceState::Loading || State == EMT2PersistenceState::Saving)
	{
		return;
	}
	if (!Owner->GetClass()->ImplementsInterface(UMT2Persistable::StaticClass()))
	{
		SetError(TEXT("Persistence owner does not implement MT2Persistable."));
		return;
	}

	FMT2PersistentRecord Record;
	Record.EntityType = EntityType;
	Record.EntityId = EntityId;
	Record.OwnerId = OwnerId;
	Record.SchemaVersion = SchemaVersion;
	Record.Revision = Revision;
	Record.PayloadJson = IMT2Persistable::Execute_CapturePersistentStateJson(Owner);
	if (Record.PayloadJson.IsEmpty())
	{
		SetError(TEXT("Persistence owner returned an empty payload."));
		return;
	}

	State = EMT2PersistenceState::Saving;
	const uint64 SavedGeneration = DirtyGeneration;
	TWeakObjectPtr<UMT2PersistenceComponent> WeakThis(this);
	Manager->SaveRecord(Record,
		[WeakThis, SavedGeneration](FMT2PersistenceSaveResult&& Result)
		{
			UMT2PersistenceComponent* Component = WeakThis.Get();
			if (!Component)
			{
				return;
			}
			if (!Result.bSucceeded)
			{
				Component->OnSaveFinishedNative.Broadcast(false, 0, Result.Error);
				Component->SetError(Result.Error);
				return;
			}
			Component->Revision = Result.Revision;
			Component->bDirty = Component->DirtyGeneration != SavedGeneration;
			Component->State = EMT2PersistenceState::Ready;
			if (Component->bDirty)
			{
				Component->RequestSave(true);
				return;
			}
			Component->OnSaved.Broadcast(Result.Revision);
			Component->OnSaveFinishedNative.Broadcast(true, Result.Revision, FString());
			Component->CompleteFlushes(true, FString());
		});
}

void UMT2PersistenceComponent::MarkDirty()
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		bDirty = true;
		++DirtyGeneration;
		if (State == EMT2PersistenceState::Error)
		{
			State = EMT2PersistenceState::Ready;
		}
	}
}

void UMT2PersistenceComponent::Flush(TFunction<void(bool, const FString&)> Completion)
{
	if (!Completion) return;
	if (!GetOwner() || !GetOwner()->HasAuthority() || State == EMT2PersistenceState::Disabled ||
		EntityId.IsEmpty())
	{
		Completion(false, TEXT("Persistence component is unavailable or has no identity."));
		return;
	}
	PendingFlushes.Add(MoveTemp(Completion));
	if (State != EMT2PersistenceState::Loading && State != EMT2PersistenceState::Saving)
	{
		RequestSave(true);
	}
}

void UMT2PersistenceComponent::StartAutosave()
{
	if (!GetWorld())
	{
		return;
	}
	GetWorld()->GetTimerManager().ClearTimer(AutosaveTimer);
	float DefaultInterval = 60.0f;
	if (const UGameInstance* GameInstance = GetWorld()->GetGameInstance())
	{
		if (const UMT2ServerRuntimeSubsystem* Runtime = GameInstance->GetSubsystem<UMT2ServerRuntimeSubsystem>())
		{
			DefaultInterval = Runtime->GetConfig().AutosaveIntervalSeconds;
		}
	}
	const float Interval = AutosaveInterval > 0.0f ? AutosaveInterval : DefaultInterval;
	if (Interval > 0.0f && !EntityId.IsEmpty())
	{
		GetWorld()->GetTimerManager().SetTimer(
			AutosaveTimer, this, &UMT2PersistenceComponent::HandleAutosave, Interval, true);
	}
}

void UMT2PersistenceComponent::SetError(const FString& Error)
{
	State = EMT2PersistenceState::Error;
	OnError.Broadcast(Error);
	CompleteFlushes(false, Error);
	UE_LOG(LogTemp, Error, TEXT("MT2 persistence: %s"), *Error);
}

void UMT2PersistenceComponent::CompleteFlushes(bool bSucceeded, const FString& Error)
{
	TArray<TFunction<void(bool, const FString&)>> Callbacks = MoveTemp(PendingFlushes);
	PendingFlushes.Reset();
	for (TFunction<void(bool, const FString&)>& Callback : Callbacks)
	{
		if (Callback) Callback(bSucceeded, Error);
	}
}

void UMT2PersistenceComponent::HandleAutosave()
{
	RequestSave(false);
}
