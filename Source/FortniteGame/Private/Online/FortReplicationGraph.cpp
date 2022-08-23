// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

/**
*
*	===================== FortReplicationGraph Replication =====================
*
*	Overview
*
*		This changes the way actor relevancy works. AActor::IsNetRelevantFor is NOT used in this system!
*
*		Instead, The UFortReplicationGraph contains UReplicationGraphNodes. These nodes are responsible for generating lists of actors to replicate for each connection.
*		Most of these lists are persistent across frames. This enables most of the gathering work ("which actors should be considered for replication) to be shared/reused.
*		Nodes may be global (used by all connections), connection specific (each connection gets its own node), or shared (e.g, teams: all connections on the same team share).
*		Actors can be in multiple nodes! For example a pawn may be in the spatialization node but also in the always-relevant-for-team node. It will be returned twice for
*		teammates. This is ok though should be minimized when possible.
*
*		UFortReplicationGraph is intended to not be directly used by the game code. That is, you should not have to include FortReplicationGraph.h anywhere else.
*		Rather, UFortReplicationGraph depends on the game code and registers for events that the game code broadcasts (e.g., events for players joining/leaving teams).
*		This choice was made because it gives UFortReplicationGraph a complete holistic view of actor replication. Rather than exposing generic public functions that any
*		place in game code can invoke, all notifications are explicitly registered in UFortReplicationGraph::InitGlobalActorClassSettings.
*
*	Fortnite Nodes
*
*		These are the top level nodes currently used:
*
*		UReplicationGraphNode_GridSpatialization2D:
*		This is the spatialization node. All "distance based relevant" actors will be routed here. This node divides the map into a 2D grid. Each cell in the grid contains
*		children nodes that hold lists of actors based on how they update/go dormant. Actors are put in multiple cells. Connections pull from the single cell they are in.
*
*		UReplicationGraphNode_ActorList
*		This is an actor list node that contains the always relevant actors. These actors are always relevant to every connection.
*
*		UFortReplicationGraphNode_AlwaysRelevant_ForConnection
*		This is the node for connection specific always relevant actors. This node does not maintain a persistent list but builds it each frame. This is possible because (currently)
*		these actors are all easily accessed from the PlayerController. A persistent list would require notifications to be broadcast when these actors change, which would be possible
*		but currently not necessary.
*
*		UFortReplicationGraphNode_PlayerStateFrequencyLimiter
*		A custom node for handling player state replication. This replicates a small rolling set of player states (currently 2/frame). This is so player states replicate
*		to simulated connections at a low, steady frequency, and to take advantage of serialization sharing. Auto proxy player states are replicated at higher frequency (to the
*		owning connection only) via UFortReplicationGraphNode_AlwaysRelevant_ForConnection.
*
*		UReplicationGraphNode_TearOff_ForConnection
*		Connection specific node for handling tear off actors. This is created and managed in the base implementation of Replication Graph.
*
*	Dependent Actors (AFortWeapon)
*
*		Replication Graph introduces a concept of dependent actor replication. This is an actor (AFortWeapon) that only replicates when another actor replicates (Pawn). I.e, the weapon
*		actor itself never goes into the Replication Graph. It is never gathered on its own and never prioritized. It just has a chance to replicate when the Pawn replicates. This keeps
*		the graph leaner since no extra work has to be done for the weapon actors.
*
*		See UFortReplicationGraph::OnPawnWeaponChange: this is how actors are added/removed from the dependent actor list.
*
*	How To Use
*
*		Making something always relevant: Please avoid if you can :) If you must, just setting AActor::bAlwaysRelevant = true in the class defaults will do it.
*
*		Making something always relevant to connection: You will need to modify UFortReplicationGraphNode_AlwaysRelevant_ForConnection::GatherActorListsForConnection. You will also want
*		to make sure the actor does not get put in one of the other nodes. The safest way to do this is by setting its EClassRepNodeMapping to NotRouted in UFortReplicationGraph::InitGlobalActorClassSettings.
*
*	How To Debug
*
*		Its a good idea to just disable rep graph to see if your problem is specific to this system or just general replication/game play problem.
*
*		If it is replication graph related, there are several useful commands that can be used: see ReplicationGraph_Debugging.cpp. The most useful are below. Use the 'cheat' command to run these on the server from a client.
*
*		"Net.RepGraph.PrintGraph" - this will print the graph to the log: each node and actor.
*		"Net.RepGraph.PrintGraph class" - same as above but will group by class.
*		"Net.RepGraph.PrintGraph nclass" - same as above but will group by native classes (hides blueprint noise)
*
*		Net.RepGraph.PrintAll <Frames> <ConnectionIdx> <"Class"/"Nclass"> -  will print the entire graph, the gathered actors, and how they were prioritized for a given connection for X amount of frames.
*
*		Net.RepGraph.PrintAllActorInfo <ActorMatchString> - will print the class, global, and connection replication info associated with an actor/class. If MatchString is empty will print everything. Call directly from client.
*
*		FortRepGraph.PrintRouting - will print the EClassRepNodeMapping for each class. That is, how a given actor class is routed (or not) in the Replication Graph.
*
*/

#include "FortReplicationGraph.h"
#include "FortniteGame.h"

#include "Net/UnrealNetwork.h"
#include "Engine/LevelStreaming.h"
#include "EngineUtils.h"
#include "CoreGlobals.h"

#if WITH_GAMEPLAY_DEBUGGER
#include "GameplayDebuggerCategoryReplicator.h"
#endif

#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameState.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Pawn.h"
#include "Engine/LevelScriptActor.h"

#include "FortPlayerPawn.h"
#include "FortWeapon.h"
#include "FortGameStateAthena.h"
#include "FortInventory.h"
#include "FortPlayerParachute.h"
#include "FortQuickBars.h"
#include "FortTeamPrivateInfo.h"
#include "BuildingSMActor.h"
#include "FortPickup.h"
#include "FortLiveBroadcastController.h"
#include "FortAthenaVehicle.h"
#include "FortPlayerController.h"
#include "FortPlayerParachute.h"
#include "FortAthenaSupplyDrop.h"

DEFINE_LOG_CATEGORY(LogFortReplicationGraph);

float CVar_FortRepGraph_DestructionInfoMaxDist = 30000.f;
static FAutoConsoleVariableRef CVarFortRepGraphDestructMaxDist(TEXT("FortRepGraph.DestructInfo.MaxDist"), CVar_FortRepGraph_DestructionInfoMaxDist, TEXT("Max distance (not squared) to rep destruct infos at"), ECVF_Default);

int32 CVar_FortRepGraph_DisplayClientLevelStreaming = 0;
static FAutoConsoleVariableRef CVarFortRepGraphDisplayClientLevelStreaming(TEXT("FortRepGraph.DisplayClientLevelStreaming"), CVar_FortRepGraph_DisplayClientLevelStreaming, TEXT(""), ECVF_Default);

float CVar_FortRepGraph_CellSize = 10000.f;
static FAutoConsoleVariableRef CVarFortRepGraphCellSize(TEXT("FortRepGraph.CellSize"), CVar_FortRepGraph_CellSize, TEXT(""), ECVF_Default);

// Essentially "Min X" for replication. This is just an initial value. The system will reset itself if actors appears outside of this.
float CVar_FortRepGraph_SpatialBiasX = -270000.f;
static FAutoConsoleVariableRef CVarFortRepGraphSpatialBiasX(TEXT("FortRepGraph.SpatialBiasX"), CVar_FortRepGraph_SpatialBiasX, TEXT(""), ECVF_Default);

// Essentially "Min Y" for replication. This is just an initial value. The system will reset itself if actors appears outside of this.
float CVar_FortRepGraph_SpatialBiasY = -270000.f;
static FAutoConsoleVariableRef CVarFortRepSpatialBiasY(TEXT("FortRepGraph.SpatialBiasY"), CVar_FortRepGraph_SpatialBiasY, TEXT(""), ECVF_Default);

// How many buckets to spread dynamic, spatialized actors across. High number = more buckets = smaller effective replication frequency. This happens before individual actors do their own NetUpdateFrequency check.
int32 CVar_FortRepGraph_DynamicActorFrequencyBuckets = 3;
static FAutoConsoleVariableRef CVarFortRepDynamicActorFrequencyBuckets(TEXT("FortRepGraph.DynamicActorFrequencyBuckets"), CVar_FortRepGraph_DynamicActorFrequencyBuckets, TEXT(""), ECVF_Default);

int32 CVar_FortRepGraph_EnableFastSharedPath = 0;
static FAutoConsoleVariableRef CVarFortRepEnableFastSharedPath(TEXT("FortRepGraph.EnableFastSharedPath"), CVar_FortRepGraph_EnableFastSharedPath, TEXT(""), ECVF_Default);

int32 CVar_FortRepGraph_DisableSpatialRebuilds = 1;
static FAutoConsoleVariableRef CVarFortRepDisableSpatialRebuilds(TEXT("FortRepGraph.DisableSpatialRebuilds"), CVar_FortRepGraph_DisableSpatialRebuilds, TEXT(""), ECVF_Default);

int32 CVar_FortRepGraph_DynamicActorFrequencyBuckets_LargeTeamMode = 0;
static FAutoConsoleVariableRef CVarFortRepDynamicActorFrequencyBucketsLargeTeamMode(TEXT("FortRepGraph.DynamicActorFrequencyBucketsLargeTeamMode"), CVar_FortRepGraph_DynamicActorFrequencyBuckets_LargeTeamMode, TEXT(""), ECVF_Default);

int32 CVar_FortRepGraph_InstantFlush_LargeTeamMode = 0;
static FAutoConsoleVariableRef CVarFortRepInstantFlushLargeTeamMode(TEXT("FortRepGraph.InstantFlushLargeTeamMode"), CVar_FortRepGraph_InstantFlush_LargeTeamMode, TEXT(""), ECVF_Default);

int32 CVar_FortRepGraph_VerifyPlayerPawnDependentActors = 0;
static FAutoConsoleVariableRef CVarFortRepVerifyPlayerPawnDependentActors(TEXT("FortRepGraph.VerifyPlayerPawnDependentActors"), CVar_FortRepGraph_VerifyPlayerPawnDependentActors, TEXT(""), ECVF_Default);

// ----------------------------------------------------------------------------------------------------------


UFortReplicationGraph::UFortReplicationGraph()
{
}

void InitClassReplicationInfo(FClassReplicationInfo& Info, UClass* Class, bool bSpatialize, float ServerMaxTickRate)
{
	AActor* CDO = Class->GetDefaultObject<AActor>();
	if (bSpatialize)
	{
		Info.CullDistanceSquared = CDO->NetCullDistanceSquared;
		UE_LOG(LogFortReplicationGraph, Log, TEXT("Setting cull distance for %s to %f (%f)"), *Class->GetName(), Info.CullDistanceSquared, FMath::Sqrt(Info.CullDistanceSquared));
	}

	Info.ReplicationPeriodFrame = FMath::Max<uint32>((uint32)FMath::RoundToFloat(ServerMaxTickRate / CDO->NetUpdateFrequency), 1);

	UClass* NativeClass = Class;
	while (!NativeClass->IsNative() && NativeClass->GetSuperClass() && NativeClass->GetSuperClass() != AActor::StaticClass())
	{
		NativeClass = NativeClass->GetSuperClass();
	}

	UE_LOG(LogFortReplicationGraph, Log, TEXT("Setting replication period for %s (%s) to %d frames (%.2f)"), *Class->GetName(), *NativeClass->GetName(), Info.ReplicationPeriodFrame, CDO->NetUpdateFrequency);
}

const UClass* GetParentNativeClass(const UClass* Class)
{
	while (Class && !Class->IsNative())
	{
		Class = Class->GetSuperClass();
	}

	return Class;
}

void UFortReplicationGraph::InitGlobalActorClassSettings()
{
	Super::InitGlobalActorClassSettings();

	// TODO: Implement this
	/* if (UFortRuntimeOptions::Get().BRServerMaxTickRate > 0.f)
	{
		UE_LOG(LogFortReplicationGraph, Display, TEXT("Setting NetServerMaxTickRate to %.2f"), UFortRuntimeOptions::Get().BRServerMaxTickRate);
		NetDriver->NetServerMaxTickRate = UFortRuntimeOptions::Get().BRServerMaxTickRate;
	}*/

	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	// Programatically build the rules.
	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------

	auto AddInfo = [&](UClass* Class, EClassRepNodeMapping Mapping) { ClassRepNodePolicies.Set(Class, Mapping); };
	AddInfo( AActor::StaticClass(),                                EClassRepNodeMapping::NotRouted);                // Default not routed
	AddInfo( AFortWeapon::StaticClass(),                           EClassRepNodeMapping::NotRouted);                // Handled via DependantActor replication (Pawn)
    AddInfo( AFortPlayerParachute::StaticClass(),                  EClassRepNodeMapping::NotRouted);                // Handled via DependantActor replication (Pawn)
    AddInfo( ALevelScriptActor::StaticClass(),                     EClassRepNodeMapping::NotRouted);                // Not needed in FN
    AddInfo( APlayerState::StaticClass(),                          EClassRepNodeMapping::NotRouted);                // Special cased via UFortReplicationGraphNode_PlayerStateFrequencyLimiter
	// TODO: Implement this
    //AddInfo( AFortTeamInfo::StaticClass(),                         EClassRepNodeMapping::NotRouted);                // Not replicated in BR
	AddInfo( AFortInventory::StaticClass(),                        EClassRepNodeMapping::NotRouted);                // Handled via UFortReplicationGraphNode_AlwaysRelevant_ForConnection
	AddInfo( AFortQuickBars::StaticClass(),                        EClassRepNodeMapping::NotRouted);                // Handled via UFortReplicationGraphNode_AlwaysRelevant_ForConnection
	AddInfo( AFortTeamPrivateInfo::StaticClass(),                  EClassRepNodeMapping::NotRouted);                // Handled via UFortReplicationGraphNode_AlwaysRelevant_ForConnection
	AddInfo( AReplicationGraphDebugActor::StaticClass(),           EClassRepNodeMapping::NotRouted);                // Not needed. Replicated special case inside RepGraph
	AddInfo( AInfo::StaticClass(),                                 EClassRepNodeMapping::RelevantAllConnections);   // Non spatialized, relevant to all
	AddInfo( ABuildingSMActor::StaticClass(),                      EClassRepNodeMapping::Spatialize_Static);        // Spatialized and never moves. Routes to GridNode.
	AddInfo( AFortPickup::StaticClass(),                           EClassRepNodeMapping::Spatialize_Dormancy);        // Spatialized dynamically while not dormant, then statically once dormant. Routes to GridNode.
	// TODO: Implement these
	//AddInfo( ABuildingGameplayActorConsumable::StaticClass(),      EClassRepNodeMapping::Spatialize_Dormancy);        // Same as AFortPickup
//	AddInfo( AFortStaticReplicatedActor::StaticClass(),            EClassRepNodeMapping::Spatialize_Static);        // Spatialized and never moves. Routes to GridNode.
	//AddInfo( AFortAlwaysRelevantReplicatedActor::StaticClass(),    EClassRepNodeMapping::RelevantAllConnections);        // Non spatialized, relevant to all


#if WITH_GAMEPLAY_DEBUGGER
//	AddInfo(AGameplayDebuggerCategoryReplicator::StaticClass(), EClassRepNodeMapping::NotRouted);				// Replicated via UFortReplicationGraphNode_AlwaysRelevant_ForConnection
#endif

	TArray<UClass*> AllReplicatedClasses;

	// TODO:
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		AActor* ActorCDO = Cast<AActor>(Class->GetDefaultObject());
		if (!ActorCDO || !ActorCDO->GetIsReplicated())
		{
			continue;
		}

		// Skip SKEL and REINST classes.
		if (Class->GetName().StartsWith(TEXT("SKEL_")) || Class->GetName().StartsWith(TEXT("REINST_")))
		{
			continue;
		}

		// --------------------------------------------------------------------
		// This is a replicated class. Save this off for the second pass below
		// --------------------------------------------------------------------

		AllReplicatedClasses.Add(Class);

		// Skip if already in the map (added explicitly)
		if (ClassRepNodePolicies.Contains(Class, false))
		{
			continue;
		}

		auto ShouldSpatialize = [](const AActor* CDO)
		{
			return CDO->GetIsReplicated() && (!(CDO->bAlwaysRelevant || CDO->bOnlyRelevantToOwner || CDO->bNetUseOwnerRelevancy));
		};

		auto GetLegacyDebugStr = [](const AActor* CDO)
		{
			return FString::Printf(TEXT("%s [%d/%d/%d]"), *CDO->GetClass()->GetName(), CDO->bAlwaysRelevant, CDO->bOnlyRelevantToOwner, CDO->bNetUseOwnerRelevancy);
		};

		// Only handle this class if it differs from its super. There is no need to put every child class explicitly in the graph class mapping
		UClass* SuperClass = Class->GetSuperClass();
		if (AActor* SuperCDO = Cast<AActor>(SuperClass->GetDefaultObject()))
		{
			if (SuperCDO->GetIsReplicated() == ActorCDO->GetIsReplicated()
				&& SuperCDO->bAlwaysRelevant == ActorCDO->bAlwaysRelevant
				&&	SuperCDO->bOnlyRelevantToOwner == ActorCDO->bOnlyRelevantToOwner
				&&	SuperCDO->bNetUseOwnerRelevancy == ActorCDO->bNetUseOwnerRelevancy
				)
			{
				continue;
			}

			if (ShouldSpatialize(ActorCDO) == false && ShouldSpatialize(SuperCDO) == true)
			{
				UE_LOG(LogFortReplicationGraph, Log, TEXT("Adding %s to NonSpatializedChildClasses. (Parent: %s)"), *GetLegacyDebugStr(ActorCDO), *GetLegacyDebugStr(SuperCDO));
				NonSpatializedChildClasses.Add(Class);
			}
		}

		if (ShouldSpatialize(ActorCDO))
		{
			AddInfo(Class, EClassRepNodeMapping::Spatialize_Dynamic);
		}
		else if (ActorCDO->bAlwaysRelevant && !ActorCDO->bOnlyRelevantToOwner)
		{
			AddInfo(Class, EClassRepNodeMapping::RelevantAllConnections);
		}
	}

	// -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	// Setup FClassReplicationInfo. This is essentially the per class replication settings. Some we set explicitly, the rest we are setting via looking at the legacy settings on AActor.
	// -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

	TArray<UClass*> ExplicitlySetClasses;
	auto SetClassInfo = [&](UClass* Class, const FClassReplicationInfo& Info) { GlobalActorReplicationInfoMap.SetClassInfo(Class, Info); ExplicitlySetClasses.Add(Class); };

	FClassReplicationInfo PawnClassRepInfo;
	PawnClassRepInfo.DistancePriorityScale = 1.f;
	PawnClassRepInfo.StarvationPriorityScale = 1.f;
	PawnClassRepInfo.ActorChannelFrameTimeout = 4;
	PawnClassRepInfo.CullDistanceSquared = 15000.f * 15000.f; // Yuck
	SetClassInfo(APawn::StaticClass(), PawnClassRepInfo);

	FClassReplicationInfo PlayerStateRepInfo;
	PlayerStateRepInfo.DistancePriorityScale = 0.f;
	PlayerStateRepInfo.ActorChannelFrameTimeout = 0;
	SetClassInfo(APlayerState::StaticClass(), PlayerStateRepInfo);

	UReplicationGraphNode_ActorListFrequencyBuckets::DefaultSettings.ListSize = 12;

	// Set FClassReplicationInfo based on legacy settings from all replicated classes
	for (UClass* ReplicatedClass : AllReplicatedClasses)
	{
		if (ExplicitlySetClasses.FindByPredicate([&](const UClass* SetClass) { return ReplicatedClass->IsChildOf(SetClass); }) != nullptr)
		{
			continue;
		}

		const bool bClassIsSpatialized = IsSpatialized(ClassRepNodePolicies.GetChecked(ReplicatedClass));

		FClassReplicationInfo ClassInfo;
		InitClassReplicationInfo(ClassInfo, ReplicatedClass, bClassIsSpatialized, NetDriver->NetServerMaxTickRate);
		GlobalActorReplicationInfoMap.SetClassInfo(ReplicatedClass, ClassInfo);
	}


	// Print out what we came up with
	UE_LOG(LogFortReplicationGraph, Log, TEXT(""));
	UE_LOG(LogFortReplicationGraph, Log, TEXT("Class Routing Map: "));
	UEnum* Enum = FindObject<UEnum>(ANY_PACKAGE, TEXT("EClassRepNodeMapping"));
	for (auto ClassMapIt = ClassRepNodePolicies.CreateIterator(); ClassMapIt; ++ClassMapIt)
	{
		UClass* Class = CastChecked<UClass>(ClassMapIt.Key().ResolveObjectPtr());
		const EClassRepNodeMapping Mapping = ClassMapIt.Value();

		// Only print if different than native class
		UClass* ParentNativeClass = GetParentNativeClass(Class);

		const EClassRepNodeMapping* ParentMapping = ClassRepNodePolicies.Get(ParentNativeClass);
		if (ParentMapping && Class != ParentNativeClass && Mapping == *ParentMapping)
		{
			continue;
		}

		UE_LOG(LogFortReplicationGraph, Log, TEXT("  %s (%s) -> %s"), *Class->GetName(), *GetNameSafe(ParentNativeClass), *Enum->GetNameStringByValue(static_cast<uint32>(Mapping)));
	}

	UE_LOG(LogFortReplicationGraph, Log, TEXT(""));
	UE_LOG(LogFortReplicationGraph, Log, TEXT("Class Settings Map: "));
	FClassReplicationInfo DefaultValues;
	for (auto ClassRepInfoIt = GlobalActorReplicationInfoMap.CreateClassMapIterator(); ClassRepInfoIt; ++ClassRepInfoIt)
	{
		const UClass* Class = CastChecked<UClass>(ClassRepInfoIt.Key().ResolveObjectPtr());
		const FClassReplicationInfo& ClassInfo = ClassRepInfoIt.Value();
		UE_LOG(LogFortReplicationGraph, Log, TEXT("  %s (%s) -> %s"), *Class->GetName(), *GetNameSafe(GetParentNativeClass(Class)), *ClassInfo.BuildDebugStringDelta());
	}


	// Rep destruct infos based on CVar value
	DestructInfoMaxDistanceSquared = CVar_FortRepGraph_DestructionInfoMaxDist * CVar_FortRepGraph_DestructionInfoMaxDist;

	// -------------------------------------------------------
	//	Register for game code callbacks.
	//	This could have been done the other way: E.g, AMyGameActor could do GetNetDriver()->GetReplicationDriver<UFortReplicationGraph>()->OnMyGameEvent etc.
	//	This way at least keeps the rep graph out of game code directly and allows rep graph to exist in its own module
	//	So for now, erring on the side of a cleaning dependencies between classes.
	// -------------------------------------------------------
// TODO: Implement these delegates
 /*	AFortGameModeAthena::NotifyPlayerStateSquadAssignment.AddUObject(this, &UFortReplicationGraph::OnPlayerStateSquadAssign);
	AFortTeamPrivateInfo::NotifyPlayerAdded.AddUObject(this, &UFortReplicationGraph::OnPlayerAddedTeam);
	AFortTeamPrivateInfo::NotifyPlayerRemoved.AddUObject(this, &UFortReplicationGraph::OnPlayerRemovedTeam);*/
	//AFortPawn::NotifyWeaponChange.AddUObject(this, &UFortReplicationGraph::OnPawnWeaponChange);
	/*AFortPlayerPawn::NotifyParachuteStateChange.AddUObject(this, &UFortReplicationGraph::OnParachuteStateChange);
	AFortPlayerPawn::NotifyVehicleChange.AddUObject(this, &UFortReplicationGraph::OnVehicleChanged);
	AFortPlayerPawnAthena::NotifyPlayerPawnCullDistanceChange.AddUObject(this, &UFortReplicationGraph::OnPlayerPawnDistSqChange);
	ABuildingActor::NotifyBuildingNetCullDistChange.AddUObject(this, &UFortReplicationGraph::OnBuildingDistSqChange);
	AFortGameModeAthena::NotifyPlaylistDataLoaded.AddUObject(this, &UFortReplicationGraph::OnPlaylistDataLoaded);
	AFortPickup::NotifyPickupCullDistanceChange.AddUObject(this, &UFortReplicationGraph::OnPickupCullDistanceChanged);
	AFortGameModeAthena::NotifyBroadcastSpectatorJoined.AddUObject(this, &UFortReplicationGraph::OnBroadcastSpectatorJoined);*/

/* TODO But not needed for now.
	if (CVar_FortRepGraph_HighFrequencyPawnUpdates > 0)
	{
		AFortPlayerPawnAthena::NotifyPawnHighFrequencyUpdatechange.AddUObject(this, &UFortReplicationGraph::OnPlayerPawnHighFrequencyChange)
	}*/

#if WITH_GAMEPLAY_DEBUGGER
	//AGameplayDebuggerCategoryReplicator::NotifyDebuggerOwnerChange.AddUObject(this, &UFortReplicationGraph::OnGameplayDebuggerOwnerChange);
#endif

	// CSV profiling
	CSVTracker.ResetTrackedClasses();
	UClass* PlayerPawnBP = FindObject<UClass>(ANY_PACKAGE, TEXT("PlayerPawn_Athena_c"));
	if (PlayerPawnBP)
	{
		CSVTracker.SetExplicitClassTracking(PlayerPawnBP, TEXT("PlayerPawn"));
	}
	CSVTracker.SetExplicitClassTracking(AFortPlayerStateAthena::StaticClass(), TEXT("PlayerState"));
	CSVTracker.SetImplicitClassTracking(ABuildingActor::StaticClass(), TEXT("Buildings"));
	CSVTracker.SetImplicitClassTracking(AFortPickup::StaticClass(), TEXT("Pickups"));
	// TODO: Implement CSVTracker.SetImplicitClassTracking(AFortProjectileBase::StaticClass(), TEXT("Projectiles"));
	CSVTracker.SetImplicitClassTracking(AFortAthenaVehicle::StaticClass(), TEXT("Vehicles"));
	CSVTracker.SetImplicitClassTracking(AFortTeamPrivateInfo::StaticClass(), TEXT("PrivateTeamInfo"));
	CSVTracker.SetImplicitClassTracking(AFortInventory::StaticClass(), TEXT("Inventory"));
	CSVTracker.SetImplicitClassTracking(AFortWeapon::StaticClass(), TEXT("Weapons"));
}

void UFortReplicationGraph::InitGlobalGraphNodes()
{
	// Preallocate some replication lists.
	PreAllocateRepList(3, 12);
	PreAllocateRepList(6, 12);
	PreAllocateRepList(128, 64);
	PreAllocateRepList(512, 16);
	PreAllocateRepList(1024, 4);
	PreAllocateRepList(2048, 4);

	// -----------------------------------------------
	//	Spatial Actors
	// -----------------------------------------------

	GridNode = CreateNewNode<UReplicationGraphNode_GridSpatialization2D>();
	GridNode->CellSize = CVar_FortRepGraph_CellSize;
	GridNode->SpatialBias = FVector2D(CVar_FortRepGraph_SpatialBiasX, CVar_FortRepGraph_SpatialBiasY);

	if (CVar_FortRepGraph_DisableSpatialRebuilds)
	{
		GridNode->AddSpatialRebuildBlacklistClass(AActor::StaticClass()); // Disable All spatial rebuilding
	}

	AddGlobalGraphNode(GridNode);

	// -----------------------------------------------
	//	Always Relevant (to everyone) Actors
	// -----------------------------------------------
	AlwaysRelevantNode = CreateNewNode<UReplicationGraphNode_ActorList>();
	AddGlobalGraphNode(AlwaysRelevantNode);

	// -----------------------------------------------
	//	Player State specialization. This will return a rolling subset of the player states to replicate
	// -----------------------------------------------
	UFortReplicationGraphNode_PlayerStateFrequencyLimiter* PlayerStateNode = CreateNewNode<UFortReplicationGraphNode_PlayerStateFrequencyLimiter>();
	AddGlobalGraphNode(PlayerStateNode);

	// -----------------------------------------------
	// Node which handles special relevancy for broadcasting spectators. Although this node is created globally, it is added per connection.
	// -----------------------------------------------

	if (bSpectatorNodeEnabled)
	{
		BroadcastSpectatorRelevancyNode = CreateNewNode<UFortReplicationGraphNode_BroadcastSpectator>();
	}

	// Init our playlist-dependent values at least once
	OnPlaylistDataLoaded(nullptr);
}

void UFortReplicationGraph::InitConnectionGraphNodes(UNetReplicationGraphConnection* RepGraphConnection)
{
	Super::InitConnectionGraphNodes(RepGraphConnection);

	UFortReplicationGraphNode_AlwaysRelevant_ForConnection* AlwaysRelevantConnectionNode = CreateNewNode<UFortReplicationGraphNode_AlwaysRelevant_ForConnection>();

	// This node needs to know when client levels go in and out of visibility
	RepGraphConnection->OnClientVisibleLevelNameAdd.AddUObject(AlwaysRelevantConnectionNode, &UFortReplicationGraphNode_AlwaysRelevant_ForConnection::OnClientLevelVisibilityAdd);
	RepGraphConnection->OnClientVisibleLevelNameRemove.AddUObject(AlwaysRelevantConnectionNode, &UFortReplicationGraphNode_AlwaysRelevant_ForConnection::OnClientLevelVisibilityRemove);

	AddConnectionGraphNode(AlwaysRelevantConnectionNode, RepGraphConnection);
}

int32 UFortReplicationGraph::ServerReplicateActors(float DeltaSeconds)
{
	// Strictly for tracking down FORT-86787
	if (CVar_FortRepGraph_VerifyPlayerPawnDependentActors > 0)
	{
		for (int32 idx = PlayerPawnsToVerify.Num() - 1; idx >= 0; --idx)
		{
			AFortPlayerPawn* Pawn = PlayerPawnsToVerify[idx];
			if (!Pawn)
			{
				PlayerPawnsToVerify.RemoveAtSwap(idx, 1, false);
				continue;
			}

			FGlobalActorReplicationInfo& ActorInfo = GlobalActorReplicationInfoMap.Get(Pawn);

			for (int32 ActorIdx = ActorInfo.DependentActorList.Num() - 1; ActorIdx >= 0; --ActorIdx)
			{
				if (IsActorValidForReplication(ActorInfo.DependentActorList[ActorIdx]) == false)
				{
					ensureAlwaysMsgf(false, TEXT("Invalid dependent actor found in %s's list: %s. VehicleStateLocal: %s. VehicleStateRep: %s"), *GetNameSafe(Pawn), *GetActorRepListTypeDebugString(ActorInfo.DependentActorList[ActorIdx]),
						*GetNameSafe(Pawn->VehicleStateLocal.Vehicle), *GetNameSafe(Pawn->VehicleStateRep.Vehicle));

					ActorInfo.DependentActorList.PrepareForWrite();
					ActorInfo.DependentActorList.RemoveAtSwap(ActorIdx);
				}
			}
		}
	}

	
	return Super::ServerReplicateActors(DeltaSeconds);
}
EClassRepNodeMapping UFortReplicationGraph::GetMappingPolicy(UClass* Class)
{
	return ClassRepNodePolicies.GetChecked(Class);
}

void UFortReplicationGraph::OnAlwaysRelevantStreamingActorDormancyFlush(FActorRepListType Actor, FGlobalActorReplicationInfo & GlobalInfo)
{
}

void UFortReplicationGraph::RouteAddNetworkActorToNodes(const FNewReplicatedActorInfo& ActorInfo, FGlobalActorReplicationInfo& GlobalInfo)
{
	EClassRepNodeMapping Policy = GetMappingPolicy(ActorInfo.Class);
	switch (Policy)
	{
	case EClassRepNodeMapping::NotRouted:
	{
		break;
	}

	case EClassRepNodeMapping::RelevantAllConnections:
	{
		if (ActorInfo.StreamingLevelName == NAME_None)
		{
			AlwaysRelevantNode->NotifyAddNetworkActor(ActorInfo);
		}
		else
		{
			FActorRepListRefView& RepList = AlwaysRelevantStreamingLevelActors.FindOrAdd(ActorInfo.StreamingLevelName);
			RepList.PrepareForWrite();
			RepList.Add(ActorInfo.Actor);
			
			if (!GlobalInfo.Events.DormancyFlush.IsBoundToObject(this))
			{
				GlobalInfo.Events.DormancyFlush.AddUObject(this, &UFortReplicationGraph::OnAlwaysRelevantStreamingActorDormancyFlush);
			}
			else
			{
				UE_LOG(LogFortReplicationGraph, Warning, TEXT("UFortReplicationGraph already bound to dormancy flush for Actor %s"), *GetPathNameSafe(ActorInfo.GetActor()));
			}
		}
		break;
	}

	case EClassRepNodeMapping::Spatialize_Static:
	{
		GridNode->AddActor_Static(ActorInfo, GlobalInfo);
		break;
	}

	case EClassRepNodeMapping::Spatialize_Dynamic:
	{
		GridNode->AddActor_Dynamic(ActorInfo, GlobalInfo);
		break;
	}

	case EClassRepNodeMapping::Spatialize_Dormancy:
	{
		GridNode->AddActor_Dormancy(ActorInfo, GlobalInfo);
		break;
	}
	};
	

	if (bSpectatorNodeEnabled && BroadcastSpectatorRelevancyNode)
	{
		BroadcastSpectatorRelevancyNode->NotifyAddNetworkActor(ActorInfo);
	}
}

void UFortReplicationGraph::RouteRemoveNetworkActorToNodes(const FNewReplicatedActorInfo& ActorInfo)
{
	EClassRepNodeMapping Policy = GetMappingPolicy(ActorInfo.Class);
	switch (Policy)
	{
	case EClassRepNodeMapping::NotRouted:
	{
		break;
	}

	case EClassRepNodeMapping::RelevantAllConnections:
	{
		if (ActorInfo.StreamingLevelName == NAME_None)
		{
			AlwaysRelevantNode->NotifyRemoveNetworkActor(ActorInfo);
		}
		else
		{
			FActorRepListRefView& RepList = AlwaysRelevantStreamingLevelActors.FindChecked(ActorInfo.StreamingLevelName);
			if (RepList.Remove(ActorInfo.Actor) == false)
			{
				UE_LOG(LogFortReplicationGraph, Warning, TEXT("Actor %s was not found in AlwaysRelevantStreamingLevelActors list. LevelName: %s"), *GetActorRepListTypeDebugString(ActorInfo.Actor), *ActorInfo.StreamingLevelName.ToString());
			}
		}
		break;
	}

	case EClassRepNodeMapping::Spatialize_Static:
	{
		GridNode->RemoveActor_Static(ActorInfo);
		break;
	}

	case EClassRepNodeMapping::Spatialize_Dynamic:
	{
		GridNode->RemoveActor_Dynamic(ActorInfo);
		break;
	}

	case EClassRepNodeMapping::Spatialize_Dormancy:
	{
		GridNode->RemoveActor_Dormancy(ActorInfo);
		break;
	}
	};

	if (bSpectatorNodeEnabled && BroadcastSpectatorRelevancyNode)
	{
		BroadcastSpectatorRelevancyNode->NotifyRemoveNetworkActor(ActorInfo, false);
	}
}

// Since we listen to global (static) events, we need to watch out for cross world broadcasts (PIE)
#if WITH_EDITOR
#define CHECK_WORLDS(X) if(X->GetWorld() != GetWorld()) return;
#else
#define CHECK_WORLDS(X)
#endif

void UFortReplicationGraph::OnPlayerStateSquadAssign(AFortPlayerStateAthena* PS)
{
	CHECK_WORLDS(PS);
	if (UNetConnection* NetConnection = PS->GetNetConnection())
	{
		UFortReplicationGraphNode_AlwaysRelevantForSquad* SquadNode = GetNodeForSquad(PS->SquadId);
		SquadNode->PlayerStates.AddUnique(PS);
		AddConnectionGraphNode(SquadNode, NetConnection);
	}
}

void UFortReplicationGraph::OnPlayerAddedTeam(AFortTeamPrivateInfo* TeamInfo, AFortPlayerState* PS)
{
//	CHECK_WORLDS(TeamInfo);
	/*if (UNetConnection* NetConnection = PS->GetNetConnection())
	{
		// TODO:
		//AddConnectionGraphNode(GetNodeForTeamInfo(TeamInfo), NetConnection)
	}*/
}

void UFortReplicationGraph::OnPlayerRemovedTeam(AFortTeamPrivateInfo* TeamInfo, AFortPlayerState* PS)
{
	// TODO:
/*	CHECK_WORLDS(TeamInfo);
	if (UNetConnection* NetConnection = PS->GetNetConnection())
	{
		RemoveConnectionGraphNode(GetNodeForTeamInfo(TeamInfo), NetConnection);
	}*/
}

void UFortReplicationGraph::OnPawnWeaponChange(AFortPawn* Pawn, AFortWeapon* NewWeapon, AFortWeapon* OldWeapon)
{
	CHECK_WORLDS(Pawn);

	FGlobalActorReplicationInfo& ActorInfo = GlobalActorReplicationInfoMap.Get(Pawn);
	ActorInfo.DependentActorList.PrepareForWrite();
	if (OldWeapon)
	{
		ActorInfo.DependentActorList.Remove(OldWeapon);
	}

	if (NewWeapon)
	{
		ActorInfo.DependentActorList.Add(NewWeapon);
	}
}
void UFortReplicationGraph::OnVehicleChanged(AFortPlayerPawn* Pawn, AFortAthenaVehicle* NewVehicle, AFortAthenaVehicle* PrevVehicle)
{
    CHECK_WORLDS(Pawn);
	FGlobalActorReplicationInfo& ActorInfo = GlobalActorReplicationInfoMap.Get(Pawn);
	ActorInfo.DependentActorList.PrepareForWrite();
	if (PrevVehicle)
	{
		ActorInfo.DependentActorList.Remove(PrevVehicle);
		if (PrevVehicle->ContainsPlayers() == false)
		{
			PrevVehicle->OnDestroyed.RemoveDynamic(this, &UFortReplicationGraph::OnVehicleDestroyed);
		}
	}

	if (NewVehicle)
	{
		ActorInfo.DependentActorList.Add(NewVehicle);
		NewVehicle->OnDestroyed.AddUniqueDynamic(this, &UFortReplicationGraph::OnVehicleDestroyed);
	}

	if (Pawn)
	{
		// Once you touch a vehicle, we are going to verify you for the rest of your life
		PlayerPawnsToVerify.AddUnique(Pawn);
	}
}

void UFortReplicationGraph::OnVehicleDestroyed(AActor* DestroyedVehicle)
{
	if (AFortAthenaVehicle* Vehicle = Cast<AFortAthenaVehicle>(DestroyedVehicle))
	{
		const TArray<FAthenaCarPlayerSlot>& PlayerSlots = Vehicle->GetPlayerSlots();
		for (const FAthenaCarPlayerSlot& PlayerSlot : PlayerSlots)
		{
			if (AFortPlayerPawn* PlayerPawn = PlayerSlot.Player)
			{
				FGlobalActorReplicationInfo& ActorInfo = GlobalActorReplicationInfoMap.Get(PlayerPawn);
				if (ActorInfo.DependentActorList.IsValid())
				{
					ActorInfo.DependentActorList.Remove(Vehicle);
				}
			}
		}
	}

}

void UFortReplicationGraph::OnParachuteStateChange(AFortPlayerPawn* Pawn, bool NewState)
{
	CHECK_WORLDS(Pawn);
	FGlobalActorReplicationInfo& ActorInfo = GlobalActorReplicationInfoMap.Get(Pawn);
	ActorInfo.DependentActorList.PrepareForWrite();

	AFortPlayerParachute* Parachute = Pawn->GetParachuteAttachment();
	if (!ensure(Parachute))
	{
		return;
	}

	if (NewState)
	{
		ActorInfo.DependentActorList.Add(Parachute);
	}
	else
	{
		ActorInfo.DependentActorList.Remove(Parachute);
	}
}

void UFortReplicationGraph::OnPlayerPawnDistSqChange(AFortPlayerPawnAthena* Pawn, float DistSq)
{
	OnCullDistSqChange(Pawn, DistSq);
}

void UFortReplicationGraph::OnBuildingDistSqChange(ABuildingActor* Building, float Dist)
{
	OnCullDistSqChange(Building, FMath::Square(Dist + 1000));
}

void UFortReplicationGraph::OnPlaylistDataLoaded(AFortGameModeAthena* GameMode)
{
	bool bIsLargeTeamGame = false;

	if (GameMode)
	{
		if (AFortGameStateAthena* GSA = GameMode->GetGameState<AFortGameStateAthena>())
		{
			bIsLargeTeamGame = GSA->bIsLargeTeamGame;
		}
	}

	GlobalActorChannelFrameNumTimeout = CVar_FortRepGraph_DynamicActorFrequencyBuckets * 2;

	/** @ Implementation for 4.20/Season 5 */
	/*UReplicationGraphNode_ActorListFrequencyBuckets::DefaultNumBuckets = CVar_FortRepGraph_DynamicActorFrequencyBuckets;
	UReplicationGraphNode_ActorListFrequencyBuckets::DefaultBuckedThresholds.Reset();
	UReplicationGraphNode_ActorListFrequencyBuckets::EnableFastPath = (CVar_FortRepGraph_EnableFastSharedPath > 0);*/

	UReplicationGraphNode_ActorListFrequencyBuckets::DefaultSettings.NumBuckets = CVar_FortRepGraph_DynamicActorFrequencyBuckets;
	UReplicationGraphNode_ActorListFrequencyBuckets::DefaultSettings.BucketThresholds.Reset();
	UReplicationGraphNode_ActorListFrequencyBuckets::DefaultSettings.EnableFastPath = (CVar_FortRepGraph_EnableFastSharedPath > 0);

	RPCSendPolicyMap.Reset();

	if (bIsLargeTeamGame)
	{
		/** @ Implementation for 4.20/Season 5 */
	/*	UReplicationGraphNode_ActorListFrequencyBuckets::DefaultBucketThresholds.Emplace(50, CVar_FortRepGraph_DynamicActorFrequencyBuckets_LargeTeamMode);
		UReplicationGraphNode_ActorListFrequencyBuckets::DefaultBucketThresholds.Emplace(80, CVar_FortRepGraph_DynamicActorFrequencyBuckets_LargeTeamMode + 1);
		UReplicationGraphNode_ActorListFrequencyBuckets::DefaultBucketThresholds.Emplace(999, CVar_FortRepGraph_DynamicActorFrequencyBuckets_LargeTeamMode + 2);*/

		// Throttle pawn frequency down when alrge numbers of them are in the same spatialization node
		UReplicationGraphNode_ActorListFrequencyBuckets::DefaultSettings.BucketThresholds.Emplace(50, CVar_FortRepGraph_DynamicActorFrequencyBuckets_LargeTeamMode);
		UReplicationGraphNode_ActorListFrequencyBuckets::DefaultSettings.BucketThresholds.Emplace(80, CVar_FortRepGraph_DynamicActorFrequencyBuckets_LargeTeamMode + 1);
		UReplicationGraphNode_ActorListFrequencyBuckets::DefaultSettings.BucketThresholds.Emplace(999, CVar_FortRepGraph_DynamicActorFrequencyBuckets_LargeTeamMode + 2);

		GlobalActorChannelFrameNumTimeout = CVar_FortRepGraph_DynamicActorFrequencyBuckets_LargeTeamMode + 2;
	}


	if (!bIsLargeTeamGame || CVar_FortRepGraph_InstantFlush_LargeTeamMode > 0)
	{
		RPCSendPolicyMap.Add(FObjectKey(AFortPlayerPawnAthena::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(AFortPlayerPawnAthena, NetMultiCast_Athena_BatchedDamageCue))), FRPCSendPolicyInfo(true));
	}
}

void UFortReplicationGraph::OnPickupCullDistanceChanged(AFortPickup* Pickup, float Dist)
{
	OnCullDistSqChange(Pickup, Dist);
}

void UFortReplicationGraph::OnPlayerPawnHighFrequencyChange(AFortPlayerPawnAthena* Pawn, bool bHighFreq)
{
	FNewReplicatedActorInfo ActorInfo(Pawn);

	if (bHighFreq)
	{
		AlwaysRelevantNode->NotifyAddNetworkActor(ActorInfo);

		// Extra safety to make sure we remove this actor if he gets destroyed
		Pawn->OnDestroyed.AddDynamic(this, &UFortReplicationGraph::OnHighFrequencyPawnDestroyed);
	}
	else
	{
		AlwaysRelevantNode->NotifyRemoveNetworkActor(ActorInfo);
		Pawn->OnDestroyed.RemoveAll(this);
	}
}

void UFortReplicationGraph::OnBroadcastSpectatorJoined(AFortLiveBroadcastController* PC)
{
	UNetConnection* NetConnection = PC ? PC->GetNetConnection() : nullptr;
	UNetReplicationGraphConnection* GraphConnection = NetConnection ? FindOrAddConnectionManager(NetConnection) : nullptr;

	if (ensure(GraphConnection && BroadcastSpectatorRelevancyNode))
	{
		AddConnectionGraphNode(BroadcastSpectatorRelevancyNode, GraphConnection);
	}
}

void UFortReplicationGraph::OnHighFrequencyPawnDestroyed(AActor* Pawn)
{
	FNewReplicatedActorInfo ActorInfo(Pawn);
	AlwaysRelevantNode->NotifyRemoveNetworkActor(ActorInfo);
}

void UFortReplicationGraph::AddReferencedObjects(UObject * InThis, FReferenceCollector & Collector)
{
	UFortReplicationGraph* This = CastChecked<UFortReplicationGraph>(InThis);
	Super::AddReferencedObjects(This, Collector);

	for (auto& MapIt : This->FortTeamPrivateInfoMap)
	{
		Collector.AddReferencedObject(MapIt.Value, This);
	}

	for (auto& MapIt : This->SquadMap)
	{
		Collector.AddReferencedObject(MapIt.Value, This);
	}
}

void UFortReplicationGraph::OnCullDistSqChange(AActor* Actor, float Dist)
{
	CHECK_WORLDS(Actor)

	if (IsActorValidForReplicationGather(Actor) == false)
	{
		return;
	}

	EClassRepNodeMapping Mapping = GetMappingPolicy(Actor->GetClass());
	if (IsSpatialized(Mapping) == false)
	{
		/*
		UClass* NativeClass = Actor->GetClass();
		while(NativeClass && !NativeClass->IsNative() && NativeClass->GetSuperClass && NativeClass->GetSuperClass() != AActor::StaticClass())
		{
			NativeClass = NativeClass->GetSuperClass();
		}

		UE_LOG(LogFortReplicationGraph, Warning, TEXT("Actor %s changed cull distance but it is not spatialized: %d. Class: %s. NativeClass: %s"), *Actor->GetPathName(), (int32)Mapping, *Actor->GetClass()->GetName(),
		*/

		// Supply drops are an example of an always relevant class that updates it cull distance (for rendering purposes) but wants to be always relevant
		return;
	}

	bool WasCreated = false;
	FGlobalActorReplicationInfo& ActorInfo = GlobalActorReplicationInfoMap.Get(Actor, WasCreated);

	const float OldDistSq = ActorInfo.Settings.CullDistanceSquared;
	const float Delta = OldDistSq - Dist;
	if (FMath::IsNearlyZero(Delta))
	{
		// If not actually changing them dont do extra work
		return;
	}

	ActorInfo.Settings.CullDistanceSquared = Dist;

	if (WasCreated == false)
	{
		// If this actor already had a global rep info, then we have to possibly update all connected clients
		// and force a rebuild of the spat graph
		for (UNetReplicationGraphConnection* Connection : Connections)
		{
			if (FConnectionReplicationActorInfo* ConnectionActorInfo = Connection->ActorInfoMap.Find(Actor))
			{
				ConnectionActorInfo->CullDistanceSquared = Dist;
			}
		}

		GridNode->NotifyActorCullDistChange(Actor, ActorInfo, OldDistSq);
	}
}

#if WITH_GAMEPLAY_DEBUGGER
void UFortReplicationGraph::OnGameplayDebuggerOwnerChange(AGameplayDebuggerCategoryReplicator* Debugger, APlayerController* OldOwner)
{
	auto GetAlwaysRelevantForConnectionNode = [&](APlayerController* Controller) -> UFortReplicationGraphNode_AlwaysRelevant_ForConnection*
	{
		if (OldOwner)
		{
			if (UNetConnection* NetConnection = OldOwner->GetNetConnection())
			{
				if (UNetReplicationGraphConnection* GraphConnection = FindOrAddConnectionManager(NetConnection))
				{
					for (UReplicationGraphNode* ConnectionNode : GraphConnection->GetConnectionGraphNodes())
					{
						if (UFortReplicationGraphNode_AlwaysRelevant_ForConnection* AlwaysRelevantConnectionNode = Cast<UFortReplicationGraphNode_AlwaysRelevant_ForConnection>(ConnectionNode))
						{
							return AlwaysRelevantConnectionNode;
						}
					}

				}
			}
		}

		return nullptr;
	};

	if (UFortReplicationGraphNode_AlwaysRelevant_ForConnection* AlwaysRelevantConnectionNode = GetAlwaysRelevantForConnectionNode(OldOwner))
	{
		AlwaysRelevantConnectionNode->GameplayDebugger = nullptr;
	}

	if (UFortReplicationGraphNode_AlwaysRelevant_ForConnection* AlwaysRelevantConnectionNode = GetAlwaysRelevantForConnectionNode(Debugger->GetReplicationOwner()))
	{
		AlwaysRelevantConnectionNode->GameplayDebugger = Debugger;
	}
}
#endif

void UFortReplicationGraph::EnableSpectatorNode()
{
	/** @ Enabling the spectator node */
	bSpectatorNodeEnabled = true;
}

void UFortReplicationGraph::LogGlobalGraphNodes(FReplicationGraphDebugInfo& DebugInfo) const
{
	Super::LogGlobalGraphNodes(DebugInfo);

	// Not really a node, just a TMap we keep with actor lists.
	DebugInfo.Log(TEXT("AlwaysRelevantStreamingLevelActors"));
	DebugInfo.PushIndent();

	for (auto MapIt : AlwaysRelevantStreamingLevelActors)
	{
		FName LevelName = MapIt.Key;
		FActorRepListRefView& RepList = MapIt.Value;
		LogActorRepList(DebugInfo, FString::Printf(TEXT("StreamingLevel: $s"), *LevelName.ToString()), RepList);
	}

	DebugInfo.PopIndent();
}
// TODO:
/*UFortReplicationGraphNode_AlwaysRelevantForTeam* UFortReplicationGraph::GetNodeForTeamInfo(AFortTeamPrivateInfo* TeamInfo)
{
	FObjectKey ObjKey(TeamInfo);
	UFortReplicationGraphNode_AlwaysRelevantForTeam* TeamNode = FortTeamPrivateInfoMap.FindRef(ObjKey);
	if (TeamNode == nullptr)
	{
		TeamNode = CreateNewNode<UFortReplicationGraphNode_AlwaysRelevantForTeam>();
		TeamNode->SetPrivateTeamInfo(TeamInfo);
		FortTeamPrivateInfoMap.Add(ObjKey, TeamNode);
	}

	return TeamNode;
}*/

UFortReplicationGraphNode_AlwaysRelevantForSquad* UFortReplicationGraph::GetNodeForSquad(uint8 SquadID)
{
	UFortReplicationGraphNode_AlwaysRelevantForSquad* SquadNode = SquadMap.FindRef(SquadID);
	if (SquadNode == nullptr)
	{
		SquadNode = CreateNewNode<UFortReplicationGraphNode_AlwaysRelevantForSquad>();
		SquadNode->SquadID = SquadID;
		SquadMap.Add(SquadID, SquadNode);
	}

	return SquadNode;
}

// ------------------------------------------------------------------------------
/*
UFortReplicationGraphNode_AlwaysRelevantForTeam::UFortReplicationGraphNode_AlwaysRelevantForTeam()
{
	bRequiresPrepareForReplicationCall = false;
}

void UFortReplicationGraphNode_AlwaysRelevantForTeam::SetPrivateTeamInfo(AFortTeamPrivateInfo* InFortTeamPrivateInfo)
{
	FortTeamPrivateInfo = InFortTeamPrivateInfo;
	ReplicationActorList.Reset();
	ReplicationActorList.ConditionalAdd(FortTeamPrivateInfo);
}

void UFortReplicationGraphNode_AlwaysRelevantForTeam::GatherActorListsForConnection(const FConnectionGatherActorListParameters& Params)
{
	Params.OutGatheredReplicationLists.AddReplicationActorList(ReplicationActorList);
}

void UFortReplicationGraphNode_AlwaysRelevantForTeam::LogNode(FreplicationGraphDebugInfo& DebugInfo, const FString& NodeName) const
{
	DebugInfo.Log(NodeName);
	DebugInfo.PushIndent();
	LogActorRepList(DebugInfo, NodeName, ReplicationActorList);
	DebugInfo.PopIndent();
}

// ------------------------------------------------------------------------------
*/
UFortReplicationGraphNode_AlwaysRelevantForSquad::UFortReplicationGraphNode_AlwaysRelevantForSquad()
{
	bRequiresPrepareForReplicationCall = true;
}

void UFortReplicationGraphNode_AlwaysRelevantForSquad::PrepareForReplication()
{
	// In solo, there is no point to this node. So just skip it. It might be slightly better to just delete the node in singles but probably doesn't matter.
	if (PlayerStates.Num() > 1)
	{
		const int32 PreviousNum = ReplicationActorList.Num();

		ReplicationActorList.Reset();
		for (AFortPlayerStateAthena* PS : PlayerStates)
		{
			if (PS)
			{
				if (AFortPlayerPawn* Pawn = PS->GetCurrentPawn())
				{
					ReplicationActorList.ConditionalAdd(Pawn);
				}
			}
		}

		UpdatePerConnectionCullDistances = PreviousNum != ReplicationActorList.Num();
	}
}

void UFortReplicationGraphNode_AlwaysRelevantForSquad::GatherActorListsForConnection(const FConnectionGatherActorListParameters& Params)
{
	if (ReplicationActorList.IsValid())
	{
		Params.OutGatheredReplicationLists.AddReplicationActorList(ReplicationActorList);

		if (UpdatePerConnectionCullDistances)
		{
			for (FActorRepListType Actor : ReplicationActorList)
			{
				FConnectionReplicationActorInfo& ConnectionActorInfo = Params.ConnectionManager.ActorInfoMap.FindOrAdd(Actor);
				ConnectionActorInfo.CullDistanceSquared = 0;
			}
		}
	}
}

void UFortReplicationGraphNode_AlwaysRelevantForSquad::LogNode(FReplicationGraphDebugInfo& DebugInfo, const FString& NodeName) const
{
	DebugInfo.Log(NodeName);
	DebugInfo.PushIndent();
	LogActorRepList(DebugInfo, FString::Printf(TEXT("Squad: %u"), SquadID), ReplicationActorList);
	DebugInfo.PopIndent();
}

// ------------------------------------------------------------------------------

UFortReplicationGraphNode_BroadcastSpectator::UFortReplicationGraphNode_BroadcastSpectator()
{
	// Add all classes we want to always be spatially relevant for a live spectator here.
	ClassesForSpectatorRelevancy.Set(AActor::StaticClass(), false);
	ClassesForSpectatorRelevancy.Set(AFortPlayerPawn::StaticClass(), true);
	ClassesForSpectatorRelevancy.Set(AFortAthenaSupplyDrop::StaticClass(), true);
}

void UFortReplicationGraphNode_BroadcastSpectator::NotifyAddNetworkActor(const FNewReplicatedActorInfo& ActorInfo)
{
	// Only add the actor if it is a subclass of our predefined class which we wish to always be spatially relevant for a spectator.
	if (ClassesForSpectatorRelevancy.GetChecked(ActorInfo.Class))
	{
		ReplicationActorList.Add(ActorInfo.Actor);
	}
}

bool UFortReplicationGraphNode_BroadcastSpectator::NotifyRemoveNetworkActor(const FNewReplicatedActorInfo& ActorInfo, bool WarnIfNotFound)
{
	bool RemovedSomething = false;

	if (ClassesForSpectatorRelevancy.GetChecked(ActorInfo.Class))
	{
		RemovedSomething = ReplicationActorList.Remove(ActorInfo.Actor);
	}

	if (!RemovedSomething && WarnIfNotFound)
	{
		UE_LOG(LogFortReplicationGraph, Warning, TEXT("Attempted to remove %s from list %s but it was not found. (StreamingLevelName == NAME_None)"), *GetActorRepListTypeDebugString(ActorInfo.Actor), *GetFullName());
	}

	return RemovedSomething;
}

void UFortReplicationGraphNode_BroadcastSpectator::GatherActorListsForConnection(const FConnectionGatherActorListParameters& Params)
{
	const AFortLiveBroadcastController* BroadcastPC = Cast<AFortLiveBroadcastController>(Params.Viewer.InViewer);
	if (!BroadcastPC)
	{
		return;
	}

	// This is horrible, shouldn't ahve to iterate every actor for every connection every frame.
	for (FActorRepListType& Actor : ReplicationActorList)
	{
		FConnectionReplicationActorInfo& ConnectionActorInfo = Params.ConnectionManager.ActorInfoMap.FindOrAdd(Actor);
		ConnectionActorInfo.CullDistanceSquared = 0;
	}

	Params.OutGatheredReplicationLists.AddReplicationActorList(ReplicationActorList);
}

// ------------------------------------------------------------------------------

void UFortReplicationGraphNode_AlwaysRelevant_ForConnection::GatherActorListsForConnection(const FConnectionGatherActorListParameters& Params)
{
	QUICK_SCOPE_CYCLE_COUNTER(UFortReplicationGraphNode_AlwaysRelevant_ForConnection_GatherActorListsForConnection);

	UFortReplicationGraph* FortGraph = CastChecked<UFortReplicationGraph>(GetOuter());

	ReplicationActorList.Reset();

	ReplicationActorList.ConditionalAdd(Params.Viewer.InViewer);
	ReplicationActorList.ConditionalAdd(Params.Viewer.ViewTarget);

	if (AFortPlayerController* PC = Cast<AFortPlayerController>(Params.Viewer.InViewer))
	{
		// 50% throttling of Inventories and PlayerStates.
		const bool bReplicatePSandInventory = (Params.ConnectionManager.ConnectionId % 2) == (Params.ReplicationFrameNum % 2);
		if (bReplicatePSandInventory)
		{
			// Always return the player state to the owning player. Simulated proxy player states are handled by UFortReplicationGraphNode_PlayerStateFrequencyLimiter
			if (APlayerState* PS = PC->PlayerState)
			{
				if (!bInitializedPlayerState)
				{
					bInitializedPlayerState = true;
					FConnectionReplicationActorInfo& ConnectionActorInfo = Params.ConnectionManager.ActorInfoMap.FindOrAdd(PS);
					ConnectionActorInfo.ReplicationPeriodFrame = 1;
				}

				ReplicationActorList.ConditionalAdd(PS);
			}
			if (AFortInventory* FortInventory = PC->GetWorldInventory())
			{
				ReplicationActorList.ConditionalAdd(FortInventory);
			}
		}
		
		if (AFortPawn* Pawn = Cast<AFortPawn>(PC->GetPawn()))
		{
			if (Pawn != LastPawn)
			{
				UE_LOG(LogFortReplicationGraph, Verbose, TEXT("Setting connection pawn cull distance to 0. %s"), *Pawn->GetName());
				LastPawn = Pawn;
				FConnectionReplicationActorInfo& ConnectionActorInfo = Params.ConnectionManager.ActorInfoMap.FindOrAdd(Pawn);
				ConnectionActorInfo.CullDistanceSquared = 0.f;
			}

			if (Pawn != Params.Viewer.ViewTarget)
			{
				ReplicationActorList.ConditionalAdd(Pawn);
			}

			for (AFortWeapon* Weapon : Pawn->CurrentWeaponList)
			{
				ReplicationActorList.ConditionalAdd(Weapon);
			}
		}

		if (Params.Viewer.ViewTarget != LastPawn)
		{
			if (AFortPawn* ViewTargetPawn = Cast<AFortPawn>(Params.Viewer.ViewTarget))
			{
				if (AFortPawn* LastFortPawn = Cast<AFortPawn>(LastPawn))
				{
					LastFortPawn->UpdateCullDistance();
				}
				UE_LOG(LogFortReplicationGraph, Verbose, TEXT("Setting connection view target pawn cull distance to 0. %s"), *ViewTargetPawn->GetName());
				LastPawn = ViewTargetPawn;
				FConnectionReplicationActorInfo& ConnectionActorInfo = Params.ConnectionManager.ActorInfoMap.FindOrAdd(ViewTargetPawn);
				ConnectionActorInfo.CullDistanceSquared = 0.f;
			}
		}
	}

	Params.OutGatheredReplicationLists.AddReplicationActorList(ReplicationActorList);

	// Always relevant streaming level actors.
	FPerConnectionActorInfoMap& ConnectionActorInfoMap = Params.ConnectionManager.ActorInfoMap;

	TMap<FName, FActorRepListRefView>& AlwaysRelevantStreamingLevelActors = FortGraph->AlwaysRelevantStreamingLevelActors;

	for (int32 Idx = AlwaysRelevantStreamingLevelsNeedingReplication.Num() - 1; Idx >= 0; --Idx)
	{
		const FName& StreamingLevel = AlwaysRelevantStreamingLevelsNeedingReplication[Idx];

		FActorRepListRefView* Ptr = AlwaysRelevantStreamingLevelActors.Find(StreamingLevel);
		if (Ptr == nullptr)
		{
			// No always relevant lists for that level
			UE_CLOG(CVar_FortRepGraph_DisplayClientLevelStreaming > 0, LogFortReplicationGraph, Display, TEXT("CLIENTSTREAMING Removing %s from AlwaysRelevantStreamingLevelActors because FActorRepListRefView is null. %s "), *StreamingLevel.ToString(), *Params.ConnectionManager.GetName());
			AlwaysRelevantStreamingLevelsNeedingReplication.RemoveAtSwap(Idx, 1, false);
			continue;
		}

		FActorRepListRefView& RepList = *Ptr;

		if (RepList.Num() > 0)
		{
			bool bAllDormant = true;
			for (FActorRepListType Actor : RepList)
			{
				FConnectionReplicationActorInfo& ConnectionActorInfo = ConnectionActorInfoMap.FindOrAdd(Actor);
				if (ConnectionActorInfo.bDormantOnConnection == false)
				{
					bAllDormant = false;
					break;
				}
			}

			if (bAllDormant)
			{
				UE_CLOG(CVar_FortRepGraph_DisplayClientLevelStreaming > 0, LogFortReplicationGraph, Display, TEXT("CLIENTSTREAMING All AlwaysRelevant Actors Dormant on StreamingLevel %s for %s. Removing list."), *StreamingLevel.ToString(), *Params.ConnectionManager.GetName());
				AlwaysRelevantStreamingLevelsNeedingReplication.RemoveAtSwap(Idx, 1, false);
			}
			else
			{
				UE_CLOG(CVar_FortRepGraph_DisplayClientLevelStreaming > 0, LogFortReplicationGraph, Display, TEXT("CLIENTSTREAMING Adding always Actors on StreamingLevel %s for %s because it has at least one non dormant actor"), *StreamingLevel.ToString(), *Params.ConnectionManager.GetName());
				Params.OutGatheredReplicationLists.AddReplicationActorList(RepList);
			}
		}
		else
		{
			UE_LOG(LogFortReplicationGraph, Warning, TEXT("UFortReplicationGraphNode_AlwaysRelevant_ForConnection::GatherActorListsForConnection - empty RepList %s"), *Params.ConnectionManager.GetName());
		}

	}

#if WITH_GAMEPLAY_DEBUGGER
	if (GameplayDebugger)
	{
		ReplicationActorList.ConditionalAdd(GameplayDebugger);
	}
#endif
}

void UFortReplicationGraphNode_AlwaysRelevant_ForConnection::FlushStreamingAlwaysRelevantList(const FNewReplicatedActorInfo& ActorInfo)
{
	UE_CLOG(CVar_FortRepGraph_DisplayClientLevelStreaming > 0, LogFortReplicationGraph, Display, TEXT("CLIENTSTREAMING ::FlushStreamingAlwaysRelevantList - %s"), *ActorInfo.StreamingLevelName.ToString());
	AlwaysRelevantStreamingLevelsNeedingReplication.Add(ActorInfo.StreamingLevelName);
}

void UFortReplicationGraphNode_AlwaysRelevant_ForConnection::OnClientLevelVisibilityAdd(FName LevelName, UWorld* StreamingWorld)
{
	UE_CLOG(CVar_FortRepGraph_DisplayClientLevelStreaming > 0, LogFortReplicationGraph, Display, TEXT("CLIENTSTREAMING ::OnClientLevelVisibilityAdd - %s"), *LevelName.ToString());
	AlwaysRelevantStreamingLevelsNeedingReplication.Add(LevelName);
}

void UFortReplicationGraphNode_AlwaysRelevant_ForConnection::OnClientLevelVisibilityRemove(FName LevelName)
{
	UE_CLOG(CVar_FortRepGraph_DisplayClientLevelStreaming > 0, LogFortReplicationGraph, Display, TEXT("CLIENTSTREAMING ::OnClientLevelVisibilityRemove - %s"), *LevelName.ToString());
    AlwaysRelevantStreamingLevelsNeedingReplication.Remove(LevelName);
}

void UFortReplicationGraphNode_AlwaysRelevant_ForConnection::LogNode(FReplicationGraphDebugInfo& DebugInfo, const FString& NodeName) const
{
	DebugInfo.Log(NodeName);
	DebugInfo.PushIndent();
	LogActorRepList(DebugInfo, NodeName, ReplicationActorList);

	for (const FName& LevelName : AlwaysRelevantStreamingLevelsNeedingReplication)
	{
		UFortReplicationGraph* FortGraph = CastChecked<UFortReplicationGraph>(GetOuter());
		if (FActorRepListRefView* RepList = FortGraph->AlwaysRelevantStreamingLevelActors.Find(LevelName))
		{
			LogActorRepList(DebugInfo, FString::Printf(TEXT("AlwaysRelevant StreamingLevel List: %s"), *LevelName.ToString()), *RepList);
		}
	}

	DebugInfo.PopIndent();
}

// ------------------------------------------------------------------------------

UFortReplicationGraphNode_PlayerStateFrequencyLimiter::UFortReplicationGraphNode_PlayerStateFrequencyLimiter()
{
	bRequiresPrepareForReplicationCall = true;
}

void UFortReplicationGraphNode_PlayerStateFrequencyLimiter::PrepareForReplication()
{
	QUICK_SCOPE_CYCLE_COUNTER(UFortReplicationGraphNode_PlayerStateFrequencyLimiter_GlobalPrepareForReplication);

	ReplicationActorLists.Reset();
	ForceNetUpdateReplicationActorList.Reset();

	ReplicationActorLists.AddDefaulted();
	FActorRepListRefView* CurrentList = &ReplicationActorLists[0];
	CurrentList->PrepareForWrite();

	// We rebuild our lists of player states each frame. This is not as efficient as it could be but its the simplest way
	// to handle players disconnecting and keeping the lists compact. If the lists were persistent we would need to defrag them as players left.

	for (TActorIterator<APlayerState> It(GetWorld()); It; ++It)
	{
		APlayerState* PS = *It;
		if (IsActorValidForReplicationGather(PS) == false)
		{
			continue;
		}

		if (CurrentList->Num() >= TargetActorsPerFrame)
		{
			ReplicationActorLists.AddDefaulted();
			CurrentList = &ReplicationActorLists.Last();
			CurrentList->PrepareForWrite();
		}

		CurrentList->Add(PS);
	}
}

void UFortReplicationGraphNode_PlayerStateFrequencyLimiter::GatherActorListsForConnection(const FConnectionGatherActorListParameters& Params)
{
	const int32 ListIdx = Params.ReplicationFrameNum % ReplicationActorLists.Num();
	Params.OutGatheredReplicationLists.AddReplicationActorList(ReplicationActorLists[ListIdx]);

	if (ForceNetUpdateReplicationActorList.Num() > 0)
	{
		Params.OutGatheredReplicationLists.AddReplicationActorList(ForceNetUpdateReplicationActorList);
	}
}

void UFortReplicationGraphNode_PlayerStateFrequencyLimiter::LogNode(FReplicationGraphDebugInfo& DebugInfo, const FString& NodeName) const
{
	DebugInfo.Log(NodeName);
	DebugInfo.PushIndent();

	int32 i = 0;
	for (const FActorRepListRefView& List : ReplicationActorLists)
	{
		LogActorRepList(DebugInfo, FString::Printf(TEXT("Bucket[%d]"), i++), List);
	}

	DebugInfo.PopIndent();
}

// ------------------------------------------------------------------------------

void UFortReplicationGraph::PrintRepNodePolicies()
{
	UEnum* Enum = FindObject<UEnum>(ANY_PACKAGE, TEXT("EClassRepNodeMapping"));
	if (!Enum)
	{
		return;
	}

	GLog->Logf(TEXT("===================================="));
	GLog->Logf(TEXT("Fortnite Replication Routing Policies"));
	GLog->Logf(TEXT("===================================="));

	for (auto It = ClassRepNodePolicies.CreateIterator(); It; ++It)
	{
		FObjectKey ObjKey = It.Key();

		EClassRepNodeMapping Mapping = It.Value();

		GLog->Logf(TEXT("%-40s --> %s"), *GetNameSafe(ObjKey.ResolveObjectPtr()), *Enum->GetNameStringByValue(static_cast<uint32>(Mapping)));
	}
}

FAutoConsoleCommandWithWorldAndArgs FortPrintRepNodePoliciesCmd(TEXT("FortRepGraph.PrintRouting"), TEXT("Prints how actor classes are routed to RepGraph nodes"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	for (TObjectIterator<UFortReplicationGraph> It; It; ++It)
	{
		It->PrintRepNodePolicies();
	}
})
);

// ------------------------------------------------------------------------------

FAutoConsoleCommandWithWorldAndArgs ChangeFrequencyBucketsCmd(TEXT("FortRepGraph.FrequencyBuckets"), TEXT("Resets frequency bucket count."), FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray< FString >& Args, UWorld* World)
{
	int32 Buckets = 1;
	if (Args.Num() > 0)
	{
		LexTryParseString<int32>(Buckets, *Args[0]);
	}

	UE_LOG(LogFortReplicationGraph, Display, TEXT("Setting Frequency Buckets to %d"), Buckets);

	for (TObjectIterator<UReplicationGraphNode_ActorListFrequencyBuckets> It; It; ++It)
	{
		UReplicationGraphNode_ActorListFrequencyBuckets* Node = *It;
		Node->SetNonStreamingCollectionSize(Buckets);
	}
}));
