// Language=angelscript

//============================================================================
// CK ENTITY SCRIPT - TEST-ONLY SUPPORT: replicated spawns executed in a chosen pass
//============================================================================
//
// Fixtures for CkAutoTest_Net_EntityScript_ReplicatesFromEveryPass. The entity-script spawn processor executes a
// request at one of three points of a frame: its slot in the main pass of FGroup_Gameplay_Script, the group's local
// settle barrier, or the end-of-frame tail pump. The marker-less processors below execute a stamped order in a chosen
// one, so no leg depends on which pass a harness wait continuation lands in. A marker-less processor is never pumped:
// it runs once per frame, in the main pass of its group, at the position its declared edge gives it.
// - Main:   before the spawn processor, so the spawn executes in the script group's main pass;
// - Settle: after the begin-play processor in the same group, so the spawn executes in the group's settle barrier;
// - Tail:   in FGroup_Replication, after the script group, so the spawn executes in the end-of-frame tail pump.
// The same three shapes finish a Continue-flow script's construction in a chosen pass of a LATER frame than the one
// that built it (the finish processors hold the order until then).
//
// TESTONLY: registered globally like every script processor; each query requires a test-only fragment, so outside the
// test they visit nothing.
//============================================================================

namespace constants_entity_script_replication_test
{
    const FName k_Tag = n"TAG_CkTestEntityScriptReplication";
}

//--------------------------------------------------------------------------------------------------------------------------
// The scripts under test
//--------------------------------------------------------------------------------------------------------------------------

// What this machine's copy received; never replicated.
struct FCk_TESTONLY_EntityScriptReplication_Local
{
    UPROPERTY()
    int32 Payload = 0;

    UPROPERTY()
    int64 ConstructedFrame = 0;

    // Set by the Continue-flow script only.
    UPROPERTY()
    TWeakObjectPtr<UCk_TESTONLY_EntityScriptReplication_Continue_EntityScript> ContinueScript;
}

struct FCk_TESTONLY_EntityScriptReplication_FinishedParams
{
    UPROPERTY()
    int32 Payload = 0;

    UPROPERTY()
    bool ChildInConstruct = false;

    FCk_TESTONLY_EntityScriptReplication_FinishedParams() {}

    FCk_TESTONLY_EntityScriptReplication_FinishedParams(int32 InPayload, bool InChildInConstruct)
    {
        Payload = InPayload;
        ChildInConstruct = InChildInConstruct;
    }
}

struct FCk_TESTONLY_EntityScriptReplication_ContinueParams
{
    UPROPERTY()
    int32 Payload = 0;

    FCk_TESTONLY_EntityScriptReplication_ContinueParams() {}

    FCk_TESTONLY_EntityScriptReplication_ContinueParams(int32 InPayload)
    {
        Payload = InPayload;
    }
}

// Finishes construction in DoConstruct. With ChildInConstruct it requests one Replicates child of itself there, on
// every machine (a client's request is the placeholder the replicated child resolves), payload Payload * 10 + 1.
class UCk_TESTONLY_EntityScriptReplication_Finished_EntityScript : UCk_GenericEntityScript_UE
{
    default _ShowInPlaceActors = false;
    default _Replication = ECk_Replication::Replicates;

    UPROPERTY(ExposeOnSpawn)
    int32 Payload = 0;

    UPROPERTY(ExposeOnSpawn)
    bool ChildInConstruct = false;

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    {
        utils_entity_script_replication_test::Add_Local(InHandle, Payload);

        if (ChildInConstruct)
        {
            auto Params = FCk_TESTONLY_EntityScriptReplication_FinishedParams(
                utils_entity_script_replication_test::Get_ChildPayload(Payload), false);
            utils_entity_script::Request_SpawnEntity(InHandle, UCk_TESTONLY_EntityScriptReplication_Finished_EntityScript, Params);
        }

        return ECk_EntityScript_ConstructionFlow::Finished;
    }
}

// Keeps construction open on the authority until a finish order is executed for it; a client's copy finishes in
// DoConstruct (it has nothing to wait for).
class UCk_TESTONLY_EntityScriptReplication_Continue_EntityScript : UCk_GenericEntityScript_UE
{
    default _ShowInPlaceActors = false;
    default _Replication = ECk_Replication::Replicates;

    UPROPERTY(ExposeOnSpawn)
    int32 Payload = 0;

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    {
        auto& Local = utils_entity_script_replication_test::Add_Local(InHandle, Payload);
        Local.ContinueScript = this;

        if (utils_net::Get_HasAuthority(InHandle) == false)
        { return ECk_EntityScript_ConstructionFlow::Finished; }

        return ECk_EntityScript_ConstructionFlow::Continue;
    }

    void Finish()
    {
        DoFinishConstruction();
    }
}

//--------------------------------------------------------------------------------------------------------------------------
// Orders
//--------------------------------------------------------------------------------------------------------------------------

enum ECk_TESTONLY_EntityScriptReplication_Kind
{
    Finished,
    // A Finished script that requests a Replicates child in its DoConstruct.
    FinishedWithChild,
    Continue
}

enum ECk_TESTONLY_EntityScriptReplication_Pass
{
    Main,
    Settle,
    Tail
}

struct FCk_TESTONLY_EntityScriptReplication_Order
{
    UPROPERTY()
    ECk_TESTONLY_EntityScriptReplication_Kind Kind = ECk_TESTONLY_EntityScriptReplication_Kind::Finished;

    UPROPERTY()
    int32 Payload = 0;

    FCk_TESTONLY_EntityScriptReplication_Order() {}

    FCk_TESTONLY_EntityScriptReplication_Order(ECk_TESTONLY_EntityScriptReplication_Kind InKind, int32 InPayload)
    {
        Kind = InKind;
        Payload = InPayload;
    }
}

struct FCk_TESTONLY_EntityScriptReplication_SpawnAtMain
{
    UPROPERTY()
    TArray<FCk_TESTONLY_EntityScriptReplication_Order> Orders;
}

struct FCk_TESTONLY_EntityScriptReplication_SpawnAtSettle
{
    UPROPERTY()
    TArray<FCk_TESTONLY_EntityScriptReplication_Order> Orders;
}

struct FCk_TESTONLY_EntityScriptReplication_SpawnAtTail
{
    UPROPERTY()
    TArray<FCk_TESTONLY_EntityScriptReplication_Order> Orders;
}

// Stamped on a Continue-flow entity: finish its construction in the main pass of a later frame than the one that built it.
struct FCk_TESTONLY_EntityScriptReplication_FinishAtMain
{
    UPROPERTY()
    bool Requested = true;
}

// Stamped on a Continue-flow entity: finish its construction in FGroup_Replication of a later frame, so the finish tag
// lands after the replicate request was already seen and is first consumed in the tail pump.
struct FCk_TESTONLY_EntityScriptReplication_FinishAtTail
{
    UPROPERTY()
    bool Requested = true;
}

// One row of the test's table: what to spawn, in which pass, and (Continue flow) in which pass of a later frame to finish it.
struct FCk_TESTONLY_EntityScriptReplication_Leg
{
    UPROPERTY()
    FString Name;

    UPROPERTY()
    FCk_TESTONLY_EntityScriptReplication_Order Order;

    UPROPERTY()
    ECk_TESTONLY_EntityScriptReplication_Pass SpawnPass = ECk_TESTONLY_EntityScriptReplication_Pass::Main;

    // Read for a Continue-flow leg only.
    UPROPERTY()
    ECk_TESTONLY_EntityScriptReplication_Pass FinishPass = ECk_TESTONLY_EntityScriptReplication_Pass::Tail;

    FCk_TESTONLY_EntityScriptReplication_Leg() {}

    FCk_TESTONLY_EntityScriptReplication_Leg(const FString& InName, const FCk_TESTONLY_EntityScriptReplication_Order& InOrder, ECk_TESTONLY_EntityScriptReplication_Pass InSpawnPass)
    {
        Name = InName;
        Order = InOrder;
        SpawnPass = InSpawnPass;
    }
}

namespace utils_entity_script_replication_test
{
    int32 Get_ChildPayload(int32 InParentPayload)
    { return InParentPayload * 10 + 1; }

    FCk_TESTONLY_EntityScriptReplication_Local& Add_Local(FCk_Handle& InHandle, int32 InPayload)
    {
        utils_entity_tag::Add(InHandle, constants_entity_script_replication_test::k_Tag);

        auto Local = FCk_TESTONLY_EntityScriptReplication_Local();
        Local.Payload = InPayload;
        Local.ConstructedFrame = utils_stats::Get_FrameCount();
        InHandle.Add_Fragment(Local);

        return InHandle.Get_Fragment(FCk_TESTONLY_EntityScriptReplication_Local);
    }

    void Add_SpawnOrder(FCk_Handle& InOwner, ECk_TESTONLY_EntityScriptReplication_Pass InPass, const FCk_TESTONLY_EntityScriptReplication_Order& InOrder)
    {
        switch (InPass)
        {
            case ECk_TESTONLY_EntityScriptReplication_Pass::Main:
            { InOwner.AddOrGet_Fragment(FCk_TESTONLY_EntityScriptReplication_SpawnAtMain).Orders.Add(InOrder); break; }
            case ECk_TESTONLY_EntityScriptReplication_Pass::Settle:
            { InOwner.AddOrGet_Fragment(FCk_TESTONLY_EntityScriptReplication_SpawnAtSettle).Orders.Add(InOrder); break; }
            case ECk_TESTONLY_EntityScriptReplication_Pass::Tail:
            { InOwner.AddOrGet_Fragment(FCk_TESTONLY_EntityScriptReplication_SpawnAtTail).Orders.Add(InOrder); break; }
        }
    }

    // Only Main and Tail exist for a finish: a finish requested in the settle is not a shape this test needs.
    void Add_FinishOrder(FCk_Handle& InEntity, ECk_TESTONLY_EntityScriptReplication_Pass InPass)
    {
        ck::EnsureIfNot(InPass != ECk_TESTONLY_EntityScriptReplication_Pass::Settle,
            "[EntityScriptReplicationTest] a finish order has no Settle shape");

        if (InPass == ECk_TESTONLY_EntityScriptReplication_Pass::Main)
        { InEntity.AddOrGet_Fragment(FCk_TESTONLY_EntityScriptReplication_FinishAtMain); }
        else
        { InEntity.AddOrGet_Fragment(FCk_TESTONLY_EntityScriptReplication_FinishAtTail); }
    }

    void Execute(FCk_Handle& InOwner, const TArray<FCk_TESTONLY_EntityScriptReplication_Order>& InOrders)
    {
        for (const auto& Order : InOrders)
        {
            switch (Order.Kind)
            {
                case ECk_TESTONLY_EntityScriptReplication_Kind::Finished:
                {
                    auto Params = FCk_TESTONLY_EntityScriptReplication_FinishedParams(Order.Payload, false);
                    utils_entity_script::Request_SpawnEntity(InOwner, UCk_TESTONLY_EntityScriptReplication_Finished_EntityScript, Params);
                    break;
                }
                case ECk_TESTONLY_EntityScriptReplication_Kind::FinishedWithChild:
                {
                    auto Params = FCk_TESTONLY_EntityScriptReplication_FinishedParams(Order.Payload, true);
                    utils_entity_script::Request_SpawnEntity(InOwner, UCk_TESTONLY_EntityScriptReplication_Finished_EntityScript, Params);
                    break;
                }
                case ECk_TESTONLY_EntityScriptReplication_Kind::Continue:
                {
                    auto Params = FCk_TESTONLY_EntityScriptReplication_ContinueParams(Order.Payload);
                    utils_entity_script::Request_SpawnEntity(InOwner, UCk_TESTONLY_EntityScriptReplication_Continue_EntityScript, Params);
                    break;
                }
            }
        }
    }

    // False while the frame that built InEntity is still running: the finish must land in a later one.
    bool Get_IsPastConstructionFrame(const FCk_Handle& InEntity)
    {
        return utils_stats::Get_FrameCount() > InEntity.Get_Fragment(FCk_TESTONLY_EntityScriptReplication_Local).ConstructedFrame;
    }

    void Finish(const FCk_Handle& InEntity)
    {
        auto Script = InEntity.Get_Fragment(FCk_TESTONLY_EntityScriptReplication_Local).ContinueScript.Get();
        if (ck::EnsureIfNot(IsValid(Script), f"[EntityScriptReplicationTest] [{InEntity.ToString()}] has no Continue-flow script to finish"))
        { return; }

        Script.Finish();
    }

    void Collect(FCk_ScriptQueryBatch InBatch, TArray<FCk_Handle>& OutHandles)
    {
        OutHandles.Empty();
        for (int32 Index = 0; Index < InBatch.Num(); ++Index)
        { OutHandles.Add(InBatch.GetHandle(Index)); }
    }

    // The live entity on InAny's machine whose DoConstruct received InPayload; invalid while it has not arrived.
    FCk_Handle Find(const FCk_Handle& InAny, int32 InPayload)
    {
        for (auto Candidate : utils_entity_tag::ForEach_Entity(InAny, constants_entity_script_replication_test::k_Tag))
        {
            if (utils_entity_lifetime::Get_IsPendingDestroy(Candidate, ECk_EntityLifetime_DestructionPhase::BeginDestroy))
            { continue; }

            if (Candidate.Has_Fragment(FCk_TESTONLY_EntityScriptReplication_Local) == false)
            { continue; }

            if (Candidate.Get_Fragment(FCk_TESTONLY_EntityScriptReplication_Local).Payload == InPayload)
            { return Candidate; }
        }

        return FCk_Handle();
    }

    bool Get_IsReplicationComplete(const FCk_Handle& InEntity)
    {
        return ck::IsValid(InEntity) && utils_entity_replication_driver::Has(InEntity)
            && utils_entity_replication_driver::Get_IsReplicationComplete(InEntity);
    }

    // How a test line spells the host's replication state of InEntity.
    FString Get_HostState(const FCk_Handle& InEntity)
    {
        if (ck::Is_NOT_Valid(InEntity))
        { return "absent"; }

        if (utils_entity_replication_driver::Has(InEntity) == false)
        { return "noDriver"; }

        return utils_entity_replication_driver::Get_IsReplicationComplete(InEntity) ? "complete" : "notComplete";
    }
}

//--------------------------------------------------------------------------------------------------------------------------
// Forced-pass processors
//--------------------------------------------------------------------------------------------------------------------------

class UCk_TESTONLY_Processor_EntityScriptReplication_SpawnAtMain : UCk_Processor_Script_Base_UE
{
    default _Group = n"FGroup_Gameplay_Script";
    default _RunBefore.Add(n"FProcessor_EntityScript_SpawnEntity_HandleRequests");

    private TArray<FCk_Handle> _Collected;

    UFUNCTION(BlueprintOverride)
    void Configure(FCk_ScriptProcessorQuery& Query)
    {
        Query.Require(FCk_TESTONLY_EntityScriptReplication_SpawnAtMain);
    }

    UFUNCTION(BlueprintOverride)
    void ForEachBatch(FCk_ScriptQueryBatch Batch, FCk_Time InDeltaT)
    {
        utils_entity_script_replication_test::Collect(Batch, _Collected);
        for (int32 Index = 0; Index < _Collected.Num(); ++Index)
        {
            auto Owner = _Collected[Index];
            const auto Orders = Owner.Get_Fragment(FCk_TESTONLY_EntityScriptReplication_SpawnAtMain).Orders;
            Owner.Request_TryRemove(FCk_TESTONLY_EntityScriptReplication_SpawnAtMain);
            utils_entity_script_replication_test::Execute(Owner, Orders);
        }
    }
}

class UCk_TESTONLY_Processor_EntityScriptReplication_SpawnAtSettle : UCk_Processor_Script_Base_UE
{
    default _Group = n"FGroup_Gameplay_Script";
    default _RunAfter.Add(n"FProcessor_EntityScript_BeginPlay");

    private TArray<FCk_Handle> _Collected;

    UFUNCTION(BlueprintOverride)
    void Configure(FCk_ScriptProcessorQuery& Query)
    {
        Query.Require(FCk_TESTONLY_EntityScriptReplication_SpawnAtSettle);
    }

    UFUNCTION(BlueprintOverride)
    void ForEachBatch(FCk_ScriptQueryBatch Batch, FCk_Time InDeltaT)
    {
        utils_entity_script_replication_test::Collect(Batch, _Collected);
        for (int32 Index = 0; Index < _Collected.Num(); ++Index)
        {
            auto Owner = _Collected[Index];
            const auto Orders = Owner.Get_Fragment(FCk_TESTONLY_EntityScriptReplication_SpawnAtSettle).Orders;
            Owner.Request_TryRemove(FCk_TESTONLY_EntityScriptReplication_SpawnAtSettle);
            utils_entity_script_replication_test::Execute(Owner, Orders);
        }
    }
}

class UCk_TESTONLY_Processor_EntityScriptReplication_SpawnAtTail : UCk_Processor_Script_Base_UE
{
    default _Group = n"FGroup_Replication";

    private TArray<FCk_Handle> _Collected;

    UFUNCTION(BlueprintOverride)
    void Configure(FCk_ScriptProcessorQuery& Query)
    {
        Query.Require(FCk_TESTONLY_EntityScriptReplication_SpawnAtTail);
    }

    UFUNCTION(BlueprintOverride)
    void ForEachBatch(FCk_ScriptQueryBatch Batch, FCk_Time InDeltaT)
    {
        utils_entity_script_replication_test::Collect(Batch, _Collected);
        for (int32 Index = 0; Index < _Collected.Num(); ++Index)
        {
            auto Owner = _Collected[Index];
            const auto Orders = Owner.Get_Fragment(FCk_TESTONLY_EntityScriptReplication_SpawnAtTail).Orders;
            Owner.Request_TryRemove(FCk_TESTONLY_EntityScriptReplication_SpawnAtTail);
            utils_entity_script_replication_test::Execute(Owner, Orders);
        }
    }
}

class UCk_TESTONLY_Processor_EntityScriptReplication_FinishAtMain : UCk_Processor_Script_Base_UE
{
    default _Group = n"FGroup_Gameplay_Script";
    default _RunBefore.Add(n"FProcessor_EntityScript_SpawnEntity_HandleRequests");

    private TArray<FCk_Handle> _Collected;

    UFUNCTION(BlueprintOverride)
    void Configure(FCk_ScriptProcessorQuery& Query)
    {
        Query.Require(FCk_TESTONLY_EntityScriptReplication_FinishAtMain);
        Query.Require(FCk_TESTONLY_EntityScriptReplication_Local);
    }

    UFUNCTION(BlueprintOverride)
    void ForEachBatch(FCk_ScriptQueryBatch Batch, FCk_Time InDeltaT)
    {
        utils_entity_script_replication_test::Collect(Batch, _Collected);
        for (int32 Index = 0; Index < _Collected.Num(); ++Index)
        {
            auto Entity = _Collected[Index];
            if (utils_entity_script_replication_test::Get_IsPastConstructionFrame(Entity) == false)
            { continue; }

            Entity.Request_TryRemove(FCk_TESTONLY_EntityScriptReplication_FinishAtMain);
            utils_entity_script_replication_test::Finish(Entity);
        }
    }
}

class UCk_TESTONLY_Processor_EntityScriptReplication_FinishAtTail : UCk_Processor_Script_Base_UE
{
    default _Group = n"FGroup_Replication";

    private TArray<FCk_Handle> _Collected;

    UFUNCTION(BlueprintOverride)
    void Configure(FCk_ScriptProcessorQuery& Query)
    {
        Query.Require(FCk_TESTONLY_EntityScriptReplication_FinishAtTail);
        Query.Require(FCk_TESTONLY_EntityScriptReplication_Local);
    }

    UFUNCTION(BlueprintOverride)
    void ForEachBatch(FCk_ScriptQueryBatch Batch, FCk_Time InDeltaT)
    {
        utils_entity_script_replication_test::Collect(Batch, _Collected);
        for (int32 Index = 0; Index < _Collected.Num(); ++Index)
        {
            auto Entity = _Collected[Index];
            if (utils_entity_script_replication_test::Get_IsPastConstructionFrame(Entity) == false)
            { continue; }

            Entity.Request_TryRemove(FCk_TESTONLY_EntityScriptReplication_FinishAtTail);
            utils_entity_script_replication_test::Finish(Entity);
        }
    }
}
