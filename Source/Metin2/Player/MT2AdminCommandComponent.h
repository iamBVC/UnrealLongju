/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "MT2AdminCommandComponent.generated.h"

class AMT2PlayerCharacter;

// GM/admin chat commands (/m, /i, /xp, ...), split out of AMT2PlayerCharacter. The character's
// ServerExecuteChatCommand RPC does the admin-rights check and then hands the raw line here, where
// a single dispatch table maps each command to its handler - so adding a command is one table row
// plus one method, instead of another branch in a growing if/else chain. Server-side only.
UCLASS(ClassGroup = "MT2", meta = (BlueprintSpawnableComponent))
class METIN2_API UMT2AdminCommandComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMT2AdminCommandComponent();

	// Parses and runs one command line (already confirmed to come from an admin). Returns false and
	// logs usage if the command is unknown or missing arguments.
	bool Execute(const FString& CommandLine);

private:
	// Handlers receive the whole token list, Args[0] being the command word itself.
	void CmdSpawnMob(const TArray<FString>& Args);
	void CmdGiveItem(const TArray<FString>& Args);
	void CmdAddExperience(const TArray<FString>& Args);
	void CmdAddMoney(const TArray<FString>& Args);
	void CmdApplyEffect(const TArray<FString>& Args);
	void CmdSetSkillGroup(const TArray<FString>& Args);
	void CmdSetSkill(const TArray<FString>& Args);
	void CmdSetRace(const TArray<FString>& Args);
	void CmdSetGender(const TArray<FString>& Args);
	void CmdSetStyle(const TArray<FString>& Args);
	void CmdGoto(const TArray<FString>& Args);
	void CmdTeleport(const TArray<FString>& Args);
	void CmdPersist(const TArray<FString>& Args);
	void CmdShutdown(const TArray<FString>& Args);
	void CmdReboot(const TArray<FString>& Args);
	void CmdServers(const TArray<FString>& Args);
	void CmdTogglePk(const TArray<FString>& Args);
	void CmdTeleportAll(const TArray<FString>& Args);
	void CmdNetworkProfiler(const TArray<FString>& Args);
	void CmdStats(const TArray<FString>& Args);
	void CmdItemList(const TArray<FString>& Args);
	void CmdMobList(const TArray<FString>& Args);
	void SendResult(bool bSucceeded, const FString& Message) const;

	AMT2PlayerCharacter* GetPlayer() const;

	// One dispatch-table row. MinTokens counts the command word too (so "/xp <n>" needs 2).
	struct FCommand
	{
		const TCHAR* Name;
		int32 MinTokens;
		void (UMT2AdminCommandComponent::*Handler)(const TArray<FString>&);
		const TCHAR* Usage;
	};
	static const TArray<FCommand>& GetCommandTable();
};
