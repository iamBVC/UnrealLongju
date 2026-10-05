/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Server/MT2AdminSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Persistence/MT2PersistenceManager.h"

namespace
{
	const FName AdminEntityType(TEXT("admin"));
}

FString UMT2AdminSubsystem::NormalizeName(const FString& CharacterName)
{
	return CharacterName.TrimStartAndEnd().ToLower();
}

bool UMT2AdminSubsystem::IsPIEWorld(const UWorld* World)
{
	return World && World->WorldType == EWorldType::PIE;
}

void UMT2AdminSubsystem::QueryAdminStatus(
	const UWorld* World, const FString& CharacterName, TFunction<void(bool)> Completion)
{
	if (IsPIEWorld(World))
	{
		Completion(true);
		return;
	}

	const FString Key = NormalizeName(CharacterName);
	UMT2PersistenceManager* Persistence = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMT2PersistenceManager>() : nullptr;
	if (Key.IsEmpty() || !Persistence || !Persistence->IsPersistenceEnabled())
	{
		Completion(false);
		return;
	}

	Persistence->LoadRecord(AdminEntityType, Key,
		[Completion = MoveTemp(Completion)](FMT2PersistenceLoadResult&& Result)
		{
			Completion(Result.bSucceeded && Result.bFound);
		});
}

void UMT2AdminSubsystem::SetAdmin(const FString& CharacterName, bool bAdmin)
{
	const FString Key = NormalizeName(CharacterName);
	UMT2PersistenceManager* Persistence = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UMT2PersistenceManager>() : nullptr;
	if (Key.IsEmpty() || !Persistence || !Persistence->IsPersistenceEnabled())
	{
		return;
	}

	if (bAdmin)
	{
		FMT2PersistentRecord Record;
		Record.EntityType = AdminEntityType;
		Record.EntityId = Key;
		Record.PayloadJson = TEXT("{\"authority\":\"implementor\"}");
		Persistence->SaveRecord(Record, [](FMT2PersistenceSaveResult&&) {});
	}
	else
	{
		// Revocation writes an empty payload for now; the lookup treats any existing record as
		// admin, so a real delete API on the persistence manager is the eventual cleanup path.
		UE_LOG(LogTemp, Warning,
			TEXT("[MT2Admin] Revoking '%s' requires deleting its row from the admins database."), *Key);
	}
}
