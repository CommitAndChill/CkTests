// ============================================================================
// SM DIVERGENCE-FIRST-BRANCH (TIMED) - REGRESSION TEST STATES
// ============================================================================
//
// What this test guards against:
//
// The same divergence-point duplicate-task bug as the *vacuous* variant
// (CkStateMachine_TestStates_DivergenceFirstBranch.as), but exercised
// through a timer-gated transition pattern. Every linear hop here uses an
// event-driven timer condition (transition Pass'es ~50ms after the source
// state is entered), so the duplicate-creation race window opens around
// timer-driven Pass results rather than around vacuous Pass results. This
// is the configuration that originally surfaced the bug in real product
// code, where transitions were gated by dwell timers rather than firing
// vacuously.
//
// Why two variants exist:
//
// - Vacuous variant: transition has zero conditions. Transition_Evaluate
//   returns Pass on the first evaluation. The race window between the
//   transition firing and the source state being torn down is one frame.
// - Timed variant (this one): transition has an event-driven timer
//   condition. The timer's OnDone fires later, then the transition
//   evaluates Pass. The race window is wider in wall-clock time but the
//   logical sequence (state Pending-Exit -> condition still alive ->
//   transition still cached as Pass -> state re-evaluates) is the same.
//
// Both variants run the same divergence-point shape and assertions.
// Together they cover both fast-path and slow-path Pass-cache handling
// during state teardown.
//
// Topology:
//
//     Enter -> Idle -> Branch -+-> Left  -> Finish
//                              `-> Right -/
//
// Each linear transition: gated by FastDelay (timer condition).
// Each divergence transition: FastDelay AND a polled payment-method check
// - gives the SM a deterministic single-branch Pass at the divergence.
//
// PASS criterion: each per-state task fires exactly once per sub-SM cycle,
// regardless of AddTransition order.
// FAIL: first-added-and-chosen branch's task fires twice (cached Pass
// result on the dying source state's transition fires Request_Transition
// a second time).

UENUM()
enum ECk_SmTest_DivergenceTimed_PaymentChoice
{
    Left,
    Right,
}

// ============================================================================
// Exact source routing. No world-wide actor lookup is permitted: A and B
// roots can overlap with other actors, and old A must be inert during B.
// ============================================================================

namespace SmDivergenceTimed_Regression
{
    ACk_SmTest_DivergenceTimed_GymActor TryGet_Source(FCk_Handle InCallback)
    {
        FCk_Handle Context;
        if (!UCk_StateMachineGym_Runtime_UE::TryGet_SourceContext(
                InCallback, Context) ||
            !utils_owning_actor::Has(Context))
        { return nullptr; }
        auto Actor = Cast<ACk_SmTest_DivergenceTimed_GymActor>(
            utils_owning_actor::TryGet_EntityOwningActor(Context));
        if (ck::Is_NOT_Valid(Actor) || Actor.IsActorBeingDestroyed() ||
            Actor.Get_SourceClosed() ||
            Actor.Get_ActivePassKindForRoot(Context) == 0)
        { return nullptr; }
        return Actor;
    }

    bool Increment(FCk_Handle InCallback, FName InLabel)
    {
        auto Actor = TryGet_Source(InCallback);
        if (ck::Is_NOT_Valid(Actor)) { return false; }
        Actor.Increment_Counter(InLabel);
        return true;
    }

    int32 Get_PassKind(FCk_Handle InCallback)
    {
        FCk_Handle Context;
        auto Actor = TryGet_Source(InCallback);
        if (ck::Is_NOT_Valid(Actor) ||
            !UCk_StateMachineGym_Runtime_UE::TryGet_SourceContext(
                InCallback, Context))
        { return 0; }
        return Actor.Get_ActivePassKindForRoot(Context);
    }

    bool HasExactTimer(FCk_Handle_Timer InTimer, FCk_Handle InCondition)
    {
        auto Timer = FCk_Handle(InTimer);
        return Timer != FCk_Handle() && InCondition != FCk_Handle() &&
            UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Timer) &&
            UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(
                InCondition) &&
            ck::IsValid(InTimer) && ck::IsValid(InCondition) &&
            !utils_entity_lifetime::Get_IsPendingDestroy(
                InCondition, ECk_EntityLifetime_DestructionPhase::BeginDestroy) &&
            !utils_entity_lifetime::Get_IsPendingDestroy(
                Timer, ECk_EntityLifetime_DestructionPhase::BeginDestroy) &&
            utils_entity_lifetime::Has_LifetimeOwner(Timer) &&
            utils_entity_lifetime::Get_LifetimeOwner(Timer) == InCondition;
    }
}

// ============================================================================
// CONDITIONS
// ============================================================================

// Event-driven timer condition. On EnterCondition it arms a CkTimer for
// DelaySeconds; when the timer's OnDone fires, MarkSatisfied flips the
// condition to Pass. This is the canonical "wait a moment, then proceed"
// transition gate - used by every linear hop in this test SM so each
// transition's Pass result is established asynchronously (mirroring how
// real game-code transitions tend to be timed rather than vacuous).
//
// Default delay is small (0.05s) so a full sub-SM cycle (Enter -> Idle ->
// Branch -> chosen -> Finish, 4-5 transitions) completes in ~0.25s, well
// inside the gym's settle window.
UCLASS()
class UCk_SmTest_DivergenceTimed_Condition_FastDelay : UCk_SmCondition_EventDriven
{
    UPROPERTY(EditAnywhere)
    float32 DelaySeconds = 0.05f;
    private FCk_Handle _Condition;
    private FCk_Handle_Timer _Timer;

    UFUNCTION(BlueprintOverride)
    void DoEnterCondition(FCk_Handle_SmCondition InHandle, ECk_Sm_NetContext InNetContext)
    {
        auto _CkPerfScope = ck::ScopedStat();
        auto Actor = SmDivergenceTimed_Regression::TryGet_Source(
            FCk_Handle(InHandle));
        if (ck::Is_NOT_Valid(Actor)) { return; }
        if (!Math::IsFinite(DelaySeconds) || DelaySeconds <= 0.0f)
        { Actor.Request_CloseSource(); return; }
        auto TimerParams = FCk_Timer_Spec(FCk_Time(DelaySeconds));
        TimerParams
            .Set_StartingState(ECk_Timer_State::Running)
            .Set_Behavior(ECk_Timer_Behavior::PauseOnDone);

        _Condition = FCk_Handle(InHandle);
        _Timer = utils_timer::Add(InHandle, TimerParams);
        if (!SmDivergenceTimed_Regression::HasExactTimer(
                _Timer, _Condition))
        {
            auto Current = SmDivergenceTimed_Regression::TryGet_Source(
                _Condition);
            if (ck::IsValid(Current)) { Current.Request_CloseSource(); }
            return;
        }
        Actor = SmDivergenceTimed_Regression::TryGet_Source(_Condition);
        if (ck::Is_NOT_Valid(Actor)) { return; }
        _Timer.BindTo_OnDone(FCk_Delegate_Timer(this, n"OnDelayElapsed"));
    }

    UFUNCTION(BlueprintOverride)
    void DoExitCondition(FCk_Handle_SmCondition InHandle,
        ECk_Sm_NetContext InNetContext)
    { _Condition = FCk_Handle(); _Timer = FCk_Handle_Timer(); }

    UFUNCTION()
    private void OnDelayElapsed(
        FCk_Handle_Timer InTimer,
        FCk_Chrono InChrono,
        FCk_Time InDeltaT)
    {
        if (_Timer != InTimer ||
            !SmDivergenceTimed_Regression::HasExactTimer(
                InTimer, _Condition) ||
            ck::Is_NOT_Valid(
                SmDivergenceTimed_Regression::TryGet_Source(_Condition)))
        { return; }
        MarkSatisfied();
    }
};

// Polled - true when PaymentChoice == Left.
UCLASS()
class UCk_SmTest_DivergenceTimed_Condition_PaymentIsLeft : UCk_SmCondition_Polled
{
    UFUNCTION(BlueprintOverride)
    bool DoEvaluate(FCk_Handle_SmCondition InHandle, FCk_Time InDeltaT) const
    {
        auto _CkPerfScope = ck::ScopedStat();
        return SmDivergenceTimed_Regression::Get_PassKind(
            FCk_Handle(InHandle)) == 1;
    }
};

// Polled - true when PaymentChoice == Right.
UCLASS()
class UCk_SmTest_DivergenceTimed_Condition_PaymentIsRight : UCk_SmCondition_Polled
{
    UFUNCTION(BlueprintOverride)
    bool DoEvaluate(FCk_Handle_SmCondition InHandle, FCk_Time InDeltaT) const
    {
        auto _CkPerfScope = ck::ScopedStat();
        return SmDivergenceTimed_Regression::Get_PassKind(
            FCk_Handle(InHandle)) == 2;
    }
};

// ============================================================================
// COUNTER TASKS (EnterExitOnly)
// ============================================================================

UCLASS()
class UCk_SmTest_DivergenceTimed_Task_Enter : UCk_SmTask_EntityScript
{
    default _TaskMode = ECk_SmTaskMode::EnterExitOnly;

    UFUNCTION(BlueprintOverride)
    void DoEnterTask(FCk_Handle_SmTask InHandle, ECk_Sm_NetContext InNetContext)
    { SmDivergenceTimed_Regression::Increment(FCk_Handle(InHandle), n"Enter"); }
};

UCLASS()
class UCk_SmTest_DivergenceTimed_Task_Idle : UCk_SmTask_EntityScript
{
    default _TaskMode = ECk_SmTaskMode::EnterExitOnly;

    UFUNCTION(BlueprintOverride)
    void DoEnterTask(FCk_Handle_SmTask InHandle, ECk_Sm_NetContext InNetContext)
    { SmDivergenceTimed_Regression::Increment(FCk_Handle(InHandle), n"Idle"); }
};

UCLASS()
class UCk_SmTest_DivergenceTimed_Task_Branch : UCk_SmTask_EntityScript
{
    default _TaskMode = ECk_SmTaskMode::EnterExitOnly;

    UFUNCTION(BlueprintOverride)
    void DoEnterTask(FCk_Handle_SmTask InHandle, ECk_Sm_NetContext InNetContext)
    { SmDivergenceTimed_Regression::Increment(FCk_Handle(InHandle), n"Branch"); }
};

UCLASS()
class UCk_SmTest_DivergenceTimed_Task_Left : UCk_SmTask_EntityScript
{
    default _TaskMode = ECk_SmTaskMode::EnterExitOnly;

    UFUNCTION(BlueprintOverride)
    void DoEnterTask(FCk_Handle_SmTask InHandle, ECk_Sm_NetContext InNetContext)
    { SmDivergenceTimed_Regression::Increment(FCk_Handle(InHandle), n"Left"); }
};

UCLASS()
class UCk_SmTest_DivergenceTimed_Task_Right : UCk_SmTask_EntityScript
{
    default _TaskMode = ECk_SmTaskMode::EnterExitOnly;

    UFUNCTION(BlueprintOverride)
    void DoEnterTask(FCk_Handle_SmTask InHandle, ECk_Sm_NetContext InNetContext)
    { SmDivergenceTimed_Regression::Increment(FCk_Handle(InHandle), n"Right"); }
};

UCLASS()
class UCk_SmTest_DivergenceTimed_Task_Finish : UCk_SmTask_EntityScript
{
    default _TaskMode = ECk_SmTaskMode::EnterExitOnly;

    UFUNCTION(BlueprintOverride)
    void DoEnterTask(FCk_Handle_SmTask InHandle, ECk_Sm_NetContext InNetContext)
    {
        auto _CkPerfScope = ck::ScopedStat();
        if (!SmDivergenceTimed_Regression::Increment(
                FCk_Handle(InHandle), n"Finish"))
        { return; }

        // Stop the owning sub-SM so the gym sees a settled state and can
        // verify counters. The wrapping UCk_SmTest_DivergenceTimed_SubSmTask
        // uses SucceedOnStop completion behaviour.
        auto OwningSm = Get_OwningStateMachine();
        auto SmEntity = FCk_Handle(OwningSm);
        FCk_Handle TaskContext;
        FCk_Handle SmContext;
        auto Actor = SmDivergenceTimed_Regression::TryGet_Source(
            FCk_Handle(InHandle));
        if (ck::Is_NOT_Valid(Actor) ||
            !UCk_StateMachineGym_Runtime_UE::TryGet_SourceContext(
                FCk_Handle(InHandle), TaskContext))
        { return; }
        if (SmEntity == FCk_Handle() ||
            !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(
                SmEntity) ||
            ck::Is_NOT_Valid(OwningSm) ||
            utils_entity_lifetime::Get_IsPendingDestroy(
                SmEntity, ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
            !utils_state_machine::Has(SmEntity) ||
            !UCk_StateMachineGym_Runtime_UE::TryGet_SourceContext(
                SmEntity, SmContext) ||
            SmContext != TaskContext ||
            Actor.Get_ActivePassKindForRoot(TaskContext) == 0)
        { Actor.Request_CloseSource(); return; }
        utils_state_machine::Request_Stop(OwningSm);
    }
};

// ============================================================================
// SUB-SM STATES - every linear transition gated by FastDelay (timed Pass).
// The divergence transitions additionally have a polled payment-method
// check so exactly one branch wins deterministically.
// ============================================================================

UCLASS()
class UCk_SmTest_DivergenceTimed_State_Enter : UCk_SmState_EntityScript
{
    UFUNCTION(BlueprintOverride)
    void DoDefineState(FCk_Handle_SmState_UnderConstruction& InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        if (ck::Is_NOT_Valid(
                SmDivergenceTimed_Regression::TryGet_Source(
                    FCk_Handle(Get_OwnerStateMachine())))) { return; }
        AddTask(InHandle, UCk_SmTest_DivergenceTimed_Task_Enter);

        auto ToIdle = AddTransition(InHandle, UCk_SmTest_DivergenceTimed_State_Idle);
        AddCondition(ToIdle, UCk_SmTest_DivergenceTimed_Condition_FastDelay);
    }
};

UCLASS()
class UCk_SmTest_DivergenceTimed_State_Idle : UCk_SmState_EntityScript
{
    UFUNCTION(BlueprintOverride)
    void DoDefineState(FCk_Handle_SmState_UnderConstruction& InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        if (ck::Is_NOT_Valid(
                SmDivergenceTimed_Regression::TryGet_Source(
                    FCk_Handle(Get_OwnerStateMachine())))) { return; }
        AddTask(InHandle, UCk_SmTest_DivergenceTimed_Task_Idle);

        auto ToBranch = AddTransition(InHandle, UCk_SmTest_DivergenceTimed_State_Branch);
        AddCondition(ToBranch, UCk_SmTest_DivergenceTimed_Condition_FastDelay);
    }
};

// The divergence point - state with two outgoing transitions. AddTransition
// order is read from the gym actor at DefineState time so a single PIE
// session can exercise both orders by re-spawning the SM with a swapped
// flag. Each branch is gated by FastDelay (so it Pass'es timed, not
// vacuously) AND a polled payment-method check (so exactly one wins).
UCLASS()
class UCk_SmTest_DivergenceTimed_State_Branch : UCk_SmState_EntityScript
{
    UFUNCTION(BlueprintOverride)
    void DoDefineState(FCk_Handle_SmState_UnderConstruction& InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        const int32 PassKind = SmDivergenceTimed_Regression::Get_PassKind(
            FCk_Handle(Get_OwnerStateMachine()));
        if (PassKind != 1 && PassKind != 2) { return; }
        AddTask(InHandle, UCk_SmTest_DivergenceTimed_Task_Branch);

        if (PassKind == 1)
        {
            auto ToLeft = AddTransition(InHandle, UCk_SmTest_DivergenceTimed_State_Left);
            AddCondition(ToLeft, UCk_SmTest_DivergenceTimed_Condition_FastDelay);
            AddCondition(ToLeft, UCk_SmTest_DivergenceTimed_Condition_PaymentIsLeft);

            auto ToRight = AddTransition(InHandle, UCk_SmTest_DivergenceTimed_State_Right);
            AddCondition(ToRight, UCk_SmTest_DivergenceTimed_Condition_FastDelay);
            AddCondition(ToRight, UCk_SmTest_DivergenceTimed_Condition_PaymentIsRight);
        }
        else
        {
            auto ToRight = AddTransition(InHandle, UCk_SmTest_DivergenceTimed_State_Right);
            AddCondition(ToRight, UCk_SmTest_DivergenceTimed_Condition_FastDelay);
            AddCondition(ToRight, UCk_SmTest_DivergenceTimed_Condition_PaymentIsRight);

            auto ToLeft = AddTransition(InHandle, UCk_SmTest_DivergenceTimed_State_Left);
            AddCondition(ToLeft, UCk_SmTest_DivergenceTimed_Condition_FastDelay);
            AddCondition(ToLeft, UCk_SmTest_DivergenceTimed_Condition_PaymentIsLeft);
        }
    }
};

UCLASS()
class UCk_SmTest_DivergenceTimed_State_Left : UCk_SmState_EntityScript
{
    UFUNCTION(BlueprintOverride)
    void DoDefineState(FCk_Handle_SmState_UnderConstruction& InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        if (ck::Is_NOT_Valid(
                SmDivergenceTimed_Regression::TryGet_Source(
                    FCk_Handle(Get_OwnerStateMachine())))) { return; }
        AddTask(InHandle, UCk_SmTest_DivergenceTimed_Task_Left);

        auto ToFinish = AddTransition(InHandle, UCk_SmTest_DivergenceTimed_State_Finish);
        AddCondition(ToFinish, UCk_SmTest_DivergenceTimed_Condition_FastDelay);
    }
};

UCLASS()
class UCk_SmTest_DivergenceTimed_State_Right : UCk_SmState_EntityScript
{
    UFUNCTION(BlueprintOverride)
    void DoDefineState(FCk_Handle_SmState_UnderConstruction& InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        if (ck::Is_NOT_Valid(
                SmDivergenceTimed_Regression::TryGet_Source(
                    FCk_Handle(Get_OwnerStateMachine())))) { return; }
        AddTask(InHandle, UCk_SmTest_DivergenceTimed_Task_Right);

        auto ToFinish = AddTransition(InHandle, UCk_SmTest_DivergenceTimed_State_Finish);
        AddCondition(ToFinish, UCk_SmTest_DivergenceTimed_Condition_FastDelay);
    }
};

UCLASS()
class UCk_SmTest_DivergenceTimed_State_Finish : UCk_SmState_EntityScript
{
    UFUNCTION(BlueprintOverride)
    void DoDefineState(FCk_Handle_SmState_UnderConstruction& InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        if (ck::Is_NOT_Valid(
                SmDivergenceTimed_Regression::TryGet_Source(
                    FCk_Handle(Get_OwnerStateMachine())))) { return; }
        AddTask(InHandle, UCk_SmTest_DivergenceTimed_Task_Finish);
    }
};

// ============================================================================
// SUB-SM WRAPPER
// ============================================================================

UCLASS()
class UCk_SmTest_DivergenceTimed_SubSmTask : UCk_SmTask_SubStateMachine
{
    default _InitialStateClass = UCk_SmTest_DivergenceTimed_State_Enter;
    default _CompletionBehavior = ECk_SmTask_SubSm_CompletionBehavior::SucceedOnStop;
};

UCLASS()
class UCk_SmTest_DivergenceTimed_ParentState : UCk_SmState_EntityScript
{
    UFUNCTION(BlueprintOverride)
    void DoDefineState(FCk_Handle_SmState_UnderConstruction& InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        if (ck::Is_NOT_Valid(
                SmDivergenceTimed_Regression::TryGet_Source(
                    FCk_Handle(Get_OwnerStateMachine())))) { return; }
        AddTask(InHandle, UCk_SmTest_DivergenceTimed_SubSmTask);
    }
};

// ============================================================================
