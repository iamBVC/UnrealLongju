/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Persistence/MT2PersistenceTypes.h"
#include "Server/MT2ServerRuntimeTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MT2PersistenceManager.generated.h"

class FMT2PersistenceBackend;

UCLASS()
class METIN2_API UMT2PersistenceManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure, Category = "Persistence")
	bool IsPersistenceEnabled() const;

	UFUNCTION(BlueprintPure, Category = "Persistence", meta = (WorldContext = "WorldContextObject"))
	static UMT2PersistenceManager* Get(const UObject* WorldContextObject);

	void LoadRecord(
		FName EntityType,
		const FString& EntityId,
		TFunction<void(FMT2PersistenceLoadResult&&)> Completion);

	void SaveRecord(
		const FMT2PersistentRecord& Record,
		TFunction<void(FMT2PersistenceSaveResult&&)> Completion);

	void RegisterAccount(
		const FString& Username, const FString& Password,
		TFunction<void(FMT2AccountRegistrationResult&&)> Completion);

	void AuthenticateAccount(
		const FString& Username, const FString& Password,
		TFunction<void(FMT2AccountLoginResult&&)> Completion);

	void CreateCharacter(
		const FString& AccountId, const FString& CharacterName,
		const FMT2CharacterAppearance& Appearance, EMT2Empire Empire,
		const FString& InitialMapId,
		TFunction<void(FMT2CharacterCreateResult&&)> Completion);

	void FindCharacter(
		const FString& AccountId, const FString& CharacterId,
		TFunction<void(FMT2CharacterCreateResult&&)> Completion);

	void UpdateCharacterLocation(
		const FString& CharacterId, const FString& MapId, int32 Channel,
		TFunction<void(bool, FString&&)> Completion);

	void FlushDatabase(TFunction<void(bool, FString&&)> Completion);

	// Direct access to the database, for the coordinator process that owns it. The async methods above
	// exist because map servers talk to the database over the network; code running *inside* the
	// coordinator is already next to the file, and the messenger's reads are small indexed lookups
	// made while handling a peer message. Returns null anywhere the backend is not local.
	FMT2PersistenceBackend* GetLocalBackend() const { return Backend.Get(); }

private:
	bool CanIssueRequests() const;

	TSharedPtr<FMT2PersistenceBackend, ESPMode::ThreadSafe> Backend;
	TAtomic<bool> bShuttingDown = false;
};
