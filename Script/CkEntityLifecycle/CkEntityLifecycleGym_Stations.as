// Language=angelscript

USTRUCT()
struct FCkLifecycleGym_Params
{
    UPROPERTY() FTransform InitialTransform = FTransform::Identity;
    UPROPERTY() bool SharedAuthority = false;
    UPROPERTY() int32 BranchIndex = -1;
    UPROPERTY() int32 BranchGeneration = 0;
}

USTRUCT()
struct FCkLifecycleGym_RowSample
{
    UPROPERTY() int32 BranchGeneration = 0;
    UPROPERTY() FString Title;
    UPROPERTY() FString Body;
    UPROPERTY() FString Instructions;
    UPROPERTY() TArray<bool> Results;
    UPROPERTY() TArray<int32> Counters;
    UPROPERTY() int32 Phase = -1;
    UPROPERTY() int32 Cycle = 0;
    UPROPERTY() float32 ElapsedMilliseconds = 0.0f;
    UPROPERTY() float32 GoalMilliseconds = 0.0f;
    UPROPERTY() int32 ActualCallbackCount = 0;
    UPROPERTY() int32 LastCompletedCycle = -1;
    UPROPERTY() int32 LastCycleCallbackCount = 0;

}

USTRUCT()
struct FCkLifecycleGym_Snapshot
{
    UPROPERTY() int32 Generation = 0;
    UPROPERTY() int32 Sequence = 0;
    UPROPERTY() bool Ready = false;
    UPROPERTY() bool Failed = false;
    UPROPERTY() FString FailureReason;
    UPROPERTY() TArray<FCkLifecycleGym_RowSample> Rows;
}

USTRUCT()
struct FCkLifecycleGym_ControlCommit
{
    UPROPERTY() int32 RequestSerial = 0;
    UPROPERTY() int32 DeliverySerial = 0;
    UPROPERTY() int32 Row = 0;
    UPROPERTY() FCkLifecycleGym_Snapshot Snapshot;
}

namespace CkLifecycleGym
{
	TArray<FCkGym_Station_SpawnParams_Payload> Get_Stations()
	{
		auto Stations = TArray<FCkGym_Station_SpawnParams_Payload>();

		// Station 1: Handle & Entity Basics
		{
			auto Station = FCkGym_Station_SpawnParams_Payload();
			Station.Tags.Add(n"Gym.EntityLifecycle.HandleAndEntity");
			Station.Title = FText::FromString("HANDLE & ENTITY BASICS");
			auto Description = TArray<FText>();
			Description.Add(FText::FromString("Tests handle validity, debug names, entity decomposition,"));
			Description.Add(FText::FromString("comparison, and string conversion."));
			Station.Description = Description;
			Stations.Add(Station);
		}

		// Station 2: Ownership Tree
		{
			auto Station = FCkGym_Station_SpawnParams_Payload();
			Station.Tags.Add(n"Gym.EntityLifecycle.OwnershipTree");
			Station.Title = FText::FromString("OWNERSHIP TREE");
			auto Description = TArray<FText>();
			Description.Add(FText::FromString("Creates parent-child entity hierarchy and queries"));
			Description.Add(FText::FromString("ownership relationships, dependents, and transient state."));
			Station.Description = Description;
			Stations.Add(Station);
		}

		// Station 3: Destroy & Callbacks
		{
			auto Station = FCkGym_Station_SpawnParams_Payload();
			Station.Tags.Add(n"Gym.EntityLifecycle.DestroyCallbacks");
			Station.Title = FText::FromString("DESTROY & CALLBACKS");
			auto Description = TArray<FText>();
			Description.Add(FText::FromString("Tests entity destruction, batch destruction,"));
			Description.Add(FText::FromString("and OnBeginDestroy callbacks in automated cycles."));
			Station.Description = Description;
			Stations.Add(Station);
		}

		// Station 4: Actor Bridge
		{
			auto Station = FCkGym_Station_SpawnParams_Payload();
			Station.Tags.Add(n"Gym.EntityLifecycle.ActorBridge");
			Station.Title = FText::FromString("ACTOR BRIDGE");
			auto Description = TArray<FText>();
			Description.Add(FText::FromString("Dedicated replicated bridge probe: Actor-to-Handle roundtrip,"));
			Description.Add(FText::FromString("ECS readiness, and replication status."));
			Station.Description = Description;
			Stations.Add(Station);
		}

		// Station 5: Entity Tags
		{
			auto Station = FCkGym_Station_SpawnParams_Payload();
			Station.Tags.Add(n"Gym.EntityLifecycle.TagSystem");
			Station.Title = FText::FromString("ENTITY TAGS");
			auto Description = TArray<FText>();
			Description.Add(FText::FromString("Tests adding, querying, removing, and searching"));
			Description.Add(FText::FromString("by entity tags in automated cycles."));
			Station.Description = Description;
			Stations.Add(Station);
		}

		// Station 6: Deferred Entity
		{
			auto Station = FCkGym_Station_SpawnParams_Payload();
			Station.Tags.Add(n"Gym.EntityLifecycle.DeferredSetup");
			Station.Title = FText::FromString("DEFERRED ENTITY");
			auto Description = TArray<FText>();
			Description.Add(FText::FromString("Tests deferred entity creation, pending state queries,"));
			Description.Add(FText::FromString("setup completion, and completion callbacks."));
			Station.Description = Description;
			Stations.Add(Station);
		}

		// Station 7: Script Spawn & Cast
		{
			auto Station = FCkGym_Station_SpawnParams_Payload();
			Station.Tags.Add(n"Gym.EntityLifecycle.ScriptSpawnCast");
			Station.Title = FText::FromString("SCRIPT SPAWN & CAST");
			auto Description = TArray<FText>();
			Description.Add(FText::FromString("Tests entity script spawning, construction callbacks,"));
			Description.Add(FText::FromString("type queries, and handle casting."));
			Station.Description = Description;
			Stations.Add(Station);
		}

        for (int32 i = 0; i < Stations.Num(); ++i)
        {
            auto Transform = FTransform::Identity;
            Transform.SetLocation(FVector(500.0, -2400.0 + i * 800.0, 0.0));
            Transform.SetRotation(FRotator(0.0, 180.0, 0.0).Quaternion());
            Stations[i].Transform = Transform;
        }
        return Stations;
	}

    bool IsLive(FCk_Handle H)
    { return ck::IsValid(H) && !utils_entity_lifetime::Get_IsPendingDestroy(H, ECk_EntityLifetime_DestructionPhase::BeginDestroy); }
    bool IsDirect(FCk_Handle H, FCk_Handle Owner)
    { return IsLive(H) && IsLive(Owner) && utils_entity_lifetime::Has_LifetimeOwner(H) && utils_entity_lifetime::Get_LifetimeOwner(H) == Owner; }
    bool AreAnchorsValid(FCkGym_Station_Anchors A, int32 i)
    {
        return i >= 0 && i < 7 && A.FootprintCenter == FVector(500.0, -2400.0 + i * 800.0, 0.0) &&
            A.PanelCenter.X == A.FootprintCenter.X && A.PanelCenter.Y == A.FootprintCenter.Y &&
            A.PanelCenter.Z > A.FootprintCenter.Z && A.PanelCenter.Z < 1000000.0;
    }
    ACk_EntityLifecycleGym_GameState Get_State()
    {
        TArray<ACk_EntityLifecycleGym_GameState> States;
        GetAllActorsOfClass(ACk_EntityLifecycleGym_GameState, States);
        if (States.Num() != 1 || ck::Is_NOT_Valid(States[0])) { return nullptr; }
        return States[0];
    }
    FCk_Handle FindStation(FName Tag)
    {
        auto World = ck::TransientEntity();
        if (!IsLive(World)) { return FCk_Handle(); }
        auto Matches = utils_entity_tag::ForEach_Entity(World, Tag);
        if (Matches.Num() != 1 || !IsLive(Matches[0]) || !Matches[0].Has_Fragment(FCkGym_Station_Anchors)) { return FCk_Handle(); }
        return Matches[0];
    }
    FName ChildTag(int32 i)
    {
        if (i == 0) { return n"TAG_LifecycleGym_HandleAndEntity"; }
        if (i == 1) { return n"TAG_LifecycleGym_OwnershipTree"; }
        if (i == 2) { return n"TAG_LifecycleGym_DestroyCallbacks"; }
        if (i == 3) { return n"TAG_LifecycleGym_ActorBridge"; }
        if (i == 4) { return n"TAG_LifecycleGym_TagSystem"; }
        if (i == 5) { return n"TAG_LifecycleGym_DeferredSetup"; }
        if (i == 6) { return n"TAG_LifecycleGym_ScriptSpawnCast"; }

        return FName();
    }
}

// Lifecycle originals derive only common validation/sampling state; their real
// phase execution and delegate implementations remain in the original files.
class UCkLifecycleGym_DemoBase : UCk_GenericEntityScript_UE
{
    default _Replication = ECk_Replication::DoesNotReplicate;
    UPROPERTY(ExposeOnSpawn) bool SharedAuthority = false;
    UPROPERTY(ExposeOnSpawn) int32 BranchIndex = -1;
    UPROPERTY(ExposeOnSpawn) int32 BranchGeneration = 0;
    UPROPERTY() bool Closed = false;
    UPROPERTY() bool Failed = false;
    UPROPERTY() FCkLifecycleGym_RowSample Sample;
    UPROPERTY() TArray<FCk_Handle> ExtraRetirement;
    UPROPERTY() int32 ActualCallbackCount = 0;
    UPROPERTY() int32 LastCompletedCycle = -1;
    UPROPERTY() int32 LastCycleCallbackCount = 0;

    void Remember(FCk_Handle H)
    {
        for (int32 i = ExtraRetirement.Num() - 1; i >= 0; --i)
        { if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(ExtraRetirement[i])) { ExtraRetirement.RemoveAt(i); } }
        if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H) && !ExtraRetirement.Contains(H)) { ExtraRetirement.Add(H); }
    }
    bool CanRun(FCk_Handle H)
    {
        if (Closed || Failed || !CkLifecycleGym::IsLive(H)) { return false; }
        if (!SharedAuthority) { return true; }
        auto State = CkLifecycleGym::Get_State();
        return ck::IsValid(State) && State.HasAuthority() && State.Get_IsCurrentBranch(BranchIndex, BranchGeneration);
    }
    void FailDemo(FString Reason)
    {
        Failed = true; Closed = true;
        auto State = CkLifecycleGym::Get_State();
        if (SharedAuthority && ck::IsValid(State) && State.HasAuthority()) { State.Request_Fail(Reason); }
        else
        {
            for (auto H : ExtraRetirement) { if (CkLifecycleGym::IsLive(H)) { utils_entity_lifetime::Request_DestroyEntity(H); } }
            auto H = ck::ToEntity(this);
            if (CkLifecycleGym::IsLive(H)) { utils_entity_lifetime::Request_DestroyEntity(H); }
        }
    }
    bool CheckConstruct(FCk_Handle H)
    {
        if (!CanRun(H) || !utils_entity_lifetime::Get_CanCreateEntity(H))
        { FailDemo("Lifecycle original construction context rejected"); return false; }
        return true;
    }
}

class UCk_EntityScript_LifecycleGym_BridgeRoot : UCk_EntityScript_WithActor_UE
{ default _Replication = ECk_Replication::Replicates; }

UCLASS()
class ACk_LifecycleGym_BridgeProbe : AActor
{
    default bReplicates = true;
    default bAlwaysRelevant = true;
    default bReplicateUsingRegisteredSubObjectList = true;
    UPROPERTY(Replicated) int32 BranchGeneration = 0;
    UPROPERTY(DefaultComponent, RootComponent) USceneComponent SceneRoot;
    default SceneRoot.Mobility = EComponentMobility::Movable;
    // Authority director calls OnActor once; clients only receive actual roots.
}
