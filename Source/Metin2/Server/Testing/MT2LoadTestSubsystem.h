#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MT2LoadTestSubsystem.generated.h"

class AMT2PlayerCharacter;
class AMT2LoadTestController;

UCLASS()
class METIN2_API UMT2LoadTestSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual void Deinitialize() override;
	bool QueuePlayers(AMT2PlayerCharacter* Requester, int32 Count, float Radius, int32 Level, FString& OutMessage);
	int32 ClearPlayers();
	int32 GetPlayerCount() const;
	int32 GetQueuedCount() const;
	void ForgetController(AMT2LoadTestController* Controller);
private:
	friend class FMT2FakePlayersTest;
	struct FRequest
	{
		TWeakObjectPtr<AMT2PlayerCharacter> Requester;
		FVector Origin = FVector::ZeroVector;
		float Radius = 3000.f;
		int32 Level = 1;
		int32 Remaining = 0;
		int32 Spawned = 0;
		int32 Failed = 0;
	};
	bool SpawnPlayer(const FRequest& Request);
	TArray<FRequest> Requests;
	TSet<TWeakObjectPtr<AMT2LoadTestController>> Controllers;
	TArray<TWeakObjectPtr<AMT2LoadTestController>> Snapshot;
	uint64 NextIdentity = 1;
};
