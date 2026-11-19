// Language=angelscript

//============================================================================
// CK ENTITY SCRIPT - AUTOMATION TEST: SHARED FIXTURES FOR ORDERED ENTITY SCRIPTS
//============================================================================
//
// Fixture scripts that record, per step of the entity-script pipeline, the
// index they were spawned with, on a recorder entity (the Recorder spawn
// param):
//   - Construct:     the script's DoConstruct.
//   - OnConstructed: the finish step's broadcast, observed through
//                    Promise_OnConstructed on the pending handle. Whoever
//                    requests a fixture binds its OnOrderingEntryConstructed
//                    (see Request_Spawn), so a test class and a fixture that
//                    spawns a child both declare that UFUNCTION.
//   - BeginPlay / EndPlay.
// Every step also appends to one event list ("c2" = index 2 constructed,
// "f" = OnConstructed, "b" = BeginPlay, "e" = EndPlay), so a test can read the
// interleaving of steps across scripts.
//
// The classes differ only in their ordering defaults:
//   Unordered     - the default policy.
//   Ordered       - DefaultGroup.
//   OrderedOther  - DefaultGroup, a second class.
//   GroupX/GroupY - CustomGroup, two groups.
//   GroupMissing  - CustomGroup naming no group (invalid: ensures at request
//                   time and is ordered in the default group).
//   ChurnParent   - unordered, its DoConstruct spawns an Unordered child.
//============================================================================

namespace Ck
{
    asset Asset_AutoTest_EntityScript_Ordering_Tags of UCk_GameplayTags
    {
        GameplayTags.Add(n"AutoTest.EntityScript.Ordering.X");
        GameplayTags.Add(n"AutoTest.EntityScript.Ordering.Y");
    }
}

struct FCk_Fragment_AutoTest_EntityScriptOrdering_Record
{
    UPROPERTY()
    TArray<int32> Constructed;

    UPROPERTY()
    TArray<int32> Finished;

    UPROPERTY()
    TArray<int32> Begun;

    UPROPERTY()
    TArray<int64> BegunFrames;

    UPROPERTY()
    TArray<int32> Ended;

    // Indices whose script entity carried an ordering sequence at its DoConstruct.
    UPROPERTY()
    TArray<int32> Sequenced;

    // Parallel to Sequenced: the group and sequence each of those entities was stamped with.
    UPROPERTY()
    TArray<FGameplayTag> StampGroups;

    UPROPERTY()
    TArray<int64> StampSequences;

    UPROPERTY()
    TArray<FString> Events;
}

// Stamped on each fixture's own entity at DoConstruct, so whoever observes its
// OnConstructed can find its index and recorder.
struct FCk_Fragment_AutoTest_EntityScriptOrdering_Entry
{
    UPROPERTY()
    int32 Index = 0;

    UPROPERTY()
    FCk_Handle Recorder;
}

USTRUCT()
struct FCk_AutoTest_EntityScriptOrdering_Params
{
    UPROPERTY()
    int32 Index = 0;

    UPROPERTY()
    FCk_Handle Recorder;

    // When valid, the script requests this entity's destruction in its DoBeginPlay.
    UPROPERTY()
    FCk_Handle DestroyOnBeginPlay;

    // When >= 0, the script's DoConstruct requests an Ordered child with this index, owned by the script.
    UPROPERTY()
    int32 ChildIndex = -1;

    FCk_AutoTest_EntityScriptOrdering_Params() {}
    FCk_AutoTest_EntityScriptOrdering_Params(int32 InIndex, FCk_Handle InRecorder)
    {
        Index = InIndex;
        Recorder = InRecorder;
    }
}

namespace utils_autotest_entity_script_ordering
{
    FGameplayTag Get_GroupX()
    {
        return utils_gameplay_tag::ResolveGameplayTag(n"AutoTest.EntityScript.Ordering.X");
    }

    FGameplayTag Get_GroupY()
    {
        return utils_gameplay_tag::ResolveGameplayTag(n"AutoTest.EntityScript.Ordering.Y");
    }

    // Requests one fixture at InOwner and binds InListener's OnOrderingEntryConstructed to its OnConstructed.
    FCk_Handle_PendingEntityScript Request_Spawn(
        FCk_Handle InOwner,
        TSubclassOf<UCk_EntityScript_UE> InClass,
        const FCk_AutoTest_EntityScriptOrdering_Params& InParams,
        UObject InListener)
    {
        auto Owner = InOwner;
        auto Pending = utils_entity_script::Request_SpawnEntity(Owner, InClass, FInstancedStruct::Make(InParams));
        Pending.Promise_OnConstructed(FCk_Delegate_EntityScript_Constructed(InListener, n"OnOrderingEntryConstructed"));
        return Pending;
    }

    void Add_Record(FCk_Handle InRecorder)
    {
        auto Recorder = InRecorder;
        Recorder.AddOrGet_Fragment(FCk_Fragment_AutoTest_EntityScriptOrdering_Record);
    }

    void Request_ClearRecord(FCk_Handle InRecorder)
    {
        auto Recorder = InRecorder;
        auto& Record = Recorder.AddOrGet_Fragment(FCk_Fragment_AutoTest_EntityScriptOrdering_Record);
        Record.Constructed.Empty();
        Record.Finished.Empty();
        Record.Begun.Empty();
        Record.BegunFrames.Empty();
        Record.Ended.Empty();
        Record.Sequenced.Empty();
        Record.StampGroups.Empty();
        Record.StampSequences.Empty();
        Record.Events.Empty();
    }

    FCk_Fragment_AutoTest_EntityScriptOrdering_Record Get_Record(FCk_Handle InRecorder)
    {
        if (ck::Is_NOT_Valid(InRecorder) || InRecorder.Has_Fragment(FCk_Fragment_AutoTest_EntityScriptOrdering_Record) == false)
        { return FCk_Fragment_AutoTest_EntityScriptOrdering_Record(); }

        return InRecorder.Get_Fragment(FCk_Fragment_AutoTest_EntityScriptOrdering_Record);
    }

    FString Format(const TArray<int32>& InOrder)
    {
        FString Result = "";
        for (int32 Index = 0; Index < InOrder.Num(); ++Index)
        {
            if (Index > 0)
            { Result += ","; }
            Result += f"{InOrder[Index]}";
        }
        return Result;
    }

    // 0,1,...,InCount-1 as text, for the asserts.
    FString Format_Ascending(int32 InCount)
    {
        auto Expected = TArray<int32>();
        for (int32 Index = 0; Index < InCount; ++Index)
        { Expected.Add(Index); }
        return Format(Expected);
    }

    // The three steps' records on one line, for logs and assert messages.
    FString Format_Steps(FCk_Handle InRecorder)
    {
        auto Record = Get_Record(InRecorder);
        return f"Construct=[{Format(Record.Constructed)}] OnConstructed=[{Format(Record.Finished)}] BeginPlay=[{Format(Record.Begun)}]";
    }

    FString Format_Events(FCk_Handle InRecorder)
    {
        auto Record = Get_Record(InRecorder);
        FString Result = "";
        for (int32 Index = 0; Index < Record.Events.Num(); ++Index)
        {
            if (Index > 0)
            { Result += " "; }
            Result += Record.Events[Index];
        }
        return Result;
    }

    // Position of InEvent in the recorder's event list, -1 when absent.
    int32 Get_EventPosition(FCk_Handle InRecorder, const FString& InEvent)
    {
        auto Record = Get_Record(InRecorder);
        return Record.Events.FindIndex(InEvent);
    }

    void On_Construct(FCk_Handle InScriptEntity, FCk_Handle InRecorder, int32 InIndex)
    {
        auto ScriptEntity = InScriptEntity;
        auto& Entry = ScriptEntity.AddOrGet_Fragment(FCk_Fragment_AutoTest_EntityScriptOrdering_Entry);
        Entry.Index = InIndex;
        Entry.Recorder = InRecorder;

        if (ck::Is_NOT_Valid(InRecorder))
        { return; }

        auto Recorder = InRecorder;
        auto& Record = Recorder.AddOrGet_Fragment(FCk_Fragment_AutoTest_EntityScriptOrdering_Record);
        Record.Constructed.Add(InIndex);
        Record.Events.Add(f"c{InIndex}");

        auto Group = FGameplayTag();
        int64 Sequence = 0;
        if (UCk_Utils_AutoTest_EntityScriptOrdering_UE::TryGet_Ordering(InScriptEntity, Group, Sequence))
        {
            Record.Sequenced.Add(InIndex);
            Record.StampGroups.Add(Group);
            Record.StampSequences.Add(Sequence);
        }
    }

    // The ordering stamp the fixture with InIndex carried at its DoConstruct; false when it carried none.
    bool TryGet_Stamp(FCk_Handle InRecorder, int32 InIndex, FGameplayTag&out OutGroup, int64&out OutSequence)
    {
        auto Record = Get_Record(InRecorder);
        auto Position = Record.Sequenced.FindIndex(InIndex);
        if (Position < 0)
        { return false; }

        OutGroup = Record.StampGroups[Position];
        OutSequence = Record.StampSequences[Position];
        return true;
    }

    void On_Constructed(FCk_Handle_EntityScript InScriptEntity)
    {
        auto TypedEntity = InScriptEntity;
        auto ScriptEntity = TypedEntity.H();
        if (ck::Is_NOT_Valid(ScriptEntity) || ScriptEntity.Has_Fragment(FCk_Fragment_AutoTest_EntityScriptOrdering_Entry) == false)
        { return; }

        auto Entry = ScriptEntity.Get_Fragment(FCk_Fragment_AutoTest_EntityScriptOrdering_Entry);
        if (ck::Is_NOT_Valid(Entry.Recorder))
        { return; }

        auto Recorder = Entry.Recorder;
        auto& Record = Recorder.AddOrGet_Fragment(FCk_Fragment_AutoTest_EntityScriptOrdering_Record);
        Record.Finished.Add(Entry.Index);
        Record.Events.Add(f"f{Entry.Index}");
    }

    void On_BeginPlay(FCk_Handle InRecorder, int32 InIndex, FCk_Handle InDestroyOnBeginPlay)
    {
        if (ck::IsValid(InRecorder))
        {
            auto Recorder = InRecorder;
            auto& Record = Recorder.AddOrGet_Fragment(FCk_Fragment_AutoTest_EntityScriptOrdering_Record);
            Record.Begun.Add(InIndex);
            Record.BegunFrames.Add(utils_stats::Get_FrameCount());
            Record.Events.Add(f"b{InIndex}");
        }

        if (ck::IsValid(InDestroyOnBeginPlay))
        {
            auto ToDestroy = InDestroyOnBeginPlay;
            utils_entity_lifetime::Request_DestroyEntity(ToDestroy);
        }
    }

    void On_EndPlay(FCk_Handle InRecorder, int32 InIndex)
    {
        if (ck::Is_NOT_Valid(InRecorder))
        { return; }

        auto Recorder = InRecorder;
        auto& Record = Recorder.AddOrGet_Fragment(FCk_Fragment_AutoTest_EntityScriptOrdering_Record);
        Record.Ended.Add(InIndex);
        Record.Events.Add(f"e{InIndex}");
    }
}

//----------------------------------------------------------------------------
// The fixture scripts.
//----------------------------------------------------------------------------

class UCk_AutoTest_EntityScriptOrdering_Unordered : UCk_GenericEntityScript_UE
{
    UPROPERTY(ExposeOnSpawn)
    int32 Index = 0;

    UPROPERTY(ExposeOnSpawn)
    FCk_Handle Recorder;

    UPROPERTY(ExposeOnSpawn)
    FCk_Handle DestroyOnBeginPlay;

    UPROPERTY(ExposeOnSpawn)
    int32 ChildIndex = -1;

    default _Replication = ECk_Replication::DoesNotReplicate;

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    {
        utils_autotest_entity_script_ordering::On_Construct(InHandle, Recorder, Index);

        if (ChildIndex >= 0)
        {
            utils_autotest_entity_script_ordering::Request_Spawn(InHandle, UCk_AutoTest_EntityScriptOrdering_Ordered,
                FCk_AutoTest_EntityScriptOrdering_Params(ChildIndex, Recorder), this);
        }

        return ECk_EntityScript_ConstructionFlow::Finished;
    }

    UFUNCTION()
    void OnOrderingEntryConstructed(FCk_Handle_EntityScript InHandle)
    {
        utils_autotest_entity_script_ordering::On_Constructed(InHandle);
    }

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        utils_autotest_entity_script_ordering::On_BeginPlay(Recorder, Index, DestroyOnBeginPlay);
    }

    UFUNCTION(BlueprintOverride)
    void DoEndPlay(FCk_Handle InHandle)
    {
        utils_autotest_entity_script_ordering::On_EndPlay(Recorder, Index);
    }
}

class UCk_AutoTest_EntityScriptOrdering_Ordered : UCk_AutoTest_EntityScriptOrdering_Unordered
{
    default _OrderingPolicy = ECk_EntityScript_OrderingPolicy::DefaultGroup;
}

class UCk_AutoTest_EntityScriptOrdering_OrderedOther : UCk_AutoTest_EntityScriptOrdering_Unordered
{
    default _OrderingPolicy = ECk_EntityScript_OrderingPolicy::DefaultGroup;
}

class UCk_AutoTest_EntityScriptOrdering_GroupX : UCk_AutoTest_EntityScriptOrdering_Unordered
{
    default _OrderingPolicy = ECk_EntityScript_OrderingPolicy::CustomGroup;
    default _OrderingGroup = utils_gameplay_tag::ResolveGameplayTag(n"AutoTest.EntityScript.Ordering.X");
}

class UCk_AutoTest_EntityScriptOrdering_GroupY : UCk_AutoTest_EntityScriptOrdering_Unordered
{
    default _OrderingPolicy = ECk_EntityScript_OrderingPolicy::CustomGroup;
    default _OrderingGroup = utils_gameplay_tag::ResolveGameplayTag(n"AutoTest.EntityScript.Ordering.Y");
}

// Invalid on purpose: CustomGroup with no _OrderingGroup.
class UCk_AutoTest_EntityScriptOrdering_GroupMissing : UCk_AutoTest_EntityScriptOrdering_Unordered
{
    default _OrderingPolicy = ECk_EntityScript_OrderingPolicy::CustomGroup;
}

// Unordered, records nothing; its DoConstruct spawns an Unordered child that records nothing either.
class UCk_AutoTest_EntityScriptOrdering_ChurnParent : UCk_GenericEntityScript_UE
{
    default _Replication = ECk_Replication::DoesNotReplicate;

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    {
        auto Self = InHandle;
        utils_entity_script::Request_SpawnEntity(Self, UCk_AutoTest_EntityScriptOrdering_Unordered,
            FInstancedStruct::Make(FCk_AutoTest_EntityScriptOrdering_Params()));
        return ECk_EntityScript_ConstructionFlow::Finished;
    }
}
