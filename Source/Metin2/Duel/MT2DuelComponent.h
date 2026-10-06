#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TimerManager.h"
#include "MT2DuelComponent.generated.h"

class AMT2PlayerState;

UENUM(BlueprintType)
enum class EMT2DuelPhase : uint8 { Challenge, Fighting, Revenge };

USTRUCT(BlueprintType)
struct METIN2_API FMT2DuelEntry
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) TObjectPtr<AMT2PlayerState> Opponent = nullptr;
	UPROPERTY(BlueprintReadOnly) EMT2DuelPhase Phase = EMT2DuelPhase::Challenge;
	UPROPERTY(BlueprintReadOnly) bool bCanAccept = false;
	// Server bookkeeping only; combat activity does not generate replicated updates.
	double LastActivity = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMT2DuelsChangedSignature);

// Session/map-local agreements, not persisted character data. Each participant owns a view.
UCLASS(ClassGroup="MT2", meta=(BlueprintSpawnableComponent))
class METIN2_API UMT2DuelComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UMT2DuelComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	bool RequestDuel(AMT2PlayerState* Opponent, FString& Error);
	bool CancelDuel(AMT2PlayerState* Opponent);
	void CancelAllDuels();
	bool IsFighting(const AMT2PlayerState* Opponent) const;
	const FMT2DuelEntry* FindDuel(const AMT2PlayerState* Opponent) const;
	void RecordHit(AMT2PlayerState* Opponent);
	// Called synchronously at death, before ordinary PK penalties. Includes the legacy 15s grace.
	bool HandleDefeat(AMT2PlayerState* Killer);
	UPROPERTY(BlueprintAssignable) FMT2DuelsChangedSignature OnDuelsChanged;
private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FMT2DuelLifecycleTest;
#endif
	AMT2PlayerState* PlayerState() const;
	FMT2DuelEntry* FindMutable(const AMT2PlayerState* Opponent);
	void Changed();
	void CleanupExpiredDuels();
	UFUNCTION() void OnRep_Duels();
	UPROPERTY(Transient, ReplicatedUsing=OnRep_Duels) TArray<FMT2DuelEntry> Duels;
	FTimerHandle CleanupTimer;
	double LastRequestTime = -1.e30;
};
