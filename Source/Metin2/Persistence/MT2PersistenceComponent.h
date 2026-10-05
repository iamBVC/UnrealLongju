/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Persistence/MT2PersistenceTypes.h"
#include "MT2PersistenceComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMT2PersistenceLoadedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMT2PersistenceSavedSignature, int64, Revision);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMT2PersistenceErrorSignature, const FString&, Error);
DECLARE_MULTICAST_DELEGATE_FourParams(
	FMT2PersistenceLoadFinishedNative, bool /*bSucceeded*/, bool /*bFound*/,
	bool /*bHasWorldPosition*/, const FString& /*Error*/);
DECLARE_MULTICAST_DELEGATE_ThreeParams(
	FMT2PersistenceSaveFinishedNative, bool /*bSucceeded*/, int64 /*Revision*/, const FString& /*Error*/);

UCLASS(BlueprintType, ClassGroup = "MT2", meta = (BlueprintSpawnableComponent))
class METIN2_API UMT2PersistenceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMT2PersistenceComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Persistence")
	void ConfigurePersistence(FName InEntityType, const FString& InEntityId, const FString& InOwnerId);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Persistence")
	void RequestLoad();

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Persistence")
	void RequestSave(bool bForce = false);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Persistence")
	void MarkDirty();

	// Forces the newest actor state to durable storage. If a load/save is already running, the
	// callback waits for it and any follow-up dirty save instead of silently skipping the request.
	void Flush(TFunction<void(bool, const FString&)> Completion);

	UFUNCTION(BlueprintPure, Category = "Persistence")
	EMT2PersistenceState GetPersistenceState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "Persistence")
	bool IsDirty() const { return bDirty; }

	UFUNCTION(BlueprintPure, Category = "Persistence")
	const FString& GetEntityId() const { return EntityId; }

	UFUNCTION(BlueprintPure, Category = "Persistence")
	const FString& GetOwnerId() const { return OwnerId; }

	UPROPERTY(BlueprintAssignable, Category = "Persistence")
	FMT2PersistenceLoadedSignature OnLoaded;

	UPROPERTY(BlueprintAssignable, Category = "Persistence")
	FMT2PersistenceSavedSignature OnSaved;

	UPROPERTY(BlueprintAssignable, Category = "Persistence")
	FMT2PersistenceErrorSignature OnError;

	FMT2PersistenceLoadFinishedNative OnLoadFinishedNative;
	FMT2PersistenceSaveFinishedNative OnSaveFinishedNative;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Persistence")
	FName EntityType = TEXT("actor");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Persistence")
	FString EntityId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Persistence")
	FString OwnerId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Persistence", meta = (ClampMin = "1"))
	int32 SchemaVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Persistence")
	bool bAutoLoad = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Persistence")
	bool bCreateIfMissing = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Persistence")
	bool bSaveOnEndPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Persistence", meta = (ClampMin = "0.0", Units = "s"))
	float AutosaveInterval = 0.0f;

private:
	void StartAutosave();
	void SetError(const FString& Error);
	void CompleteFlushes(bool bSucceeded, const FString& Error);

	UFUNCTION()
	void HandleAutosave();

	UPROPERTY(Transient)
	EMT2PersistenceState State = EMT2PersistenceState::Disabled;

	bool bDirty = true;
	bool bBegunPlay = false;
	uint64 DirtyGeneration = 1;
	int64 Revision = 0;
	FTimerHandle AutosaveTimer;
	TArray<TFunction<void(bool, const FString&)>> PendingFlushes;
};
