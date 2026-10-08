#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Fishing/MT2FishingSettings.h"
#include "TimerManager.h"
#include "MT2FishingComponent.generated.h"

class AMT2PlayerCharacter;
class AMT2MapPresentationActor;

UENUM(BlueprintType)
enum class EMT2FishingPhase : uint8 { Idle, Waiting, Bite };
UENUM(BlueprintType)
enum class EMT2FishingEvent : uint8 { Cast, Bite, Caught, Failed, Cancelled };
USTRUCT(BlueprintType)
struct METIN2_API FMT2FishingState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) EMT2FishingPhase Phase = EMT2FishingPhase::Idle;
	UPROPERTY() uint32 Session = 0;
	UPROPERTY(BlueprintReadOnly) FVector HookLocation = FVector::ZeroVector;
};
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMT2FishingEventSignature, EMT2FishingEvent, Event, int32, CaughtVnum);

UCLASS(BlueprintType, ClassGroup="MT2", meta=(BlueprintSpawnableComponent))
class METIN2_API UMT2FishingComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UMT2FishingComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	UFUNCTION(BlueprintPure, Category="Fishing") bool IsFishing() const { return State.Phase != EMT2FishingPhase::Idle; }
	UFUNCTION(BlueprintPure, Category="Fishing") bool HasRodEquipped() const;
	UFUNCTION(BlueprintPure, Category="Fishing") FMT2FishingState GetFishingState() const { return State; }
	UFUNCTION(BlueprintCallable, Category="Fishing") void RequestToggleFishing();
	UFUNCTION(BlueprintCallable, Category="Fishing") void RequestCancelFishing();
	UFUNCTION(Server, Reliable) void ServerToggleFishing(uint32 ExpectedSession);
	UFUNCTION(Server, Reliable) void ServerCancelFishing(uint32 ExpectedSession);
	// Authority-only cleanup; never resolves a catch.
	void CancelFishing();
	UPROPERTY(BlueprintAssignable, Category="Fishing") FMT2FishingEventSignature OnFishingEvent;
private:
	friend class FMT2FishingAuthorityTest;
	bool StartFishing();
	bool ValidateAttempt() const;
	void Bite();
	void CheckAttempt();
	void ReelIn();
	void Finish(EMT2FishingEvent Event, int32 Vnum=0, bool bPractice=false);
	void Say(const FString& Message) const;
	UFUNCTION() void OnEquipmentChanged();
	UFUNCTION() void OnRep_State();
	UFUNCTION(NetMulticast, Reliable) void MulticastFishingEvent(EMT2FishingEvent Event, int32 Vnum);
	UPROPERTY(ReplicatedUsing=OnRep_State) FMT2FishingState State;
	TWeakObjectPtr<AMT2MapPresentationActor> Map;
	FVector CastOrigin = FVector::ZeroVector;
	int32 RodVnum = 0, BaitPower = 0, TableIndex = INDEX_NONE;
	double BiteTime = 0, LastRequestTime = -DBL_MAX, NextStartTime = 0;
	uint32 SessionCounter = 0;
	FMT2FishingCatch Catch;
	FTimerHandle BiteTimer, GuardTimer, ExpiryTimer;
	bool bCancelRequested = false;
	bool bResolving = false;
};
