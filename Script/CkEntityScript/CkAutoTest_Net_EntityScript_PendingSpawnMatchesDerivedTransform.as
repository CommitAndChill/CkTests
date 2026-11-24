// Language=angelscript

//============================================================================
// CK ENTITY SCRIPT - NET AUTOMATION TEST: a client's pending child spawn matches its replicated child
//============================================================================
//
// The server spawns a replicated parent under the subject with a rotated transform. On every machine the parent's
// DoConstruct spawns two replicated children whose transform param it derives from its own
// (CkEntityScript_TESTONLY_PendingSpawnMatch.as). On the client each spawn is a pending entity that the replicated
// child must be matched to, although the client's derived transform and the child's replicated one agree only to
// replication precision. Each machine asserts both child OnConstructed callbacks fired and each resolved, through
// Get_PendingHandle_ForConstructed, to the slot whose transform the child carries.
//
// Surface: Ck.EntityScript.Net.AS_EntityScript_PendingSpawnMatchesDerivedTransform
//============================================================================

class UCk_AutoTest_Net_EntityScript_PendingSpawnMatchesDerivedTransform : UCk_AutoTest_NetBase
{
    default _TimeoutSeconds = 25.0f;

    private const int32 kPollBudget = 100000;
    private const float kMaxSeconds = 8.0f;
    // LOAD-SENSITIVE: the server keeps the parent alive this long for the client to finish its checks; under heavy
    // load a client slower than this reds as "the parent was destroyed before both callbacks fired".
    private const float kServerHoldSeconds = 10.0f;

    private FCk_Handle_PendingEntityScript _PendingParent;
    private FCk_Handle _Parent;
    private float _ServerResolvedTime = -1.0f;
    private float _CallbackWaitStartTime = -1.0f;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto Subject = Get_SubjectEntity();
        if (ck::Is_NOT_Valid(Subject))
        { FinishFailure("subject not found - harness misconfigured?"); return; }

        if (utils_net::Get_HasAuthority(Subject) == false)
        {
            Start_CallbackWait();
            return;
        }

        const auto ParentTransform = FTransform(
            FRotator(8.5f, 131.7f, -3.25f), FVector(1234.5f, -876.25f, 37.75f), FVector(1.0f, 1.0f, 1.0f));

        _PendingParent = utils_entity_script::Request_SpawnEntity(
            Subject,
            UCk_TESTONLY_PendingSpawnMatch_Parent_EntityScript,
            UCk_TESTONLY_PendingSpawnMatch_Parent_EntityScript::Params(ParentTransform));
        utils_pending_entity_script::Promise_OnConstructed(
            _PendingParent, FCk_Delegate_EntityScript_Constructed(this, n"OnServerParentConstructed"));
    }

    UFUNCTION()
    private void OnServerParentConstructed(FCk_Handle_EntityScript InParent)
    {
        _PendingParent = FCk_Handle_PendingEntityScript();
        _Parent = FCk_Handle(InParent);
        Track_ForCleanup(_Parent);
        Start_CallbackWait();
    }

    // The predicate also gives up on its own after kMaxSeconds so the continuation can say how many callbacks fired.
    private void Start_CallbackWait()
    {
        _CallbackWaitStartTime = System::GetGameTimeInSeconds();
        WaitUntil(n"Check_BothCallbacksFired", n"OnBothCallbacksFired", kPollBudget, kMaxSeconds + 5.0f);
    }

    UFUNCTION()
    private void Check_BothCallbacksFired(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        auto Parent = Get_ParentScript();
        Res.Set((Parent != nullptr && Parent.CallbackCount >= constants_pending_spawn_match_test::k_ChildCount)
            || System::GetGameTimeInSeconds() - _CallbackWaitStartTime >= kMaxSeconds);
    }

    UFUNCTION()
    private void OnBothCallbacksFired(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }

        const FString Machine = utils_net::Get_HasAuthority(Get_SubjectEntity()) ? "server" : "client";
        auto Parent = Get_ParentScript();
        if (Parent == nullptr)
        { FinishFailure(f"{Machine}: the replicated parent never arrived, or was destroyed before both callbacks fired"); return; }

        if (Parent.CallbackCount != constants_pending_spawn_match_test::k_ChildCount)
        { FinishFailure(f"{Machine}: {Parent.CallbackCount} of {constants_pending_spawn_match_test::k_ChildCount} child OnConstructed callbacks fired - the replicated children never matched their pending spawns"); return; }

        if (Parent.Failures.IsEmpty() == false)
        { FinishFailure(f"{Machine}:{Parent.Failures}"); return; }

        if (Parent.ResolvedSlots.Contains(0) == false || Parent.ResolvedSlots.Contains(1) == false)
        { FinishFailure(f"{Machine}: the callbacks resolved to slots {Parent.ResolvedSlots[0]} and {Parent.ResolvedSlots[1]}, expected 0 and 1"); return; }

        if (Machine == "client")
        { FinishSuccess(); return; }

        WaitUntil(n"Check_ServerHeld", n"OnServerHeld", kPollBudget, kServerHoldSeconds + 5.0f);
    }

    UFUNCTION()
    private void Check_ServerHeld(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        if (_ServerResolvedTime < 0.0f)
        { _ServerResolvedTime = System::GetGameTimeInSeconds(); }

        auto Res = OutResult;
        Res.Set(System::GetGameTimeInSeconds() - _ServerResolvedTime >= kServerHoldSeconds);
    }

    UFUNCTION()
    private void OnServerHeld(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }
        FinishSuccess();
    }

    private UCk_TESTONLY_PendingSpawnMatch_Parent_EntityScript Get_ParentScript()
    {
        auto Subject = Get_SubjectEntity();
        if (ck::Is_NOT_Valid(Subject))
        { return nullptr; }

        for (auto Candidate : utils_entity_tag::ForEach_Entity(Subject, constants_pending_spawn_match_test::k_ParentTag))
        {
            if (ck::IsValid(Candidate))
            { return Cast<UCk_TESTONLY_PendingSpawnMatch_Parent_EntityScript>(utils_entity_script::TryGet_EntityScript(Candidate)); }
        }

        return nullptr;
    }
}
