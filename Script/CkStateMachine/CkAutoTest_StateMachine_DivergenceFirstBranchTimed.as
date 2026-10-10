// Language=angelscript

//============================================================================
// CK STATE MACHINE - AUTOMATION TEST: DIVERGENCE FIRST-BRANCH (TIMED)
//============================================================================
//
// Headless port of the DivergenceFirstBranchTimed gym station - same shape
// as the non-timed variant but the divergence transitions are gated by
// short timer-based event conditions (~0.05s per hop) instead of vacuous
// transitions. The two together cover both fast-path (vacuous Pass on
// first evaluation) and timed-path (Pass after a delayed event) divergence
// handling.
//
// What this catches:
//   The same first-added-branch task-doubling bug as the non-timed test,
//   but exercised through the event-driven condition code path. A bug that
//   only surfaces in one of the two paths shows up as an asymmetric
//   pass/fail across this test and the non-timed variant.
//
// Pass criteria are identical to the non-timed test - every counter equals
// exactly 1 across both passes; the bug doubles whichever branch was
// first-added-and-chosen.
//
// Wait for the actor's actual B-snapshot completion marker; the wall-clock
// timeout bounds failure, rather than guessing when deferred construction ends.
//============================================================================

class UCk_AutoTest_StateMachine_DivergenceFirstBranchTimed : UCk_AutoTest_NetBase
{
    // Standalone keeps the original local-source path. Replicated mode waits
    // for a completed authority value frame and its real Iris actor identity.
    default _NetSubjectClass = ACk_AutoTest_NetSubject_Divergence_UE;
    default _TimeoutSeconds = 7.0f;

    private ACk_SmTest_DivergenceTimed_GymActor _GymActor;
    private ACk_AutoTest_NetSubject_Divergence_UE _SubjectActor;
    private UWorld _World;
    private bool _Standalone = false;
    private bool _Authority = false;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        if (ck::Is_NOT_Valid(InHandle))
        { FinishFailure("test entity is invalid"); return; }
        _World = utils_entity_lifetime::Get_WorldForEntity(InHandle);
        if (ck::Is_NOT_Valid(_World))
        { FinishFailure("test entity has no live world"); return; }
        auto Mode = _World.GetNetMode();
        _Standalone = Mode == ENetMode::NM_Standalone;
        _Authority = Mode == ENetMode::NM_DedicatedServer ||
            Mode == ENetMode::NM_ListenServer;
        if (!_Standalone && !_Authority && Mode != ENetMode::NM_Client)
        { FinishFailure("unsupported world net mode"); return; }

        if (!_Standalone)
        {
            auto Subject = Get_SubjectEntity();
            if (ck::Is_NOT_Valid(Subject) || !utils_owning_actor::Has(Subject))
            { FinishFailure("replicated subject bridge is unavailable"); return; }
            _SubjectActor = Cast<ACk_AutoTest_NetSubject_Divergence_UE>(
                utils_owning_actor::TryGet_EntityOwningActor(Subject));
            if (ck::Is_NOT_Valid(_SubjectActor) ||
                _SubjectActor.IsActorBeingDestroyed() ||
                _SubjectActor.GetWorld() != _World)
            { FinishFailure("exact divergence subject is unavailable"); return; }
        }

        if (_Standalone || _Authority)
        {
            // The standalone runner ignores NetMode metadata. Preserve its
            // real two-pass source; only network authority retains it on the
            // replicated subject before deferred BeginPlay can run.
            const bool Deferred = true;
            _GymActor = Cast<ACk_SmTest_DivergenceTimed_GymActor>(
                SpawnActor(ACk_SmTest_DivergenceTimed_GymActor, FVector(0.0f, 0.0f, 0.0f),
                    FRotator::ZeroRotator, NAME_None, Deferred));
            if (ck::Is_NOT_Valid(_GymActor))
            { FinishFailure("SpawnActor returned invalid gym actor"); return; }
            _GymActor.StationHandle = FCk_Handle();
            if (_Authority)
            {
                _GymActor.SetOwner(_SubjectActor);
                if (!_SubjectActor.TryRetain_SourceActor(_GymActor, true))
                {
                    if (ck::IsValid(_GymActor) && !_GymActor.IsActorBeingDestroyed())
                    { _GymActor.DestroyActor(); }
                    _GymActor = nullptr;
                    FinishFailure("authority subject rejected its source actor");
                    return;
                }
            }
            FinishSpawningActor(_GymActor);
            if (ck::Is_NOT_Valid(_GymActor) || _GymActor.IsActorBeingDestroyed())
            { FinishFailure("gym actor was lost during FinishSpawningActor"); return; }
        }

        Add_Step_WaitUntil("both timed source passes committed snapshots", n"Check_SourceFinished", 900);
        if (_Authority)
        { Add_Step("publish the real completed authority source", n"Step_PublishSource"); }
        Add_Step("assert the exact A and B task counts", n"Step_AssertSnapshots");
        Run_Steps(InHandle);
    }

    private bool Get_HasExactCompletedFrame(FCk_AutoTest_DivergenceFrame Frame)
    {
        if (ck::Is_NOT_Valid(_World) || ck::Is_NOT_Valid(_SubjectActor) ||
            _SubjectActor.IsActorBeingDestroyed() ||
            _SubjectActor.GetWorld() != _World ||
            !Frame.bCompleted || Frame.SchemaVersion != 1 ||
            Frame.SourceGeneration != 1 || Frame.Sequence != 1 ||
            Frame.bTimed != true || Frame.SourceNetIdentity <= 0)
        { return false; }
        auto Actor = Frame.SourceActor;
        if (ck::Is_NOT_Valid(Actor) || Actor.IsActorBeingDestroyed() ||
            Actor.GetWorld() != _World || Actor.GetOwner() != _SubjectActor ||
            Cast<ACk_SmTest_DivergenceTimed_GymActor>(Actor) == nullptr)
        { return false; }
        return ACk_AutoTest_NetSubject_Divergence_UE::Get_IrisActorIdentity(Actor) ==
            Frame.SourceNetIdentity;
    }

    UFUNCTION()
    private void Check_SourceFinished(FCk_Handle InHandle,
        FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        if (_Standalone || _Authority)
        {
            if (ck::Is_NOT_Valid(_GymActor) || _GymActor.IsActorBeingDestroyed() ||
                _GymActor.GetWorld() != _World || _GymActor.Get_SourceClosed())
            { FinishFailure("gym source closed before both snapshots committed"); return; }
            const bool Completed = _GymActor.Get_SourceRunFinished();
            Result.Set(Completed && (_Standalone ||
                ACk_AutoTest_NetSubject_Divergence_UE::Get_IrisActorIdentity(
                    _GymActor) > 0));
            return;
        }
        if (ck::Is_NOT_Valid(_SubjectActor) ||
            _SubjectActor.IsActorBeingDestroyed() || _SubjectActor.GetWorld() != _World)
        { FinishFailure("replicated subject was lost before completion"); return; }
        auto Frame = _SubjectActor.Get_Snapshot();
        if (Frame.bCompleted && (Frame.SchemaVersion != 1 ||
            Frame.SourceGeneration != 1 || Frame.Sequence != 1 ||
            Frame.bTimed != true || Frame.SourceNetIdentity <= 0))
        { FinishFailure("completed divergence frame has invalid metadata"); return; }
        if (Frame.bCompleted && ck::IsValid(Frame.SourceActor) &&
            !Frame.SourceActor.IsActorBeingDestroyed())
        {
            auto Actor = Frame.SourceActor;
            auto IrisId = ACk_AutoTest_NetSubject_Divergence_UE::Get_IrisActorIdentity(Actor);
            if (IrisId > 0 && (Actor.GetWorld() != _World ||
                Actor.GetOwner() != _SubjectActor ||
                Cast<ACk_SmTest_DivergenceTimed_GymActor>(Actor) == nullptr ||
                IrisId != Frame.SourceNetIdentity))
            { FinishFailure("completed divergence source has wrong family or Iris identity"); return; }
        }
        // Its actor reference and Iris bridge can settle after the value
        // property. A completed prefix is never accepted as ready.
        Result.Set(Get_HasExactCompletedFrame(Frame));
    }

    UFUNCTION()
    private void Step_PublishSource(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        if (!_Authority || ck::Is_NOT_Valid(_SubjectActor) ||
            _SubjectActor.IsActorBeingDestroyed() ||
            ck::Is_NOT_Valid(_GymActor) || _GymActor.IsActorBeingDestroyed() ||
            !_GymActor.Get_SourceRunFinished() ||
            ACk_AutoTest_NetSubject_Divergence_UE::Get_IrisActorIdentity(_GymActor) <= 0 ||
            !_SubjectActor.TryPublish_CompletedSource())
        { FinishFailure("authority could not publish its completed source"); }
    }

    UFUNCTION()
    private void Step_AssertSnapshots(FCk_Handle InHandle,
        FInstancedStruct InPayload)
    {
        if (_Standalone)
        {
            if (ck::Is_NOT_Valid(_GymActor) || _GymActor.IsActorBeingDestroyed() ||
                _GymActor.Get_SourceClosed() || !_GymActor.Get_SourceRunFinished())
            { CloseOwnActor(); FinishFailure("standalone source was lost before assertions"); return; }
            // Pass A - AddLeftFirst + PaymentLeft -> Enter -> Idle -> Branch -> Left -> Finish.
            Assert_Equals_Int(_GymActor.Snap_A_Enter,  1, "Pass A (timed): Enter task fires exactly once");
            Assert_Equals_Int(_GymActor.Snap_A_Idle,   1, "Pass A (timed): Idle task fires exactly once");
            Assert_Equals_Int(_GymActor.Snap_A_Branch, 1, "Pass A (timed): Branch task fires exactly once");
            Assert_Equals_Int(_GymActor.Snap_A_Left,   1, "Pass A (timed): Left task fires exactly once (bug doubles to 2)");
            Assert_Equals_Int(_GymActor.Snap_A_Right,  0, "Pass A (timed): Right task does NOT fire (not chosen)");
            Assert_Equals_Int(_GymActor.Snap_A_Finish, 1, "Pass A (timed): Finish task fires exactly once");

            // Pass B - AddRightFirst + PaymentRight -> Enter -> Idle -> Branch -> Right -> Finish.
            Assert_Equals_Int(_GymActor.Snap_B_Enter,  1, "Pass B (timed): Enter task fires exactly once");
            Assert_Equals_Int(_GymActor.Snap_B_Idle,   1, "Pass B (timed): Idle task fires exactly once");
            Assert_Equals_Int(_GymActor.Snap_B_Branch, 1, "Pass B (timed): Branch task fires exactly once");
            Assert_Equals_Int(_GymActor.Snap_B_Left,   0, "Pass B (timed): Left task does NOT fire (not chosen)");
            Assert_Equals_Int(_GymActor.Snap_B_Right,  1, "Pass B (timed): Right task fires exactly once (bug doubles to 2)");
            Assert_Equals_Int(_GymActor.Snap_B_Finish, 1, "Pass B (timed): Finish task fires exactly once");
            CloseOwnActor();
            return;
        }
        if (ck::Is_NOT_Valid(_SubjectActor) ||
            _SubjectActor.IsActorBeingDestroyed())
        { FinishFailure("replicated subject was lost before assertions"); return; }
        auto Frame = _SubjectActor.Get_Snapshot();
        if (!Get_HasExactCompletedFrame(Frame))
        { FinishFailure("completed source frame was lost before assertions"); return; }
        if (!_Authority)
        {
            auto LocalSource = Cast<ACk_SmTest_DivergenceTimed_GymActor>(Frame.SourceActor);
            if (LocalSource == nullptr || LocalSource.Get_SourceRoots().Num() != 0 ||
                LocalSource.Get_SourceConstructedCount() != 0 ||
                LocalSource.Snap_A_Enter != 0 || LocalSource.Snap_A_Idle != 0 ||
                LocalSource.Snap_A_Branch != 0 || LocalSource.Snap_A_Left != 0 ||
                LocalSource.Snap_A_Right != 0 || LocalSource.Snap_A_Finish != 0 ||
                LocalSource.Snap_B_Enter != 0 || LocalSource.Snap_B_Idle != 0 ||
                LocalSource.Snap_B_Branch != 0 || LocalSource.Snap_B_Left != 0 ||
                LocalSource.Snap_B_Right != 0 || LocalSource.Snap_B_Finish != 0)
            { FinishFailure("client ran a local divergence source instead of reading its frame"); return; }
        }

        // Pass A - AddLeftFirst + PaymentLeft -> Enter -> Idle -> Branch -> Left -> Finish.
        Assert_Equals_Int(Frame.Snap_A_Enter,  1, "Pass A (timed): Enter task fires exactly once");
        Assert_Equals_Int(Frame.Snap_A_Idle,   1, "Pass A (timed): Idle task fires exactly once");
        Assert_Equals_Int(Frame.Snap_A_Branch, 1, "Pass A (timed): Branch task fires exactly once");
        Assert_Equals_Int(Frame.Snap_A_Left,   1, "Pass A (timed): Left task fires exactly once (bug doubles to 2)");
        Assert_Equals_Int(Frame.Snap_A_Right,  0, "Pass A (timed): Right task does NOT fire (not chosen)");
        Assert_Equals_Int(Frame.Snap_A_Finish, 1, "Pass A (timed): Finish task fires exactly once");

        // Pass B - AddRightFirst + PaymentRight -> Enter -> Idle -> Branch -> Right -> Finish.
        Assert_Equals_Int(Frame.Snap_B_Enter,  1, "Pass B (timed): Enter task fires exactly once");
        Assert_Equals_Int(Frame.Snap_B_Idle,   1, "Pass B (timed): Idle task fires exactly once");
        Assert_Equals_Int(Frame.Snap_B_Branch, 1, "Pass B (timed): Branch task fires exactly once");
        Assert_Equals_Int(Frame.Snap_B_Left,   0, "Pass B (timed): Left task does NOT fire (not chosen)");
        Assert_Equals_Int(Frame.Snap_B_Right,  1, "Pass B (timed): Right task fires exactly once (bug doubles to 2)");
        Assert_Equals_Int(Frame.Snap_B_Finish, 1, "Pass B (timed): Finish task fires exactly once");
    }

    private void CloseOwnActor()
    {
        // In network worlds the exact subject owns the retained source and
        // destroys it during its own EndPlay, after client frame delivery.
        if (!_Standalone) { return; }
        auto Actor = _GymActor;
        _GymActor = nullptr;
        if (ck::Is_NOT_Valid(Actor) || Actor.IsActorBeingDestroyed()) { return; }
        Actor.Request_CloseSource();
        if (ck::IsValid(Actor) && !Actor.IsActorBeingDestroyed())
        { Actor.DestroyActor(); }
    }

    UFUNCTION(BlueprintOverride)
    void DoEndPlay(FCk_Handle InHandle)
    { CloseOwnActor(); }
}
