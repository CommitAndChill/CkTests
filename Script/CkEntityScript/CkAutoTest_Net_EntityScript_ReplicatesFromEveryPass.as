// Language=angelscript

//============================================================================
// CK ENTITY SCRIPT - NET AUTOMATION TEST: a Replicates script replicates whichever pass executed its spawn
//============================================================================
//
// The server spawns Replicates entity scripts under the subject from each of the three points of a frame where the
// spawn processor executes a request (main pass, the script group's settle barrier, the end-of-frame tail pump; forced
// by the processors in CkEntityScript_TESTONLY_ReplicationPasses.as), in both construction flows:
// - Finished-flow, spawned in each pass;
// - Continue-flow, spawned in each pass and finished in the tail pump of a later frame (the finish tag lands after the
//   replicate request was already seen), plus one spawned and finished in the main pass;
// - a Finished-flow parent spawned in the main pass that requests a Replicates child in its DoConstruct (the child's
//   spawn executes in the settle barrier);
// - once all of those settled, one more Continue-flow script alone: spawned in the main pass, left for a few quiet
//   frames, then finished in the tail pump. With nothing else spawning or replicating, no other entity's replicate
//   request moves the replicate processor's dirty marker in the frame of the finish, so only the finish itself can
//   wake it.
// The server asserts every entity reports replication complete and that no ensure fired; the client asserts every
// payload arrives. One line per entity is logged on each machine ([EntityScriptReplicationTest-...]).
//
// Surface: Ck.EntityScript.Net.AS_EntityScript_ReplicatesFromEveryPass
//============================================================================

class UCk_AutoTest_Net_EntityScript_ReplicatesFromEveryPass : UCk_AutoTest_NetBase
{
    default _TimeoutSeconds = 25.0f;

    // How long each wait may take, in frames counted from the wait's start; each predicate ends itself on its budget so
    // the asserts can say which entity is missing.
    private const int64 k_HostBuildFrames = 240;
    private const int64 k_HostCompleteFrames = 240;
    private const int32 k_QuietFrames = 10;
    private const int64 k_ClientWatchFrames = 1200;
    // A poll budget above any frame budget: the harness polls twice a frame.
    private const int32 k_PollBudget = 5000;
    private const float32 k_ServerHoldSeconds = 2.0f;

    private TArray<FCk_TESTONLY_EntityScriptReplication_Leg> _Legs;
    private FCk_TESTONLY_EntityScriptReplication_Leg _AloneLeg;
    // Every entity the test expects (each leg's root, the child of the parent leg, then the alone leg), by name and payload.
    private TArray<FString> _ExpectedNames;
    private TArray<int32> _ExpectedPayloads;
    private TArray<bool> _ClientSeen;

    private int32 _EnsuresBefore = 0;
    private int64 _WaitStartFrame = 0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto Subject = Get_SubjectEntity();
        if (ck::Is_NOT_Valid(Subject))
        { FinishFailure("subject entity not found"); return; }

        Build_Legs();
        _WaitStartFrame = utils_stats::Get_FrameCount();

        if (utils_net::Get_HasAuthority(Subject) == false)
        {
            WaitUntil(n"Is_ClientWatched", n"OnClientWatched", k_PollBudget);
            return;
        }

        _EnsuresBefore = utils_ensure::Get_EnsureCount();
        for (const auto& Leg : _Legs)
        { utils_entity_script_replication_test::Add_SpawnOrder(Subject, Leg.SpawnPass, Leg.Order); }

        WaitUntil(n"Is_HostBuilt", n"OnHostBuilt", k_PollBudget);
    }

    private void Build_Legs()
    {
        auto Finished = ECk_TESTONLY_EntityScriptReplication_Kind::Finished;
        auto Continue = ECk_TESTONLY_EntityScriptReplication_Kind::Continue;
        auto Main = ECk_TESTONLY_EntityScriptReplication_Pass::Main;
        auto Settle = ECk_TESTONLY_EntityScriptReplication_Pass::Settle;
        auto Tail = ECk_TESTONLY_EntityScriptReplication_Pass::Tail;

        _Legs.Add(FCk_TESTONLY_EntityScriptReplication_Leg("Finished spawned in the main pass", FCk_TESTONLY_EntityScriptReplication_Order(Finished, 7101), Main));
        _Legs.Add(FCk_TESTONLY_EntityScriptReplication_Leg("Finished spawned in the settle", FCk_TESTONLY_EntityScriptReplication_Order(Finished, 7102), Settle));
        _Legs.Add(FCk_TESTONLY_EntityScriptReplication_Leg("Finished spawned in the tail pump", FCk_TESTONLY_EntityScriptReplication_Order(Finished, 7103), Tail));
        _Legs.Add(FCk_TESTONLY_EntityScriptReplication_Leg("Continue spawned in the main pass, finished in a later tail pump", FCk_TESTONLY_EntityScriptReplication_Order(Continue, 7104), Main));
        _Legs.Add(FCk_TESTONLY_EntityScriptReplication_Leg("Continue spawned in the settle, finished in a later tail pump", FCk_TESTONLY_EntityScriptReplication_Order(Continue, 7105), Settle));
        _Legs.Add(FCk_TESTONLY_EntityScriptReplication_Leg("Continue spawned in the tail pump, finished in a later tail pump", FCk_TESTONLY_EntityScriptReplication_Order(Continue, 7106), Tail));

        auto MainFinished = FCk_TESTONLY_EntityScriptReplication_Leg("Continue spawned in the main pass, finished in a later main pass", FCk_TESTONLY_EntityScriptReplication_Order(Continue, 7107), Main);
        MainFinished.FinishPass = Main;
        _Legs.Add(MainFinished);

        _Legs.Add(FCk_TESTONLY_EntityScriptReplication_Leg("Finished parent spawned in the main pass", FCk_TESTONLY_EntityScriptReplication_Order(ECk_TESTONLY_EntityScriptReplication_Kind::FinishedWithChild, 7108), Main));

        _AloneLeg = FCk_TESTONLY_EntityScriptReplication_Leg("Continue spawned alone in the main pass, finished in a quiet later tail pump", FCk_TESTONLY_EntityScriptReplication_Order(Continue, 7109), Main);

        for (const auto& Leg : _Legs)
        {
            _ExpectedNames.Add(Leg.Name);
            _ExpectedPayloads.Add(Leg.Order.Payload);

            if (Leg.Order.Kind == ECk_TESTONLY_EntityScriptReplication_Kind::FinishedWithChild)
            {
                _ExpectedNames.Add(Leg.Name + ": its DoConstruct child");
                _ExpectedPayloads.Add(utils_entity_script_replication_test::Get_ChildPayload(Leg.Order.Payload));
            }
        }

        _ExpectedNames.Add(_AloneLeg.Name);
        _ExpectedPayloads.Add(_AloneLeg.Order.Payload);

        for (int32 Index = 0; Index < _ExpectedPayloads.Num(); ++Index)
        { _ClientSeen.Add(false); }
    }

    private bool Get_IsPastFrames(int64 InFrames)
    {
        return utils_stats::Get_FrameCount() - _WaitStartFrame >= InFrames;
    }

    private void Start_FrameWait(FName InPredicateName, FName InContinuationName)
    {
        _WaitStartFrame = utils_stats::Get_FrameCount();
        WaitUntil(InPredicateName, InContinuationName, k_PollBudget);
    }

    //------------------------------------------------------------------------
    // Server
    //------------------------------------------------------------------------

    // Every leg's root exists on the host (or the frame budget ran out: the asserts then say which).
    UFUNCTION()
    private void Is_HostBuilt(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Subject = Get_SubjectEntity();
        auto AllBuilt = true;
        for (const auto& Leg : _Legs)
        {
            if (ck::Is_NOT_Valid(utils_entity_script_replication_test::Find(Subject, Leg.Order.Payload)))
            { AllBuilt = false; }
        }

        auto Result = OutResult;
        Result.Set(AllBuilt || Get_IsPastFrames(k_HostBuildFrames));
    }

    UFUNCTION()
    private void OnHostBuilt(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        auto Subject = Get_SubjectEntity();
        for (const auto& Leg : _Legs)
        {
            auto Entity = utils_entity_script_replication_test::Find(Subject, Leg.Order.Payload);
            if (ck::Is_NOT_Valid(Entity))
            { continue; }

            Track_ForCleanup(Entity);

            if (Leg.Order.Kind == ECk_TESTONLY_EntityScriptReplication_Kind::Continue)
            { utils_entity_script_replication_test::Add_FinishOrder(Entity, Leg.FinishPass); }
        }

        Start_FrameWait(n"Is_HostSettled", n"OnHostSettled");
    }

    // Every expected entity but the alone leg reports replication complete on the host, or the frame budget ran out.
    UFUNCTION()
    private void Is_HostSettled(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Subject = Get_SubjectEntity();
        auto AllComplete = true;
        for (const auto Payload : _ExpectedPayloads)
        {
            if (Payload == _AloneLeg.Order.Payload)
            { continue; }

            if (utils_entity_script_replication_test::Get_IsReplicationComplete(utils_entity_script_replication_test::Find(Subject, Payload)) == false)
            { AllComplete = false; }
        }

        auto Result = OutResult;
        Result.Set(AllComplete || Get_IsPastFrames(k_HostCompleteFrames));
    }

    UFUNCTION()
    private void OnHostSettled(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        auto Subject = Get_SubjectEntity();
        utils_entity_script_replication_test::Add_SpawnOrder(Subject, _AloneLeg.SpawnPass, _AloneLeg.Order);
        Start_FrameWait(n"Is_AloneBuilt", n"OnAloneBuilt");
    }

    UFUNCTION()
    private void Is_AloneBuilt(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        const auto Built = ck::IsValid(utils_entity_script_replication_test::Find(Get_SubjectEntity(), _AloneLeg.Order.Payload));
        auto Result = OutResult;
        Result.Set(Built || Get_IsPastFrames(k_HostBuildFrames));
    }

    // The pumps that saw its replicate request (with no finish tag yet) have run; nothing else is spawning.
    UFUNCTION()
    private void OnAloneBuilt(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        WaitFrames(k_QuietFrames, n"OnAloneQuiet");
    }

    UFUNCTION()
    private void OnAloneQuiet(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        auto Entity = utils_entity_script_replication_test::Find(Get_SubjectEntity(), _AloneLeg.Order.Payload);
        if (ck::IsValid(Entity))
        {
            Track_ForCleanup(Entity);
            utils_entity_script_replication_test::Add_FinishOrder(Entity, _AloneLeg.FinishPass);
        }

        Start_FrameWait(n"Is_AloneSettled", n"OnAloneSettled");
    }

    UFUNCTION()
    private void Is_AloneSettled(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        const auto Entity = utils_entity_script_replication_test::Find(Get_SubjectEntity(), _AloneLeg.Order.Payload);
        auto Result = OutResult;
        Result.Set(utils_entity_script_replication_test::Get_IsReplicationComplete(Entity) || Get_IsPastFrames(k_HostCompleteFrames));
    }

    UFUNCTION()
    private void OnAloneSettled(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        auto Subject = Get_SubjectEntity();
        FString NotComplete = "";
        for (int32 Index = 0; Index < _ExpectedPayloads.Num(); ++Index)
        {
            const auto Entity = utils_entity_script_replication_test::Find(Subject, _ExpectedPayloads[Index]);
            const auto State = utils_entity_script_replication_test::Get_HostState(Entity);
            Log(f"[EntityScriptReplicationTest-server] {_ExpectedNames[Index]} (payload {_ExpectedPayloads[Index]}): {State}");

            if (State != "complete")
            { NotComplete += f" [{_ExpectedNames[Index]}: {State}]"; }
        }

        const auto NewEnsures = utils_ensure::Get_EnsureCount() - _EnsuresBefore;
        Log(f"[EntityScriptReplicationTest-server] new ensures: {NewEnsures}");

        Assert_True(NotComplete == "", f"server: not replicated:{NotComplete}");
        Assert_Equals_Int(NewEnsures, 0, "server: no ensure fired while the spawns replicated");
        WaitSeconds(k_ServerHoldSeconds, n"OnServerHeld");
    }

    UFUNCTION()
    private void OnServerHeld(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        FinishSuccess();
    }

    //------------------------------------------------------------------------
    // Client
    //------------------------------------------------------------------------

    // Latches each payload the first time it is seen; ends when all arrived or the frame budget ran out.
    UFUNCTION()
    private void Is_ClientWatched(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Subject = Get_SubjectEntity();
        auto AllSeen = true;
        for (int32 Index = 0; Index < _ExpectedPayloads.Num(); ++Index)
        {
            if (_ClientSeen[Index] == false && ck::IsValid(utils_entity_script_replication_test::Find(Subject, _ExpectedPayloads[Index])))
            { _ClientSeen[Index] = true; }

            if (_ClientSeen[Index] == false)
            { AllSeen = false; }
        }

        auto Result = OutResult;
        Result.Set(AllSeen || Get_IsPastFrames(k_ClientWatchFrames));
    }

    UFUNCTION()
    private void OnClientWatched(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        FString Missing = "";
        for (int32 Index = 0; Index < _ExpectedPayloads.Num(); ++Index)
        {
            const auto Word = _ClientSeen[Index] ? "arrived" : "missing";
            Log(f"[EntityScriptReplicationTest-client] {_ExpectedNames[Index]} (payload {_ExpectedPayloads[Index]}): {Word}");

            if (_ClientSeen[Index] == false)
            { Missing += f" [{_ExpectedNames[Index]}]"; }
        }

        Assert_True(Missing == "", f"client: never arrived:{Missing}");
        FinishSuccess();
    }
}
