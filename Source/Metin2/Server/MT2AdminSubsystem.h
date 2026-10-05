/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MT2AdminSubsystem.generated.h"

// Server-side admin (GM) registry, mirroring the old game's gm.cpp: admins live in a separate
// `admins` table in the coordinator's unified SQLite database, keyed by lowercased character name
// just like the old name-keyed g_map_GM. PIE sessions grant admin to everyone, replicating the
// old server's "if (test_server) return GM_IMPLEMENTOR" test-server rule.
UCLASS()
class METIN2_API UMT2AdminSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// Async lookup: Completion(true) if the character name has an admin record. Runs the completion
	// on the game thread. PIE worlds complete synchronously with true.
	void QueryAdminStatus(
		const UWorld* World, const FString& CharacterName, TFunction<void(bool)> Completion);

	// Authority-side registry edit so admins can be managed in-game later ("/gm add" style).
	void SetAdmin(const FString& CharacterName, bool bAdmin);

	static bool IsPIEWorld(const UWorld* World);

private:
	static FString NormalizeName(const FString& CharacterName);
};
