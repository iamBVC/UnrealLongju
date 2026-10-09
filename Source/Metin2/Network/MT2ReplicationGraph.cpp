/*

- Copyright © 2026 Castellese Brian Vincenzo.
- Licensed under the PolyForm Noncommercial License 1.0.0.
- Commercial use and resale require separate written permission.
- See LICENSE for the complete terms.

*/

#include "Network/MT2ReplicationGraph.h"

#include "Engine/ChildConnection.h"
#include "Mobs/MT2Mob.h"
#include "Mobs/MT2MobRuntimeSettings.h"
#include "UObject/UObjectIterator.h"

namespace
{
	constexpr float SpatialCellSize = 10000.0f;

	bool IsMobSpatialActor(const AActor* Actor)
	{
		return Actor && Actor->IsA<AMT2Mob>() &&
			!Actor->bAlwaysRelevant && !Actor->bOnlyRelevantToOwner;
	}
}

bool FMT2ConnectionAlwaysRelevantNodePair::operator==(UNetConnection* Connection) const
{
	if (Connection && Connection->GetUChildConnection())
	{
		Connection = Connection->GetUChildConnection()->Parent;
	}
	return Connection == NetConnection;
}

void UMT2ReplicationGraph::InitGlobalActorClassSettings()
{
	Super::InitGlobalActorClassSettings();

	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* ActorClass = *It;
		const AActor* ActorCDO = Cast<AActor>(ActorClass->GetDefaultObject());
		if (!ActorCDO || !ActorCDO->GetIsReplicated())
		{
			continue;
		}
		if (ActorClass->GetName().StartsWith(TEXT("SKEL_")) ||
			ActorClass->GetName().StartsWith(TEXT("REINST_")))
		{
			continue;
		}

		FClassReplicationInfo ClassInfo;
		ClassInfo.ReplicationPeriodFrame =
			GetReplicationPeriodFrameForFrequency(IsMobSpatialActor(ActorCDO)
				? GetDefault<UMT2MobRuntimeSettings>()->BaselineMobReplicationRate : ActorCDO->GetNetUpdateFrequency());
		ClassInfo.SetCullDistanceSquared(
			ActorCDO->bAlwaysRelevant || ActorCDO->bOnlyRelevantToOwner
				? 0.0f
				: IsMobSpatialActor(ActorCDO)
					? FMath::Square(GetDefault<UMT2MobRuntimeSettings>()->NetCullDistance)
					: ActorCDO->GetNetCullDistanceSquared());
		GlobalActorReplicationInfoMap.SetClassInfo(ActorClass, ClassInfo);
	}
}

void UMT2ReplicationGraph::InitGlobalGraphNodes()
{
	SpatialGridNode = CreateNewNode<UReplicationGraphNode_GridSpatialization2D>();
	SpatialGridNode->CellSize = SpatialCellSize;
	SpatialGridNode->SpatialBias = FVector2D(-UE_OLD_WORLD_MAX, -UE_OLD_WORLD_MAX);
	AddGlobalGraphNode(SpatialGridNode);

	MobGridNode = CreateNewNode<UReplicationGraphNode_GridSpatialization2D>();
	MobGridNode->CellSize = FMath::Max(GetDefault<UMT2MobRuntimeSettings>()->ReplicationGridCellSize, 100.f);
	MobGridNode->SpatialBias = FVector2D(-UE_OLD_WORLD_MAX, -UE_OLD_WORLD_MAX);
	MobGridNode->CreateCellNodeOverride = [](
		UReplicationGraphNode_GridSpatialization2D* Parent)
	{
		UReplicationGraphNode_GridCell* Cell =
			Parent->CreateChildNode<UReplicationGraphNode_GridCell>();
		Cell->CreateDynamicNodeOverride = [](UReplicationGraphNode_GridCell* CellParent)
		{
			// Standard lists honor ForceNetUpdate and the actor's configured period. Distance/view
			// frequency zones would otherwise silently override the state scheduler's rates.
			return CellParent->CreateChildNode<UReplicationGraphNode_ActorList>();
		};
		return Cell;
	};
	AddGlobalGraphNode(MobGridNode);

	AlwaysRelevantNode = CreateNewNode<UReplicationGraphNode_ActorList>();
	AddGlobalGraphNode(AlwaysRelevantNode);
}

void UMT2ReplicationGraph::SetMobReplicationFrequency(AActor* Actor, float Frequency)
{
	if (!IsMobSpatialActor(Actor)) return;
	const uint16 Period = GetReplicationPeriodFrameForFrequency(FMath::Max(Frequency, 0.1f));
	if (auto* Info = GlobalActorReplicationInfoMap.Find(Actor)) Info->Settings.ReplicationPeriodFrame = Period;
	auto UpdateConnections = [Actor, Period](const auto& List)
	{
		for (const auto& Connection : List)
		{
			if (auto* Info = Connection->ActorInfoMap.Find(Actor))
			{
				Info->ReplicationPeriodFrame = Period;
				Info->NextReplicationFrameNum = FMath::Min(Info->NextReplicationFrameNum, Info->LastRepFrameNum + Period);
			}
		}
	};
	UpdateConnections(Connections);
	UpdateConnections(PendingConnections);
}

void UMT2ReplicationGraph::InitConnectionGraphNodes(
	UNetReplicationGraphConnection* RepGraphConnection)
{
	Super::InitConnectionGraphNodes(RepGraphConnection);

	UReplicationGraphNode_AlwaysRelevant_ForConnection* ConnectionNode =
		CreateNewNode<UReplicationGraphNode_AlwaysRelevant_ForConnection>();
	AddConnectionGraphNode(ConnectionNode, RepGraphConnection);
	AlwaysRelevantForConnectionList.Emplace(
		RepGraphConnection->NetConnection, ConnectionNode);
}

void UMT2ReplicationGraph::RouteAddNetworkActorToNodes(
	const FNewReplicatedActorInfo& ActorInfo,
	FGlobalActorReplicationInfo& GlobalInfo)
{
	AActor* Actor = ActorInfo.Actor;
	ensureMsgf(!(Actor->bAlwaysRelevant && Actor->bOnlyRelevantToOwner),
		TEXT("Replicated actor %s is both always relevant and owner-only."),
		*Actor->GetName());

	if (Actor->bAlwaysRelevant)
	{
		AlwaysRelevantNode->NotifyAddNetworkActor(ActorInfo);
	}
	else if (Actor->bOnlyRelevantToOwner)
	{
		if (UReplicationGraphNode_AlwaysRelevant_ForConnection* Node =
			GetAlwaysRelevantNodeForConnection(Actor->GetNetConnection()))
		{
			Node->NotifyAddNetworkActor(ActorInfo);
		}
		else
		{
			ActorsWithoutNetConnection.AddUnique(Actor);
		}
	}
	else if (IsMobSpatialActor(Actor))
	{
		MobGridNode->AddActor_Dormancy(ActorInfo, GlobalInfo);
	}
	else
	{
		SpatialGridNode->AddActor_Dormancy(ActorInfo, GlobalInfo);
	}
}

void UMT2ReplicationGraph::RouteRemoveNetworkActorToNodes(
	const FNewReplicatedActorInfo& ActorInfo)
{
	AActor* Actor = ActorInfo.Actor;
	ActorsWithoutNetConnection.RemoveSingleSwap(Actor);

	if (Actor->bAlwaysRelevant)
	{
		AlwaysRelevantNode->NotifyRemoveNetworkActor(ActorInfo);
		SetActorDestructionInfoToIgnoreDistanceCulling(Actor);
	}
	else if (Actor->bOnlyRelevantToOwner)
	{
		// The owning connection can already be cleared when an actor is destroyed during seamless
		// travel. Remove it from the node that actually contains it instead of relying on that
		// transient pointer, otherwise the old actor survives in the replication list.
		for (const FMT2ConnectionAlwaysRelevantNodePair& Pair : AlwaysRelevantForConnectionList)
		{
			if (Pair.Node)
			{
				Pair.Node->NotifyRemoveNetworkActor(ActorInfo, false);
			}
		}
	}
	else if (IsMobSpatialActor(Actor))
	{
		MobGridNode->RemoveActor_Dormancy(ActorInfo);
	}
	else
	{
		SpatialGridNode->RemoveActor_Dormancy(ActorInfo);
	}
}

void UMT2ReplicationGraph::RouteRenameNetworkActorToNodes(
	const FRenamedReplicatedActorInfo& ActorInfo)
{
	AActor* Actor = ActorInfo.NewActorInfo.Actor;
	if (Actor->bAlwaysRelevant)
	{
		AlwaysRelevantNode->NotifyActorRenamed(ActorInfo);
	}
	else if (Actor->bOnlyRelevantToOwner)
	{
		bool bRenamed = false;
		for (const FMT2ConnectionAlwaysRelevantNodePair& Pair : AlwaysRelevantForConnectionList)
		{
			if (Pair.Node)
			{
				bRenamed |= Pair.Node->NotifyActorRenamed(ActorInfo, false);
			}
		}

		if (!bRenamed)
		{
			if (UReplicationGraphNode_AlwaysRelevant_ForConnection* Node =
				GetAlwaysRelevantNodeForConnection(Actor->GetNetConnection()))
			{
				Node->NotifyAddNetworkActor(ActorInfo.NewActorInfo);
			}
			else
			{
				ActorsWithoutNetConnection.AddUnique(Actor);
			}
		}
	}
	else if (IsMobSpatialActor(Actor))
	{
		MobGridNode->RenameActor_Dormancy(ActorInfo);
	}
	else
	{
		SpatialGridNode->RenameActor_Dormancy(ActorInfo);
	}
}

int32 UMT2ReplicationGraph::ServerReplicateActors(float DeltaSeconds)
{
	for (int32 Index = ActorsWithoutNetConnection.Num() - 1; Index >= 0; --Index)
	{
		AActor* Actor = ActorsWithoutNetConnection[Index];
		if (!IsValid(Actor))
		{
			ActorsWithoutNetConnection.RemoveAtSwap(Index, EAllowShrinking::No);
			continue;
		}

		if (UReplicationGraphNode_AlwaysRelevant_ForConnection* Node =
			GetAlwaysRelevantNodeForConnection(Actor->GetNetConnection()))
		{
			Node->NotifyAddNetworkActor(FNewReplicatedActorInfo(Actor));
			ActorsWithoutNetConnection.RemoveAtSwap(Index, EAllowShrinking::No);
		}
	}

	return Super::ServerReplicateActors(DeltaSeconds);
}

UReplicationGraphNode_AlwaysRelevant_ForConnection*
UMT2ReplicationGraph::GetAlwaysRelevantNodeForConnection(UNetConnection* Connection) const
{
	if (!Connection)
	{
		return nullptr;
	}
	if (const FMT2ConnectionAlwaysRelevantNodePair* Pair =
		AlwaysRelevantForConnectionList.FindByKey(Connection))
	{
		return Pair->Node;
	}
	return nullptr;
}
