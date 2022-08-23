// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ReplicationGraph.h"
#include "FortReplicationGraph.generated.h"

class ABuildingActor;
class AFortPawn;
class AFortPlayerPawn;
class AFortPlayerPawnAthena;
class AFortPlayerState;
class AFortTeamPrivateInfo;
class AFortWeapon;
class AFortPickup;
class UFortReplicationGraphNode_AlwaysRelevantForTeam;
class UFortReplicationGraphNode_AlwaysRelevantForSquad;
class UFortReplicationGraphNode_BroadcastSpectator;
class UReplicationGraphNode_GridSpatialization2D;
class AFortPlayerStateAthena;
class AGameplayDebuggerCategoryReplicator;
class AFortAthenaVehicle;
class AFortLiveBroadcastController;

DECLARE_LOG_CATEGORY_EXTERN( LogFortReplicationGraph, Display, All);

// This is the main enum we use to route actors to the right replication node. Each class maps to one enum.
UENUM()
enum class EClassRepNodeMapping
{
	NotRouted,						// Doesn't map to any node. Used for special case actors that handled by special case nodes (UFortReplicationGraphNode_AlwaysRelevantForTeam, UFortReplicationGraphNode_PlayerStateFrequencyLimiter)
	RelevantAllConnections,			// Routes to an AlwaysRelevantNode or AlwaysRelevantStreamingLevelNode node

	// ONLY SPATIALIZED Enums below here! See UFortReplicationGraph::IsSpatialized

	Spatialize_Static,				// Routes to GridNode: these actors don't move and don't need to be updated every frame.
	Spatialize_Dynamic,				// Routes to GridNode: these actors mode frequently and are updated once per frame.
	Spatialize_Dormancy,			// Routes to GridNode: While dormant we treat as static. When flushed/not dormant dynamic. Note this is for things that "move while not dormant". BuildingActors for example are Spatialize_Static.

};

/** Fortnite Replication Graph implementation. See additional notes in FortReplicationGraph.cpp! */
UCLASS(transient, config=Engine)
class UFortReplicationGraph :public UReplicationGraph
{
	GENERATED_BODY()

public:

	UFortReplicationGraph();

	virtual int32 ServerReplicateActors(float DeltaSeconds) override;

	virtual void InitGlobalActorClassSettings() override;
	virtual void InitGlobalGraphNodes() override;
	virtual void InitConnectionGraphNodes(UNetReplicationGraphConnection* RepGraphConnection) override;
	virtual void RouteAddNetworkActorToNodes(const FNewReplicatedActorInfo& ActorInfo, FGlobalActorReplicationInfo& GlobalInfo) override;
	virtual void RouteRemoveNetworkActorToNodes(const FNewReplicatedActorInfo& ActorInfo) override;

	UPROPERTY()
	TArray<UClass*>	SpatializedClasses;

	UPROPERTY()
	TArray<UClass*> NonSpatializedChildClasses;

	UPROPERTY()
	TArray<UClass*>	AlwaysRelevantClasses;

	UPROPERTY()
	UReplicationGraphNode_GridSpatialization2D* GridNode;

	UPROPERTY()
	UReplicationGraphNode_ActorList* AlwaysRelevantNode;

	UPROPERTY()
	UFortReplicationGraphNode_BroadcastSpectator* BroadcastSpectatorRelevancyNode;

	TMap<FName, FActorRepListRefView> AlwaysRelevantStreamingLevelActors;

	//UFortReplicationGraphNode_AlwaysRelevantForTeam* UFortReplicationGraph::GetNodeForTeam(AFortTeamPrivateInfo* TeamInfo);
	UFortReplicationGraphNode_AlwaysRelevantForSquad* GetNodeForSquad(uint8 SquadID);

	void OnPlayerStateSquadAssign(AFortPlayerStateAthena* PS);
	void OnPlayerAddedTeam(AFortTeamPrivateInfo* TeamInfo, AFortPlayerState* PS);
	void OnPlayerRemovedTeam(AFortTeamPrivateInfo* TeamInfo, AFortPlayerState* PS);
	void OnPawnWeaponChange(AFortPawn* Pawn, AFortWeapon* NewWeapon, AFortWeapon* OldWeapon);
	void OnVehicleChanged(AFortPlayerPawn* Pawn, AFortAthenaVehicle* NewVehicle, AFortAthenaVehicle* PrevVehicle);
	void OnParachuteStateChange(AFortPlayerPawn* Pawn, bool NewState);
	void OnCullDistSqChange(AActor* Actor, float Dist);
	void OnBuildingDistSqChange(ABuildingActor* Building, float Dist);
	void OnPlayerPawnDistSqChange(AFortPlayerPawnAthena* Pawn, float DistSq);
	void OnPlaylistDataLoaded(class AFortGameModeAthena* GameMode);
	void OnPickupCullDistanceChanged(AFortPickup* Pickup, float dist);
	void OnPlayerPawnHighFrequencyChange(AFortPlayerPawnAthena* Pawn, bool bHighFreq);
	void OnBroadcastSpectatorJoined(AFortLiveBroadcastController* PC);

	UFUNCTION()
	void OnVehicleDestroyed(AActor* DestroyedVehicle);

	UFUNCTION()
	void OnHighFrequencyPawnDestroyed(AActor* Pawn);

#if WITH_GAMEPLAY_DEBUGGER
	void OnGameplayDebuggerOwnerChange(AGameplayDebuggerCategoryReplicator* Debugger, APlayerController* OldOwner);
#endif

	void PrintRepNodePolicies();

	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

//	void ToggleHighFrequencyPawns();

	void EnableSpectatorNode();

	virtual void LogGlobalGraphNodes(FReplicationGraphDebugInfo& DebugInfo) const override;

private:

	UPROPERTY()
	TArray<AFortPlayerPawn*> PlayerPawnsToVerify;

	EClassRepNodeMapping GetMappingPolicy(UClass* Class);

	bool IsSpatialized(EClassRepNodeMapping Mapping) const { return Mapping >= EClassRepNodeMapping::Spatialize_Static; }

	void OnAlwaysRelevantStreamingActorDormancyFlush(FActorRepListType Actor, FGlobalActorReplicationInfo& GlobalInfo);

	TClassMap<EClassRepNodeMapping> ClassRepNodePolicies;

	TMap<FObjectKey /** AFortTeamPrivateInfo */, UFortReplicationGraphNode_AlwaysRelevantForTeam*> FortTeamPrivateInfoMap;
	TMap<uint8, UFortReplicationGraphNode_AlwaysRelevantForSquad*> SquadMap;

	bool bSpectatorNodeEnabled = false;
};

UCLASS()
class UFortReplicationGraphNode_AlwaysRelevant_ForConnection : public UReplicationGraphNode
{
	GENERATED_BODY()

public:

	virtual void NotifyAddNetworkActor(const FNewReplicatedActorInfo& Actor) override { }

	virtual bool NotifyRemoveNetworkActor(const FNewReplicatedActorInfo& ActorInfo, bool bWarnIfNotFound = true) override { return false; }

	virtual void NotifyResetAllNetworkActors() override { }

	virtual void GatherActorListsForConnection(const FConnectionGatherActorListParameters& Params) override;

	virtual void LogNode(FReplicationGraphDebugInfo& DebugInfo, const FString& NodeName) const override;

	void FlushStreamingAlwaysRelevantList(const FNewReplicatedActorInfo& ActorInfo);
	void OnClientLevelVisibilityAdd(FName LevelName, UWorld* StreamingWorld);
	void OnClientLevelVisibilityRemove(FName LevelName);

#if WITH_GAMEPLAY_DEBUGGER
	AGameplayDebuggerCategoryReplicator* GameplayDebugger = nullptr;
#endif

private:

	TArray<FName, TInlineAllocator<64> > AlwaysRelevantStreamingLevelsNeedingReplication;

	FActorRepListRefView ReplicationActorList;

	UPROPERTY()
		AActor* LastPawn = nullptr;

	bool bInitializedPlayerState = false;
};

/** This is a specialized node for handling PlayerState replication in a frequency limited fashion. It tracks all player states but only returns a subset of them to the replication driver each frame. */
UCLASS()
class UFortReplicationGraphNode_PlayerStateFrequencyLimiter : public UReplicationGraphNode
{
	GENERATED_BODY()

		UFortReplicationGraphNode_PlayerStateFrequencyLimiter();

	virtual void NotifyAddNetworkActor(const FNewReplicatedActorInfo& Actor) override { }
	virtual bool NotifyRemoveNetworkActor(const FNewReplicatedActorInfo& ActorInfo, bool bWarnIfNotFound = true) override { return false; }

	virtual void GatherActorListsForConnection(const FConnectionGatherActorListParameters& Params) override;

	virtual void PrepareForReplication() override;

	virtual void LogNode(FReplicationGraphDebugInfo& DebugInfo, const FString& NodeName) const override;

	/** How many actors we want to return to the replication driver per frame. Will not suppress ForceNetUpdate. */
	int32 TargetActorsPerFrame = 2;

private:

	TArray<FActorRepListRefView> ReplicationActorLists;
	FActorRepListRefView ForceNetUpdateReplicationActorList;
};

UCLASS()
class UFortReplicationGraphNode_BroadcastSpectator : public UReplicationGraphNode_ActorList
{
	GENERATED_BODY()

	UFortReplicationGraphNode_BroadcastSpectator();

public:

	virtual void NotifyAddNetworkActor(const FNewReplicatedActorInfo& Actor) override;

	virtual bool NotifyRemoveNetworkActor(const FNewReplicatedActorInfo& ActorInfo, bool bWarnIfNotFound = true) override;

	virtual void GatherActorListsForConnection(const FConnectionGatherActorListParameters& Params) override;

	TClassMap<bool> ClassesForSpectatorRelevancy;

	UPROPERTY()
		class AActor* LastFollowedPawn;
};

UCLASS()
class UFortReplicationGraphNode_AlwaysRelevantForSquad : public UReplicationGraphNode
{
	GENERATED_BODY()

	UFortReplicationGraphNode_AlwaysRelevantForSquad();

public:

	virtual void NotifyAddNetworkActor(const FNewReplicatedActorInfo& Actor) override { }

	virtual bool NotifyRemoveNetworkActor(const FNewReplicatedActorInfo& ActorInfo, bool bWarnIfNotFound = true) override { return false; }

	virtual void NotifyResetAllNetworkActors() override { }

	virtual void GatherActorListsForConnection(const FConnectionGatherActorListParameters& Params) override;

	virtual void LogNode(FReplicationGraphDebugInfo& DebugInfo, const FString& NodeName) const override;

	virtual void PrepareForReplication() override;

	UPROPERTY()
		TArray<class AFortPlayerStateAthena*>              PlayerStates;

	uint8 SquadID;

	int32 UpdatePerConnectionCullDistances;

private:

	FActorRepListRefView ReplicationActorList;
};