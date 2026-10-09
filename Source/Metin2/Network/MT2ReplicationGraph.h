/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#pragma once

#include "CoreMinimal.h"
#include "ReplicationGraph.h"
#include "MT2ReplicationGraph.generated.h"

class UNetConnection;
class UNetReplicationGraphConnection;
class UReplicationGraphNode_ActorList;
class UReplicationGraphNode_AlwaysRelevant_ForConnection;
class UReplicationGraphNode_GridSpatialization2D;

USTRUCT()
struct FMT2ConnectionAlwaysRelevantNodePair
{
	GENERATED_BODY()

	FMT2ConnectionAlwaysRelevantNodePair() = default;
	FMT2ConnectionAlwaysRelevantNodePair(
		UNetConnection* InConnection,
		UReplicationGraphNode_AlwaysRelevant_ForConnection* InNode)
		: NetConnection(InConnection), Node(InNode)
	{
	}

	bool operator==(UNetConnection* Connection) const;

	UPROPERTY()
	TObjectPtr<UNetConnection> NetConnection;

	UPROPERTY()
	TObjectPtr<UReplicationGraphNode_AlwaysRelevant_ForConnection> Node;
};

/**
 * Project replication graph. Mobs use a dedicated spatial grid with state-driven update periods
 * and immediate forced updates. Other actors retain the standard spatial,
 * always-relevant and owner-relevant routing rules.
 */
UCLASS(Transient, Config = Engine)
class METIN2_API UMT2ReplicationGraph : public UReplicationGraph
{
	GENERATED_BODY()

public:
	void SetMobReplicationFrequency(AActor* Actor, float Frequency);
	void SetPlayerReplicationFrequency(AActor* Actor, float Frequency);
	virtual void InitGlobalActorClassSettings() override;
	virtual void InitGlobalGraphNodes() override;
	virtual void InitConnectionGraphNodes(
		UNetReplicationGraphConnection* RepGraphConnection) override;
	virtual void RouteAddNetworkActorToNodes(
		const FNewReplicatedActorInfo& ActorInfo,
		FGlobalActorReplicationInfo& GlobalInfo) override;
	virtual void RouteRemoveNetworkActorToNodes(
		const FNewReplicatedActorInfo& ActorInfo) override;
	virtual int32 ServerReplicateActors(float DeltaSeconds) override;

protected:
	virtual void RouteRenameNetworkActorToNodes(
		const FRenamedReplicatedActorInfo& ActorInfo) override;

private:
	void SetActorReplicationFrequency(AActor* Actor, float Frequency);
	UReplicationGraphNode_AlwaysRelevant_ForConnection* GetAlwaysRelevantNodeForConnection(
		UNetConnection* Connection) const;

	UPROPERTY()
	TObjectPtr<UReplicationGraphNode_GridSpatialization2D> SpatialGridNode;

	UPROPERTY()
	TObjectPtr<UReplicationGraphNode_GridSpatialization2D> MobGridNode;

	UPROPERTY()
	TObjectPtr<UReplicationGraphNode_ActorList> AlwaysRelevantNode;

	UPROPERTY()
	TArray<FMT2ConnectionAlwaysRelevantNodePair> AlwaysRelevantForConnectionList;

	UPROPERTY()
	TArray<TObjectPtr<AActor>> ActorsWithoutNetConnection;
};
