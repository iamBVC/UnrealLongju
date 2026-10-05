/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "Party/MT2PartyTypes.h"
#include "MT2Party.generated.h"

class AMT2PlayerState;

struct FMT2PartyFlagKeyFuncs : TDefaultMapKeyFuncs<FString, int32, false>
{
	static bool Matches(KeyInitType A, KeyInitType B) { return A.Equals(B, ESearchCase::CaseSensitive); }
	static uint32 GetKeyHash(KeyInitType Key) { return FCrc::StrCrc32(*Key); }
};

UCLASS(NotPlaceable)
class METIN2_API AMT2Party : public AInfo
{
	GENERATED_BODY()

public:
	AMT2Party();

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual bool IsNetRelevantFor(
		const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const override;
	bool InitializeParty(AMT2PlayerState* Leader, AMT2PlayerState* FirstMember);
	bool AddMember(AMT2PlayerState* Member);
	void RemoveMember(AMT2PlayerState* Member);
	void DetachLocalMemberForTravel(AMT2PlayerState* Member);
	bool RemoveMemberByCharacterId(const FString& CharacterId);
	void Disband(bool bNotifyCoordinator = true);
	void ApplyCoordinatorSnapshot(
		const FString& InPartyId, const TArray<FMT2PartyMemberData>& InMembers,
		const TArray<AMT2PlayerState*>& LocalMemberStates);

	const FString& GetPartyId() const { return PartyId; }

	UFUNCTION(BlueprintPure, Category = "Party")
	bool ContainsMember(const AMT2PlayerState* Member) const;
	bool ContainsCharacter(const FString& CharacterId) const;

	UFUNCTION(BlueprintPure, Category = "Party")
	bool IsLeader(const AMT2PlayerState* Member) const;

	UFUNCTION(BlueprintPure, Category = "Party")
	int32 GetMemberCount() const { return HasAuthority() ? MemberStates.Num() : Members.Num(); }

	UFUNCTION(BlueprintPure, Category = "Party")
	const TArray<FMT2PartyMemberData>& GetMembers() const { return Members; }

	const TArray<TObjectPtr<AMT2PlayerState>>& GetMemberStates() const { return MemberStates; }
	AMT2PlayerState* ClaimNextLootRecipient();
	int32 GetQuestFlag(FName FlagName) const { return GetScriptFlag(FlagName.ToString()); }
	void SetQuestFlag(FName FlagName, int32 Value);
	int32 GetScriptFlag(const FString& FlagName) const { return QuestFlags.FindRef(FlagName); }
	void SetScriptFlag(const FString& FlagName, int32 Value);

	UPROPERTY(BlueprintAssignable, Category = "Party")
	FMT2PartyMembersChangedSignature OnMembersChanged;

private:
	void RefreshMemberData(bool bForce = false);
	void PublishSnapshot() const;
	FMT2PartyMemberData MakeMemberData(AMT2PlayerState* State, bool bLeader) const;

	UFUNCTION()
	void OnRep_Members();

	UPROPERTY(Replicated)
	TObjectPtr<AMT2PlayerState> LeaderState;

	UPROPERTY(Replicated)
	FString PartyId;

	UPROPERTY()
	TArray<TObjectPtr<AMT2PlayerState>> MemberStates;

	UPROPERTY(ReplicatedUsing = OnRep_Members)
	TArray<FMT2PartyMemberData> Members;

	int32 NextLootRecipientIndex = 0;

	// Server-runtime quest state shared by the current party. Dungeon migration will serialize this
	// alongside the party snapshot when instanced dungeons are introduced.
	TMap<FString, int32, FDefaultSetAllocator, FMT2PartyFlagKeyFuncs> QuestFlags;
};
