// ============================================================================
// SM GRAPH-WALK GHOST-TASK REGRESSION - TEST STATES
// ============================================================================
//
// Guards CkFoundation PR #643. The debug graph-walk processor
// (FProcessor_Sm_Debug_GraphWalk, guarded by CK_BUILD_SM_GRAPH_WALK
// defined for editor/development builds) constructs temp state / condition /
// task entities during deferred debugger capture to cache the reachable
// graph for the HFSM viewer. Those temp entities are stamped with
// FTag_Sm_Debug_GraphWalkEntity;
// UCk_Sm*_EntityScript::BeginPlay short-circuits on that tag so their
// Enter* bodies never run.
//
// Without the short-circuit, every reachable state's task DoEnterTask fires
// during capture. Side-effecting tasks (counter writes, Request_Stop on
// the owning SM - see UBb_Hfsm_Task_TerminateOwningSm) then corrupt real
// state before the SM has started.
//
// This file defines a 5-state linear chain A -> B -> C -> D -> E gated by a
// polled-false condition (the real SM never advances past A). Each state's
// task increments a label-keyed counter on DoEnterTask. If the short-circuit
// regresses, counters B..E become nonzero during capture and the gym's
// station reports FAIL. A terminal-state Request_Stop variant is used on E
// in the sub-SM build so a regression additionally stops the real sub-SM
// immediately - matching the original CheckoutCounter failure mode.
//
// In non-graph-walk builds (CK_BUILD_SM_GRAPH_WALK=0) this test trivially
// passes since no ghost entities are ever created. The regression guard
// only has teeth in editor/development.

// ============================================================================
// COUNTER REGISTRY (stateless helpers)
// ============================================================================
//
// AngelScript disallows mutable namespace globals, so counter state lives as
// a TMap<FName,int32> UPROPERTY on ACk_SmTest_GraphWalkRegression_GymActor.
// Task callbacks resolve their exact accepted top or sub source root.

namespace SmGraphWalk_Regression
{
    ACk_SmTest_GraphWalkRegression_GymActor TryGet_Source(
        FCk_Handle InCallback, bool InTop)
    {
        FCk_Handle Context;
        if (!UCk_StateMachineGym_Runtime_UE::TryGet_SourceContext(
                InCallback, Context) || !utils_owning_actor::Has(Context))
        { return nullptr; }
        auto Actor = Cast<ACk_SmTest_GraphWalkRegression_GymActor>(
            utils_owning_actor::TryGet_EntityOwningActor(Context));
        if (ck::Is_NOT_Valid(Actor) || Actor.IsActorBeingDestroyed() ||
            Actor.Get_SourceClosed())
        { return nullptr; }
        auto Roots = Actor.Get_SourceRoots();
        if (Roots.Num() < 1 || Roots.Num() > 2 ||
            (Roots.Num() == 2 && Roots[0] == Roots[1]) ||
            Actor.Get_SourceConstructedCount() < 1 ||
            Actor.Get_SourceConstructedCount() > Roots.Num() ||
            (InTop && (Context != Roots[0] || Actor.TopEntity != Context)) ||
            (!InTop && (Roots.Num() != 2 || Context != Roots[1] ||
                Actor.SubEntity != Context)) ||
            !Actor.Get_HasCurrentSourceContext())
        { return nullptr; }
        // A state/task callback may run before Add's returned SM handle is
        // assigned. An assigned handle must still identify this source root.
        auto Assigned = InTop ? Actor.TopSmHandle : Actor.SubSmParentHandle;
        if (ck::IsValid(Assigned) && FCk_Handle(Assigned) != Context)
        { Actor.Request_CloseSource(); return nullptr; }
        return Actor;
    }

    bool Increment(FCk_Handle_SmTask InHandle, FName InLabel, bool InTop)
    {
        auto Actor = TryGet_Source(FCk_Handle(InHandle), InTop);
        if (ck::Is_NOT_Valid(Actor)) { return false; }
        Actor.Increment_Counter(InLabel);
        return true;
    }

    bool HasExactStopTarget(FCk_Handle_SmTask InTask,
        FCk_Handle_StateMachine InOwningSm, bool InTop)
    {
        auto Actor = TryGet_Source(FCk_Handle(InTask), InTop);
        if (ck::Is_NOT_Valid(Actor)) { return false; }
        auto Sm = FCk_Handle(InOwningSm);
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Sm) ||
            ck::Is_NOT_Valid(InOwningSm) ||
            utils_entity_lifetime::Get_IsPendingDestroy(
                Sm, ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
            !utils_state_machine::Has(Sm))
        { return false; }
        FCk_Handle SmContext;
        if (!UCk_StateMachineGym_Runtime_UE::TryGet_SourceContext(
                Sm, SmContext) ||
            SmContext != (InTop ? Actor.TopEntity : Actor.SubEntity))
        { return false; }
        // Do not require this to be the outer SM: Sub E belongs to the
        // nested SM whose context is still the accepted SubEntity root.
        return ck::IsValid(TryGet_Source(FCk_Handle(InTask), InTop));
    }
}

// ============================================================================
// POLLED-FALSE CONDITION
// ============================================================================

// DoEvaluate returns false unconditionally - no real transition can ever fire.
// The real SM stays in its initial state forever; any counter increment
// beyond the initial state is proof of a graph-walk ghost regression.
UCLASS()
class UCk_SmTest_Condition_PolledFalse : UCk_SmCondition_Polled
{
    UFUNCTION(BlueprintOverride)
    bool DoEvaluate(FCk_Handle_SmCondition InHandle, FCk_Time InDeltaT) const
    {
        auto _CkPerfScope = ck::ScopedStat();
        return false;
    }
};

// ============================================================================
// COUNTER TASKS - TOP-LEVEL SM (EnterExitOnly)
// ============================================================================
//
// Distinct classes per state (not one parameterised class). The graph-walk
// processor instantiates a temp entity per task-in-state; reusing a single
// class across all five states would collapse attribution - any increment
// could have come from any ghost. Distinct classes preserve per-state
// attribution so a FAIL report pinpoints which ghost(s) ran.

UCLASS()
class UCk_SmTest_Task_EnterCount_Top_A : UCk_SmTask_EntityScript
{
    default _TaskMode = ECk_SmTaskMode::EnterExitOnly;

    UFUNCTION(BlueprintOverride)
    void DoEnterTask(FCk_Handle_SmTask InHandle, ECk_Sm_NetContext InNetContext)
    { SmGraphWalk_Regression::Increment(InHandle, n"TopA", true); }

    UFUNCTION(BlueprintOverride)
    void DoExitTask(FCk_Handle_SmTask InHandle, ECk_Sm_NetContext InNetContext) {}
};

UCLASS()
class UCk_SmTest_Task_EnterCount_Top_B : UCk_SmTask_EntityScript
{
    default _TaskMode = ECk_SmTaskMode::EnterExitOnly;

    UFUNCTION(BlueprintOverride)
    void DoEnterTask(FCk_Handle_SmTask InHandle, ECk_Sm_NetContext InNetContext)
    { SmGraphWalk_Regression::Increment(InHandle, n"TopB", true); }

    UFUNCTION(BlueprintOverride)
    void DoExitTask(FCk_Handle_SmTask InHandle, ECk_Sm_NetContext InNetContext) {}
};

UCLASS()
class UCk_SmTest_Task_EnterCount_Top_C : UCk_SmTask_EntityScript
{
    default _TaskMode = ECk_SmTaskMode::EnterExitOnly;

    UFUNCTION(BlueprintOverride)
    void DoEnterTask(FCk_Handle_SmTask InHandle, ECk_Sm_NetContext InNetContext)
    { SmGraphWalk_Regression::Increment(InHandle, n"TopC", true); }

    UFUNCTION(BlueprintOverride)
    void DoExitTask(FCk_Handle_SmTask InHandle, ECk_Sm_NetContext InNetContext) {}
};

UCLASS()
class UCk_SmTest_Task_EnterCount_Top_D : UCk_SmTask_EntityScript
{
    default _TaskMode = ECk_SmTaskMode::EnterExitOnly;

    UFUNCTION(BlueprintOverride)
    void DoEnterTask(FCk_Handle_SmTask InHandle, ECk_Sm_NetContext InNetContext)
    { SmGraphWalk_Regression::Increment(InHandle, n"TopD", true); }

    UFUNCTION(BlueprintOverride)
    void DoExitTask(FCk_Handle_SmTask InHandle, ECk_Sm_NetContext InNetContext) {}
};

// Terminal state task for the top-level SM - Request_Stop on the owning SM.
// This is the sharpest regression signal: a ghost entry during debugger
// capture requests a stop, which the later verification must detect.
UCLASS()
class UCk_SmTest_Task_RequestStopOwning_Top : UCk_SmTask_EntityScript
{
    default _TaskMode = ECk_SmTaskMode::EnterExitOnly;

    UFUNCTION(BlueprintOverride)
    void DoEnterTask(FCk_Handle_SmTask InHandle, ECk_Sm_NetContext InNetContext)
    {
        auto _CkPerfScope = ck::ScopedStat();
        if (!SmGraphWalk_Regression::Increment(InHandle, n"TopStopE", true))
        { return; }

        auto OwningSm = Get_OwningStateMachine();
        if (!SmGraphWalk_Regression::HasExactStopTarget(
                InHandle, OwningSm, true)) { return; }
        utils_state_machine::Request_Stop(OwningSm);

        Mark_Result(ECk_SmTaskResult::Succeeded);
    }

    UFUNCTION(BlueprintOverride)
    void DoExitTask(FCk_Handle_SmTask InHandle, ECk_Sm_NetContext InNetContext) {}
};

// ============================================================================
// COUNTER TASKS - SUB-SM
// ============================================================================

UCLASS()
class UCk_SmTest_Task_EnterCount_Sub_A : UCk_SmTask_EntityScript
{
    default _TaskMode = ECk_SmTaskMode::EnterExitOnly;

    UFUNCTION(BlueprintOverride)
    void DoEnterTask(FCk_Handle_SmTask InHandle, ECk_Sm_NetContext InNetContext)
    { SmGraphWalk_Regression::Increment(InHandle, n"SubA", false); }

    UFUNCTION(BlueprintOverride)
    void DoExitTask(FCk_Handle_SmTask InHandle, ECk_Sm_NetContext InNetContext) {}
};

UCLASS()
class UCk_SmTest_Task_EnterCount_Sub_B : UCk_SmTask_EntityScript
{
    default _TaskMode = ECk_SmTaskMode::EnterExitOnly;

    UFUNCTION(BlueprintOverride)
    void DoEnterTask(FCk_Handle_SmTask InHandle, ECk_Sm_NetContext InNetContext)
    { SmGraphWalk_Regression::Increment(InHandle, n"SubB", false); }

    UFUNCTION(BlueprintOverride)
    void DoExitTask(FCk_Handle_SmTask InHandle, ECk_Sm_NetContext InNetContext) {}
};

UCLASS()
class UCk_SmTest_Task_EnterCount_Sub_C : UCk_SmTask_EntityScript
{
    default _TaskMode = ECk_SmTaskMode::EnterExitOnly;

    UFUNCTION(BlueprintOverride)
    void DoEnterTask(FCk_Handle_SmTask InHandle, ECk_Sm_NetContext InNetContext)
    { SmGraphWalk_Regression::Increment(InHandle, n"SubC", false); }

    UFUNCTION(BlueprintOverride)
    void DoExitTask(FCk_Handle_SmTask InHandle, ECk_Sm_NetContext InNetContext) {}
};

UCLASS()
class UCk_SmTest_Task_EnterCount_Sub_D : UCk_SmTask_EntityScript
{
    default _TaskMode = ECk_SmTaskMode::EnterExitOnly;

    UFUNCTION(BlueprintOverride)
    void DoEnterTask(FCk_Handle_SmTask InHandle, ECk_Sm_NetContext InNetContext)
    { SmGraphWalk_Regression::Increment(InHandle, n"SubD", false); }

    UFUNCTION(BlueprintOverride)
    void DoExitTask(FCk_Handle_SmTask InHandle, ECk_Sm_NetContext InNetContext) {}
};

UCLASS()
class UCk_SmTest_Task_RequestStopOwning_Sub : UCk_SmTask_EntityScript
{
    default _TaskMode = ECk_SmTaskMode::EnterExitOnly;

    UFUNCTION(BlueprintOverride)
    void DoEnterTask(FCk_Handle_SmTask InHandle, ECk_Sm_NetContext InNetContext)
    {
        auto _CkPerfScope = ck::ScopedStat();
        if (!SmGraphWalk_Regression::Increment(InHandle, n"SubStopE", false))
        { return; }

        auto OwningSm = Get_OwningStateMachine();
        if (!SmGraphWalk_Regression::HasExactStopTarget(
                InHandle, OwningSm, false)) { return; }
        utils_state_machine::Request_Stop(OwningSm);

        Mark_Result(ECk_SmTaskResult::Succeeded);
    }

    UFUNCTION(BlueprintOverride)
    void DoExitTask(FCk_Handle_SmTask InHandle, ECk_Sm_NetContext InNetContext) {}
};

// ============================================================================
// TOP-LEVEL LINEAR CHAIN: A -> B -> C -> D -> E
// ============================================================================
//
// Every transition is gated by PolledFalse, so the real SM stays in A
// forever. Per-state tasks record entry in the counter registry. State E
// uses RequestStopOwning so a ghost-run tests both the counter signal and
// the owning-SM-stopped signal.

UCLASS()
class UCk_SmTest_GraphWalk_Top_State_A : UCk_SmState_EntityScript
{
    UFUNCTION(BlueprintOverride)
    void DoDefineState(FCk_Handle_SmState_UnderConstruction& InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        auto ToB = AddTransition(InHandle, UCk_SmTest_GraphWalk_Top_State_B);
        auto Cond = AddCondition(ToB, UCk_SmTest_Condition_PolledFalse);

        AddTask(InHandle, UCk_SmTest_Task_EnterCount_Top_A);
    }

    UFUNCTION(BlueprintOverride)
    void DoEnterState(FCk_Handle_SmState InHandle, ECk_Sm_NetContext InNetContext) {}

    UFUNCTION(BlueprintOverride)
    void DoExitState(FCk_Handle_SmState InHandle, ECk_Sm_NetContext InNetContext) {}
};

UCLASS()
class UCk_SmTest_GraphWalk_Top_State_B : UCk_SmState_EntityScript
{
    UFUNCTION(BlueprintOverride)
    void DoDefineState(FCk_Handle_SmState_UnderConstruction& InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        auto ToC = AddTransition(InHandle, UCk_SmTest_GraphWalk_Top_State_C);
        auto Cond = AddCondition(ToC, UCk_SmTest_Condition_PolledFalse);

        AddTask(InHandle, UCk_SmTest_Task_EnterCount_Top_B);
    }

    UFUNCTION(BlueprintOverride)
    void DoEnterState(FCk_Handle_SmState InHandle, ECk_Sm_NetContext InNetContext) {}

    UFUNCTION(BlueprintOverride)
    void DoExitState(FCk_Handle_SmState InHandle, ECk_Sm_NetContext InNetContext) {}
};

UCLASS()
class UCk_SmTest_GraphWalk_Top_State_C : UCk_SmState_EntityScript
{
    UFUNCTION(BlueprintOverride)
    void DoDefineState(FCk_Handle_SmState_UnderConstruction& InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        auto ToD = AddTransition(InHandle, UCk_SmTest_GraphWalk_Top_State_D);
        auto Cond = AddCondition(ToD, UCk_SmTest_Condition_PolledFalse);

        AddTask(InHandle, UCk_SmTest_Task_EnterCount_Top_C);
    }

    UFUNCTION(BlueprintOverride)
    void DoEnterState(FCk_Handle_SmState InHandle, ECk_Sm_NetContext InNetContext) {}

    UFUNCTION(BlueprintOverride)
    void DoExitState(FCk_Handle_SmState InHandle, ECk_Sm_NetContext InNetContext) {}
};

UCLASS()
class UCk_SmTest_GraphWalk_Top_State_D : UCk_SmState_EntityScript
{
    UFUNCTION(BlueprintOverride)
    void DoDefineState(FCk_Handle_SmState_UnderConstruction& InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        auto ToE = AddTransition(InHandle, UCk_SmTest_GraphWalk_Top_State_E);
        auto Cond = AddCondition(ToE, UCk_SmTest_Condition_PolledFalse);

        AddTask(InHandle, UCk_SmTest_Task_EnterCount_Top_D);
    }

    UFUNCTION(BlueprintOverride)
    void DoEnterState(FCk_Handle_SmState InHandle, ECk_Sm_NetContext InNetContext) {}

    UFUNCTION(BlueprintOverride)
    void DoExitState(FCk_Handle_SmState InHandle, ECk_Sm_NetContext InNetContext) {}
};

// Terminal state: no outgoing transitions, RequestStopOwning task. If
// ghost-run regresses, this stops the real SM during capture or setup.
UCLASS()
class UCk_SmTest_GraphWalk_Top_State_E : UCk_SmState_EntityScript
{
    UFUNCTION(BlueprintOverride)
    void DoDefineState(FCk_Handle_SmState_UnderConstruction& InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        AddTask(InHandle, UCk_SmTest_Task_RequestStopOwning_Top);
    }

    UFUNCTION(BlueprintOverride)
    void DoEnterState(FCk_Handle_SmState InHandle, ECk_Sm_NetContext InNetContext) {}

    UFUNCTION(BlueprintOverride)
    void DoExitState(FCk_Handle_SmState InHandle, ECk_Sm_NetContext InNetContext) {}
};

// ============================================================================
// SUB-SM LINEAR CHAIN: A -> B -> C -> D -> E
// ============================================================================
//
// Same topology as the top-level chain, different task classes so attribution
// (top vs sub) survives a regression. Wrapped by the parent state below via
// UCk_SmTask_SubStateMachine.

UCLASS()
class UCk_SmTest_GraphWalk_Sub_State_A : UCk_SmState_EntityScript
{
    UFUNCTION(BlueprintOverride)
    void DoDefineState(FCk_Handle_SmState_UnderConstruction& InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        auto ToB = AddTransition(InHandle, UCk_SmTest_GraphWalk_Sub_State_B);
        auto Cond = AddCondition(ToB, UCk_SmTest_Condition_PolledFalse);

        AddTask(InHandle, UCk_SmTest_Task_EnterCount_Sub_A);
    }

    UFUNCTION(BlueprintOverride)
    void DoEnterState(FCk_Handle_SmState InHandle, ECk_Sm_NetContext InNetContext) {}

    UFUNCTION(BlueprintOverride)
    void DoExitState(FCk_Handle_SmState InHandle, ECk_Sm_NetContext InNetContext) {}
};

UCLASS()
class UCk_SmTest_GraphWalk_Sub_State_B : UCk_SmState_EntityScript
{
    UFUNCTION(BlueprintOverride)
    void DoDefineState(FCk_Handle_SmState_UnderConstruction& InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        auto ToC = AddTransition(InHandle, UCk_SmTest_GraphWalk_Sub_State_C);
        auto Cond = AddCondition(ToC, UCk_SmTest_Condition_PolledFalse);

        AddTask(InHandle, UCk_SmTest_Task_EnterCount_Sub_B);
    }

    UFUNCTION(BlueprintOverride)
    void DoEnterState(FCk_Handle_SmState InHandle, ECk_Sm_NetContext InNetContext) {}

    UFUNCTION(BlueprintOverride)
    void DoExitState(FCk_Handle_SmState InHandle, ECk_Sm_NetContext InNetContext) {}
};

UCLASS()
class UCk_SmTest_GraphWalk_Sub_State_C : UCk_SmState_EntityScript
{
    UFUNCTION(BlueprintOverride)
    void DoDefineState(FCk_Handle_SmState_UnderConstruction& InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        auto ToD = AddTransition(InHandle, UCk_SmTest_GraphWalk_Sub_State_D);
        auto Cond = AddCondition(ToD, UCk_SmTest_Condition_PolledFalse);

        AddTask(InHandle, UCk_SmTest_Task_EnterCount_Sub_C);
    }

    UFUNCTION(BlueprintOverride)
    void DoEnterState(FCk_Handle_SmState InHandle, ECk_Sm_NetContext InNetContext) {}

    UFUNCTION(BlueprintOverride)
    void DoExitState(FCk_Handle_SmState InHandle, ECk_Sm_NetContext InNetContext) {}
};

UCLASS()
class UCk_SmTest_GraphWalk_Sub_State_D : UCk_SmState_EntityScript
{
    UFUNCTION(BlueprintOverride)
    void DoDefineState(FCk_Handle_SmState_UnderConstruction& InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        auto ToE = AddTransition(InHandle, UCk_SmTest_GraphWalk_Sub_State_E);
        auto Cond = AddCondition(ToE, UCk_SmTest_Condition_PolledFalse);

        AddTask(InHandle, UCk_SmTest_Task_EnterCount_Sub_D);
    }

    UFUNCTION(BlueprintOverride)
    void DoEnterState(FCk_Handle_SmState InHandle, ECk_Sm_NetContext InNetContext) {}

    UFUNCTION(BlueprintOverride)
    void DoExitState(FCk_Handle_SmState InHandle, ECk_Sm_NetContext InNetContext) {}
};

UCLASS()
class UCk_SmTest_GraphWalk_Sub_State_E : UCk_SmState_EntityScript
{
    UFUNCTION(BlueprintOverride)
    void DoDefineState(FCk_Handle_SmState_UnderConstruction& InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        AddTask(InHandle, UCk_SmTest_Task_RequestStopOwning_Sub);
    }

    UFUNCTION(BlueprintOverride)
    void DoEnterState(FCk_Handle_SmState InHandle, ECk_Sm_NetContext InNetContext) {}

    UFUNCTION(BlueprintOverride)
    void DoExitState(FCk_Handle_SmState InHandle, ECk_Sm_NetContext InNetContext) {}
};

// ============================================================================
// SUB-SM WRAPPER - parent state hosting the sub-SM as a SubStateMachine task
// ============================================================================

UCLASS()
class UCk_SmTest_GraphWalk_SubSmTask : UCk_SmTask_SubStateMachine
{
    default _InitialStateClass = UCk_SmTest_GraphWalk_Sub_State_A;
    default _CompletionBehavior = ECk_SmTask_SubSm_CompletionBehavior::KeepRunning;
};

// Parent wrapper state for the sub-SM variant. Polled-false self-loop would
// require a second state, so we keep this as a terminal state holding only
// the SubStateMachine task - the parent SM stays here indefinitely while
// the sub-SM is the system under test.
UCLASS()
class UCk_SmTest_GraphWalk_SubSmWrapper_State : UCk_SmState_EntityScript
{
    UFUNCTION(BlueprintOverride)
    void DoDefineState(FCk_Handle_SmState_UnderConstruction& InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        AddTask(InHandle, UCk_SmTest_GraphWalk_SubSmTask);
    }

    UFUNCTION(BlueprintOverride)
    void DoEnterState(FCk_Handle_SmState InHandle, ECk_Sm_NetContext InNetContext) {}

    UFUNCTION(BlueprintOverride)
    void DoExitState(FCk_Handle_SmState InHandle, ECk_Sm_NetContext InNetContext)
    {
        auto Recorder = UCk_AutoTest_Sm_RecorderSubsystem::Get();
        if (ck::IsValid(Recorder))
        {
            Recorder.RecordEvent(UCk_SmTest_GraphWalk_SubSmWrapper_State,
                ECk_AutoTest_Sm_EventKind::DoExitState, InNetContext);
        }
    }
};

// A valid authored task class with an intentionally absent required nested
// state. The disabled machine in the native GraphWalk test never enters it.
UCLASS()
class UCk_SmTest_GraphWalk_InvalidSubSmTask : UCk_SmTask_SubStateMachine
{
};

UCLASS()
class UCk_SmTest_GraphWalk_InvalidWrapper_State : UCk_SmState_EntityScript
{
    UFUNCTION(BlueprintOverride)
    void DoDefineState(FCk_Handle_SmState_UnderConstruction& InHandle)
    {
        AddTask(InHandle, UCk_SmTest_GraphWalk_InvalidSubSmTask);
    }

    UFUNCTION(BlueprintOverride)
    void DoEnterState(FCk_Handle_SmState InHandle, ECk_Sm_NetContext InNetContext) {}

    UFUNCTION(BlueprintOverride)
    void DoExitState(FCk_Handle_SmState InHandle, ECk_Sm_NetContext InNetContext) {}
};

// ============================================================================
