/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Server/MT2ServerRuntimeTypes.h"
#include "TimerManager.h"
#include "MT2GameModeBase.generated.h"

UCLASS(Blueprintable)
class METIN2_API AMT2GameModeBase : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMT2GameModeBase();
	virtual void BeginPlay() override;

	virtual void PreLogin(
		const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId,
		FString& ErrorMessage) override;
	virtual void PreLoginAsync(
		const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId,
		const FOnPreLoginCompleteDelegate& OnComplete) override;
	virtual FString InitNewPlayer(
		APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId,
		const FString& Options, const FString& Portal = TEXT("")) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	bool RepairPlayerSpawn(APlayerController* PlayerController, bool bForceTownSpawn) const;

private:
	struct FPendingAdmission
	{
		FMT2TransferClaimResult Result;
		double ExpiresAtSeconds = 0.0;
	};

	void RemoveExpiredAdmissions(double NowSeconds);
	void RecoverPlayersOutsideMap();
	TMap<FString, FPendingAdmission> PendingAdmissions;
	TSet<TWeakObjectPtr<APlayerController>> PendingPIETownSpawns;
	FTimerHandle FallRecoveryTimer;
};
