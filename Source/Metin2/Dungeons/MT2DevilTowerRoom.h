#pragma once
#include "Dungeons/MT2DungeonRoom.h"
#include "MT2DevilTowerRoom.generated.h"

// Opening tower stages. Room-owned transitions replace the old killer-owned quest timer.
UCLASS()
class METIN2_API AMT2DevilTowerRoom : public AMT2DungeonRoom
{
	GENERATED_BODY()
public:
	AMT2DevilTowerRoom();
	virtual bool JoinPlayer(AMT2PlayerState* Player) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Devil Tower", meta=(ClampMin="1", ClampMax="3")) int32 Floor = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Devil Tower", meta=(ClampMin="0.1")) float TransitionSeconds = 6.f;
protected:
	virtual void OnEncounterCompleted() override;
private:
	void QueueAdvance();
	FTimerHandle StartTimer, AdvanceTimer;
};
