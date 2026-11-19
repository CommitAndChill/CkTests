// Language=angelscript

//============================================================================
// CK CUE - AUTOMATION TEST: SHARED FIXTURES FOR ORDERED CUES
//============================================================================
//
// One cue class that opts in to entity-script ordering (default group). Its
// DoBeginPlay appends its Index to a record fragment on its owner, so in the
// net rows the subject on each machine holds that machine's record. A cue
// adds the record when it is missing, so a cue that arrives before the
// test's body runs on that machine is still recorded.
//============================================================================

namespace Ck
{
    asset Asset_AutoTest_Cue_Ordering_Tags of UCk_GameplayTags
    {
        GameplayTags.Add(n"AutoTest.Cue.Ordering.Ordered");
    }
}

struct FCk_Fragment_AutoTestCue_OrderingRecord
{
    UPROPERTY()
    TArray<int32> Begun;
}

// Only the index crosses the wire.
USTRUCT()
struct FCk_AutoTestCue_Ordering_NetSpawnParams
{
    UPROPERTY()
    int32 Index = 0;

    FCk_AutoTestCue_Ordering_NetSpawnParams() {}
    FCk_AutoTestCue_Ordering_NetSpawnParams(int32 InIndex) { Index = InIndex; }
}

namespace utils_autotest_cue_ordering
{
    FGameplayTag Get_CueName()
    {
        return utils_gameplay_tag::ResolveGameplayTag(n"AutoTest.Cue.Ordering.Ordered");
    }

    // Fires one reliable ordered cue at InOwner.
    void Fire_Net(FCk_Handle InOwner, int32 InIndex, ECk_Cue_MulticastPolicy InMulticastPolicy)
    {
        auto Owner = InOwner;
        utils_cue_generic::Request_ExecuteCue(
            Owner,
            Get_CueName(),
            FInstancedStruct::Make(FCk_AutoTestCue_Ordering_NetSpawnParams(InIndex)),
            ECk_Cue_ReliabilityPolicy::Reliable,
            InMulticastPolicy);
    }

    TArray<int32> Get_Begun(FCk_Handle InRecorder)
    {
        if (ck::Is_NOT_Valid(InRecorder) || InRecorder.Has_Fragment(FCk_Fragment_AutoTestCue_OrderingRecord) == false)
        { return TArray<int32>(); }

        return InRecorder.Get_Fragment(FCk_Fragment_AutoTestCue_OrderingRecord).Begun;
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
}

class UCk_AutoTestCue_Ordering_Ordered : UCk_GenericCue_EntityScript
{
    UPROPERTY(ExposeOnSpawn)
    int32 Index = 0;

    default _Replication = ECk_Replication::DoesNotReplicate;
    default _LifetimeBehavior = ECk_Cue_LifetimeBehavior::AfterOneFrame;
    default _ConcurrencyPolicy = ECk_Cue_ConcurrencyPolicy::AllowMultiple;
    default _OrderingPolicy = ECk_EntityScript_OrderingPolicy::DefaultGroup;

    UFUNCTION(BlueprintOverride)
    FGameplayTag Get_CueName() const
    {
        return utils_autotest_cue_ordering::Get_CueName();
    }

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    {
        return ECk_EntityScript_ConstructionFlow::Finished;
    }

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto Recorder = utils_entity_lifetime::Get_LifetimeOwner(InHandle);
        if (ck::Is_NOT_Valid(Recorder))
        { return; }

        auto& Record = Recorder.AddOrGet_Fragment(FCk_Fragment_AutoTestCue_OrderingRecord);
        Record.Begun.Add(Index);
    }
}
