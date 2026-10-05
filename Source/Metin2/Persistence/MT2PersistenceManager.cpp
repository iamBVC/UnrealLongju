/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Persistence/MT2PersistenceManager.h"

#include "Async/Async.h"
#include "Authentication/MT2AuthenticationUtils.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Persistence/MT2PersistenceBackend.h"
#include "Server/MT2ServerRuntimeSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogMT2Persistence, Log, All);

void UMT2PersistenceManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UMT2ServerRuntimeSubsystem>();
	bShuttingDown.Store(false);
	const UMT2ServerRuntimeSubsystem* Runtime = GetGameInstance()->GetSubsystem<UMT2ServerRuntimeSubsystem>();
	if (!Runtime || !Runtime->IsCoordinator())
	{
		Backend.Reset();
		return;
	}

	const FMT2ServerRuntimeConfig& RuntimeConfig = Runtime->GetConfig();
	FMT2PersistenceBackendConfig Config;
	Config.RootDirectory = RuntimeConfig.DatabaseRoot;
	Config.Filename = RuntimeConfig.DatabaseFile;
	Config.SynchronousMode = RuntimeConfig.DatabaseSynchronousMode;
	Config.BusyTimeoutMilliseconds = RuntimeConfig.DatabaseBusyTimeoutMilliseconds;
	Config.bCheckIntegrity = RuntimeConfig.bCheckDatabaseIntegrity;

	TSharedPtr<FMT2PersistenceBackend, ESPMode::ThreadSafe> NewBackend =
		MakeShared<FMT2PersistenceBackend, ESPMode::ThreadSafe>(MoveTemp(Config));
	FString Error;
	if (!NewBackend->Initialize(Error))
	{
		UE_LOG(LogMT2Persistence, Error, TEXT("Coordinator SQLite startup failed: %s"), *Error);
		Backend.Reset();
		return;
	}
	Backend = MoveTemp(NewBackend);
	UE_LOG(LogMT2Persistence, Display, TEXT("Coordinator unified SQLite persistence enabled."));
}

void UMT2PersistenceManager::Deinitialize()
{
	bShuttingDown.Store(true);
	Backend.Reset();
	Super::Deinitialize();
}

bool UMT2PersistenceManager::IsPersistenceEnabled() const
{
	return CanIssueRequests();
}

UMT2PersistenceManager* UMT2PersistenceManager::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UMT2PersistenceManager>() : nullptr;
}

void UMT2PersistenceManager::LoadRecord(
	FName EntityType, const FString& EntityId,
	TFunction<void(FMT2PersistenceLoadResult&&)> Completion)
{
	if (!CanIssueRequests() || EntityType.IsNone() || EntityId.IsEmpty())
	{
		FMT2PersistenceLoadResult Result;
		Result.Error = TEXT("Persistence load rejected: manager disabled or identity invalid.");
		Completion(MoveTemp(Result));
		return;
	}
	UMT2ServerRuntimeSubsystem* Runtime = GetGameInstance()->GetSubsystem<UMT2ServerRuntimeSubsystem>();
	if (Runtime && Runtime->IsMapServer())
	{
		Runtime->RequestPersistenceLoad(EntityType, EntityId, MoveTemp(Completion));
		return;
	}
	const TSharedPtr<FMT2PersistenceBackend, ESPMode::ThreadSafe> ActiveBackend = Backend;
	TWeakObjectPtr<UMT2PersistenceManager> WeakThis(this);
	Async(EAsyncExecution::ThreadPool,
		[ActiveBackend, WeakThis, EntityType, EntityId, Completion = MoveTemp(Completion)]() mutable
		{
			FMT2PersistenceLoadResult Result = ActiveBackend->Load(EntityType, EntityId);
			AsyncTask(ENamedThreads::GameThread,
				[WeakThis, Result = MoveTemp(Result), Completion = MoveTemp(Completion)]() mutable
				{
					if (WeakThis.IsValid() && !WeakThis->bShuttingDown.Load()) Completion(MoveTemp(Result));
				});
		});
}

void UMT2PersistenceManager::SaveRecord(
	const FMT2PersistentRecord& Record,
	TFunction<void(FMT2PersistenceSaveResult&&)> Completion)
{
	if (!CanIssueRequests() || !Record.IsValid() || Record.PayloadJson.IsEmpty())
	{
		FMT2PersistenceSaveResult Result;
		Result.Error = TEXT("Persistence save rejected: manager disabled or record invalid.");
		Completion(MoveTemp(Result));
		return;
	}
	UMT2ServerRuntimeSubsystem* Runtime = GetGameInstance()->GetSubsystem<UMT2ServerRuntimeSubsystem>();
	if (Runtime && Runtime->IsMapServer())
	{
		Runtime->RequestPersistenceSave(Record, MoveTemp(Completion));
		return;
	}
	const TSharedPtr<FMT2PersistenceBackend, ESPMode::ThreadSafe> ActiveBackend = Backend;
	TWeakObjectPtr<UMT2PersistenceManager> WeakThis(this);
	Async(EAsyncExecution::ThreadPool,
		[ActiveBackend, WeakThis, Record, Completion = MoveTemp(Completion)]() mutable
		{
			FMT2PersistenceSaveResult Result = ActiveBackend->Save(Record);
			AsyncTask(ENamedThreads::GameThread,
				[WeakThis, Result = MoveTemp(Result), Completion = MoveTemp(Completion)]() mutable
				{
					if (WeakThis.IsValid() && !WeakThis->bShuttingDown.Load()) Completion(MoveTemp(Result));
				});
		});
}

void UMT2PersistenceManager::RegisterAccount(
	const FString& Username, const FString& Password,
	TFunction<void(FMT2AccountRegistrationResult&&)> Completion)
{
	FString ValidationError;
	if (!Backend.IsValid() || !MT2Authentication::ValidateUsername(Username, ValidationError) ||
		!MT2Authentication::ValidatePassword(Password, ValidationError))
	{
		FMT2AccountRegistrationResult Result;
		Result.Error = ValidationError.IsEmpty() ? TEXT("Account registration is unavailable.") : ValidationError;
		Completion(MoveTemp(Result));
		return;
	}
	const TSharedPtr<FMT2PersistenceBackend, ESPMode::ThreadSafe> ActiveBackend = Backend;
	TWeakObjectPtr<UMT2PersistenceManager> WeakThis(this);
	Async(EAsyncExecution::ThreadPool,
		[ActiveBackend, WeakThis, Username, Password, Completion = MoveTemp(Completion)]() mutable
		{
			FMT2AccountRegistrationResult Result;
			FString Salt;
			FString Hash;
			if (!MT2Authentication::HashPassword(Password, Salt, Hash)) Result.Error = TEXT("Password hashing failed.");
			else Result = ActiveBackend->RegisterAccount(Username, Salt, Hash);
			AsyncTask(ENamedThreads::GameThread,
				[WeakThis, Result = MoveTemp(Result), Completion = MoveTemp(Completion)]() mutable
				{
					if (WeakThis.IsValid() && !WeakThis->bShuttingDown.Load()) Completion(MoveTemp(Result));
				});
		});
}

void UMT2PersistenceManager::AuthenticateAccount(
	const FString& Username, const FString& Password,
	TFunction<void(FMT2AccountLoginResult&&)> Completion)
{
	FString ValidationError;
	if (!Backend.IsValid() || !MT2Authentication::ValidateUsername(Username, ValidationError) ||
		!MT2Authentication::ValidatePassword(Password, ValidationError))
	{
		FMT2AccountLoginResult Result;
		Result.Error = TEXT("Invalid username or password.");
		Completion(MoveTemp(Result));
		return;
	}
	const TSharedPtr<FMT2PersistenceBackend, ESPMode::ThreadSafe> ActiveBackend = Backend;
	TWeakObjectPtr<UMT2PersistenceManager> WeakThis(this);
	Async(EAsyncExecution::ThreadPool,
		[ActiveBackend, WeakThis, Username, Password, Completion = MoveTemp(Completion)]() mutable
		{
			FMT2AccountLoginResult Result = ActiveBackend->AuthenticateAccount(Username, Password);
			AsyncTask(ENamedThreads::GameThread,
				[WeakThis, Result = MoveTemp(Result), Completion = MoveTemp(Completion)]() mutable
				{
					if (WeakThis.IsValid() && !WeakThis->bShuttingDown.Load()) Completion(MoveTemp(Result));
				});
		});
}

void UMT2PersistenceManager::CreateCharacter(
	const FString& AccountId, const FString& CharacterName,
	const FMT2CharacterAppearance& Appearance, EMT2Empire Empire, const FString& InitialMapId,
	TFunction<void(FMT2CharacterCreateResult&&)> Completion)
{
	const bool bAppearanceValid =
		static_cast<uint8>(Appearance.Race) <= static_cast<uint8>(EMT2CharacterRace::Shaman) &&
		static_cast<uint8>(Appearance.Sex) <= static_cast<uint8>(EMT2CharacterSex::Female) &&
		static_cast<uint8>(Appearance.Style) <= static_cast<uint8>(EMT2CharacterStyle::Blue) &&
		Empire >= EMT2Empire::Shinsoo && Empire <= EMT2Empire::Jinno;
	if (!Backend.IsValid() || AccountId.IsEmpty() || !bAppearanceValid)
	{
		FMT2CharacterCreateResult Result;
		Result.Error = bAppearanceValid ? TEXT("Character creation is unavailable.")
			: TEXT("Character appearance or empire is invalid.");
		Completion(MoveTemp(Result));
		return;
	}
	const TSharedPtr<FMT2PersistenceBackend, ESPMode::ThreadSafe> ActiveBackend = Backend;
	TWeakObjectPtr<UMT2PersistenceManager> WeakThis(this);
	Async(EAsyncExecution::ThreadPool,
		[ActiveBackend, WeakThis, AccountId, CharacterName, Appearance, Empire, InitialMapId,
		 Completion = MoveTemp(Completion)]() mutable
		{
			FMT2CharacterCreateResult Result = ActiveBackend->CreateCharacter(
				AccountId, CharacterName, Appearance, Empire, InitialMapId);
			AsyncTask(ENamedThreads::GameThread,
				[WeakThis, Result = MoveTemp(Result), Completion = MoveTemp(Completion)]() mutable
				{
					if (WeakThis.IsValid() && !WeakThis->bShuttingDown.Load()) Completion(MoveTemp(Result));
				});
		});
}

void UMT2PersistenceManager::FindCharacter(
	const FString& AccountId, const FString& CharacterId,
	TFunction<void(FMT2CharacterCreateResult&&)> Completion)
{
	if (!Backend.IsValid() || AccountId.IsEmpty() || CharacterId.IsEmpty())
	{
		FMT2CharacterCreateResult Result;
		Result.Error = TEXT("Character lookup is unavailable.");
		Completion(MoveTemp(Result));
		return;
	}
	const TSharedPtr<FMT2PersistenceBackend, ESPMode::ThreadSafe> ActiveBackend = Backend;
	TWeakObjectPtr<UMT2PersistenceManager> WeakThis(this);
	Async(EAsyncExecution::ThreadPool,
		[ActiveBackend, WeakThis, AccountId, CharacterId, Completion = MoveTemp(Completion)]() mutable
		{
			FMT2CharacterCreateResult Result = ActiveBackend->FindCharacter(AccountId, CharacterId);
			AsyncTask(ENamedThreads::GameThread,
				[WeakThis, Result = MoveTemp(Result), Completion = MoveTemp(Completion)]() mutable
				{
					if (WeakThis.IsValid() && !WeakThis->bShuttingDown.Load()) Completion(MoveTemp(Result));
				});
		});
}

void UMT2PersistenceManager::UpdateCharacterLocation(
	const FString& CharacterId, const FString& MapId, int32 Channel,
	TFunction<void(bool, FString&&)> Completion)
{
	if (!Backend.IsValid() || CharacterId.IsEmpty() || MapId.IsEmpty())
	{
		Completion(false, FString(TEXT("Character location update is unavailable.")));
		return;
	}
	const TSharedPtr<FMT2PersistenceBackend, ESPMode::ThreadSafe> ActiveBackend = Backend;
	TWeakObjectPtr<UMT2PersistenceManager> WeakThis(this);
	Async(EAsyncExecution::ThreadPool,
		[ActiveBackend, WeakThis, CharacterId, MapId, Channel, Completion = MoveTemp(Completion)]() mutable
		{
			FString Error;
			const bool bSucceeded = ActiveBackend->UpdateCharacterLocation(CharacterId, MapId, Channel, Error);
			AsyncTask(ENamedThreads::GameThread,
				[WeakThis, bSucceeded, Error = MoveTemp(Error), Completion = MoveTemp(Completion)]() mutable
				{
					if (WeakThis.IsValid() && !WeakThis->bShuttingDown.Load())
					{
						Completion(bSucceeded, MoveTemp(Error));
					}
				});
		});
}

void UMT2PersistenceManager::FlushDatabase(TFunction<void(bool, FString&&)> Completion)
{
	if (!Backend.IsValid())
	{
		Completion(false, FString(TEXT("Coordinator database is unavailable.")));
		return;
	}
	const TSharedPtr<FMT2PersistenceBackend, ESPMode::ThreadSafe> ActiveBackend = Backend;
	TWeakObjectPtr<UMT2PersistenceManager> WeakThis(this);
	Async(EAsyncExecution::ThreadPool,
		[ActiveBackend, WeakThis, Completion = MoveTemp(Completion)]() mutable
		{
			FString Error;
			const bool bSucceeded = ActiveBackend->Flush(Error);
			AsyncTask(ENamedThreads::GameThread,
				[WeakThis, bSucceeded, Error = MoveTemp(Error), Completion = MoveTemp(Completion)]() mutable
				{
					if (WeakThis.IsValid() && !WeakThis->bShuttingDown.Load())
					{
						Completion(bSucceeded, MoveTemp(Error));
					}
				});
		});
}

bool UMT2PersistenceManager::CanIssueRequests() const
{
	if (bShuttingDown.Load()) return false;
	const UMT2ServerRuntimeSubsystem* Runtime = GetGameInstance()->GetSubsystem<UMT2ServerRuntimeSubsystem>();
	return Runtime && (Runtime->IsMapServer() || (Runtime->IsCoordinator() && Backend.IsValid()));
}
