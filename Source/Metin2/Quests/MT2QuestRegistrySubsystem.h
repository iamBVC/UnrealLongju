/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MT2QuestRegistrySubsystem.generated.h"

class UMT2Quest;

// Collects every quest Blueprint under /Game/Quests once per process and hands the loaded quest
// defaults to each player's quest manager. Quests are immutable shared definitions, so one cached
// list serves every player (per-player progress lives on UMT2QuestManagerComponent).
//
// Drop a new quest Blueprint into /Game/Quests and it is picked up automatically - no registration
// list to maintain.
UCLASS()
class METIN2_API UMT2QuestRegistrySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// Loaded quest defaults (CDOs). Loads on first use.
	const TArray<TObjectPtr<const UMT2Quest>>& GetQuests();

	// Quest by id, or null.
	const UMT2Quest* FindQuest(FName QuestId);

	// Rescans /Game/Quests. Called on first use, and available for editor iteration.
	UFUNCTION(BlueprintCallable, Category = "Quest")
	void ReloadQuests();

	// Folder scanned for quest Blueprints.
	static const TCHAR* QuestRoot();

private:
	friend class FMT2QuestTargetDispatchTest;
	friend class FMT2QuestTriggerGateTest;
	UPROPERTY(Transient)
	TArray<TObjectPtr<const UMT2Quest>> Quests;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<const UMT2Quest>> QuestsById;

	bool bLoaded = false;
};
