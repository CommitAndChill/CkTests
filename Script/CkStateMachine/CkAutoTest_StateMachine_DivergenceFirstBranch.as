// Language=angelscript

//============================================================================
// CK STATE MACHINE - AUTOMATION TEST: DIVERGENCE FIRST-BRANCH REGRESSION
//============================================================================
//
// Headless port of the DivergenceFirstBranch gym station. Spawns the
// existing gym driver actor (which sets up the SM, runs Pass A and Pass B,
// and snapshots per-pass counters), waits for the second pass to complete,
// and asserts on the snapshotted counter values.
//
// What this catches:
//   In a sub-SM, a state with multiple outgoing transitions whose first-
//   added branch happens to be the chosen one constructs the chosen
//   target-state task chain TWICE. Side-effecting tasks fire twice on a
//   single traversal. The bug tracks AddTransition order - swap the order
//   and the doubling follows the new first-added branch.
//
// Pass criteria:
//   Pass A (AddLeftFirst + ChooseLeft):
//     Each task fires exactly once per traversal:
//       Enter=1, Idle=1, Branch=1, Left=1, Right=0, Finish=1
//     Bug signal: Left=2 (first-added branch counter doubled).
//
//   Pass B (AddRightFirst + ChooseRight):
//     Each task fires exactly once:
//       Enter=1, Idle=1, Branch=1, Left=0, Right=1, Finish=1
//     Bug signal: Right=2.
//
// Wait for the driver to commit its actual second-pass snapshot. The cube
// and text are presentation; the assertions read the twelve source fields.
//============================================================================

class UCk_AutoTest_StateMachine_DivergenceFirstBranch : UCk_AutoTest_Base
{
    // Two post-construction 1.5s passes plus two deferred construction hops.
    // The actual completion marker is the gate; this is only a failure bound.
    default _TimeoutSeconds = 7.0f;

    private ACk_SmTest_DivergenceFirstBranch_GymActor _GymActor;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        // Spawn deferred so we can null out the StationHandle (no
        // BP_DemoDisplay panel exists in this test map) before BeginPlay
        // wires it up. The gym actor's display path no-ops on an invalid
        // station handle.
        auto Deferred = true;
        _GymActor = Cast<ACk_SmTest_DivergenceFirstBranch_GymActor>(
            SpawnActor(
                ACk_SmTest_DivergenceFirstBranch_GymActor,
                FVector(0.0f, 0.0f, 0.0f),
                FRotator::ZeroRotator,
                NAME_None,
                Deferred));

        if (!ck::IsValid(_GymActor))
        {
            FinishFailure("SpawnActor returned invalid gym actor");
            return;
        }

        _GymActor.StationHandle = FCk_Handle();
        FinishSpawningActor(_GymActor);
        if (ck::Is_NOT_Valid(_GymActor) || _GymActor.IsActorBeingDestroyed())
        { FinishFailure("Gym actor was lost during FinishSpawningActor"); return; }
        Add_Step_WaitUntil("both source passes committed their snapshots",
            n"Check_SourceFinished", 900);
        Add_Step("assert the exact A and B task counts", n"Step_AssertSnapshots");
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void Check_SourceFinished(FCk_Handle InHandle,
        FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Result = OutResult;
        if (ck::Is_NOT_Valid(_GymActor) || _GymActor.IsActorBeingDestroyed())
        {
            CloseOwnActor();
            FinishFailure("Gym actor was destroyed before source completion");
            return;
        }
        if (_GymActor.Get_SourceClosed())
        {
            CloseOwnActor();
            FinishFailure("Gym source closed before both snapshots committed");
            return;
        }
        Result.Set(_GymActor.Get_SourceRunFinished());
    }

    UFUNCTION()
    private void Step_AssertSnapshots(FCk_Handle InHandle,
        FInstancedStruct InPayload)
    {
        if (ck::Is_NOT_Valid(_GymActor) || _GymActor.IsActorBeingDestroyed() ||
            _GymActor.Get_SourceClosed() || !_GymActor.Get_SourceRunFinished())
        {
            CloseOwnActor();
            FinishFailure("completed source was lost before snapshot assertions");
            return;
        }

        // Pass A - AddLeftFirst + PaymentLeft -> Enter -> Idle -> Branch -> Left -> Finish.
        Assert_Equals_Int(_GymActor.Snap_A_Enter,  1, "Pass A: Enter task fires exactly once");
        Assert_Equals_Int(_GymActor.Snap_A_Idle,   1, "Pass A: Idle task fires exactly once");
        Assert_Equals_Int(_GymActor.Snap_A_Branch, 1, "Pass A: Branch task fires exactly once");
        Assert_Equals_Int(_GymActor.Snap_A_Left,   1, "Pass A: Left task fires exactly once (bug doubles to 2)");
        Assert_Equals_Int(_GymActor.Snap_A_Right,  0, "Pass A: Right task does NOT fire (not chosen)");
        Assert_Equals_Int(_GymActor.Snap_A_Finish, 1, "Pass A: Finish task fires exactly once");

        // Pass B - AddRightFirst + PaymentRight -> Enter -> Idle -> Branch -> Right -> Finish.
        Assert_Equals_Int(_GymActor.Snap_B_Enter,  1, "Pass B: Enter task fires exactly once");
        Assert_Equals_Int(_GymActor.Snap_B_Idle,   1, "Pass B: Idle task fires exactly once");
        Assert_Equals_Int(_GymActor.Snap_B_Branch, 1, "Pass B: Branch task fires exactly once");
        Assert_Equals_Int(_GymActor.Snap_B_Left,   0, "Pass B: Left task does NOT fire (not chosen)");
        Assert_Equals_Int(_GymActor.Snap_B_Right,  1, "Pass B: Right task fires exactly once (bug doubles to 2)");
        Assert_Equals_Int(_GymActor.Snap_B_Finish, 1, "Pass B: Finish task fires exactly once");

        CloseOwnActor();
    }

    private void CloseOwnActor()
    {
        auto Actor = _GymActor;
        _GymActor = nullptr;
        if (ck::Is_NOT_Valid(Actor) || Actor.IsActorBeingDestroyed())
        { return; }
        Actor.Request_CloseSource();
        if (ck::IsValid(Actor) && !Actor.IsActorBeingDestroyed())
        { Actor.DestroyActor(); }
    }

    UFUNCTION(BlueprintOverride)
    void DoEndPlay(FCk_Handle InHandle)
    {
        CloseOwnActor();
    }
}
