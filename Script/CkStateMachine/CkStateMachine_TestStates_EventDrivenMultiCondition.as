// ============================================================================
// SM EVENT-DRIVEN MULTI-CONDITION - REGRESSION TEST STATES
// ============================================================================
//
// Guards the "event-driven condition Pass results are preserved across
// transition Reset cycles" invariant. A transition with multiple event-
// driven conditions must fire exactly when ALL of them resolve Pass, even
// when those events resolve at different times - and even though the
// state evaluator's Fail/Reset cycle runs many iterations between the
// first and last events arriving.
//
// Topology:
//
//     Idle -> Finish    (one transition, TWO event-driven conditions)
//
// Conditions:
//   - FastEvent (timer): MarkSatisfied at FastDelaySeconds (~0.1s).
//   - SlowEvent (timer): MarkSatisfied at SlowDelaySeconds (~0.4s).
//
// Sequence of interest:
//   t=0    : Idle entered. Both conds Fail (auto-Fail resting state).
//            transition.Evaluate sees Fail -> trans Fail. state.Evaluate
//            cycles trans through Reset.
//   t~0.1s : FastEvent -> MarkSatisfied. Cond Pass.
//            transition.Evaluate walks: FastEvent Pass -> SlowEvent Fail
//            (still) -> trans Fail. State keeps cycling.
//   t~0.1-0.4s : multiple Reset cycles. FastEvent's Pass MUST be
//            preserved across these cycles (event-driven cond results
//            stick across Reset by framework design).
//   t~0.4s : SlowEvent -> MarkSatisfied. Cond Pass.
//            transition.Evaluate walks: both Pass -> trans Pass -> fire.
//            SM transitions to Finish.
//
// PASS criterion: Counter_Finish == 1 (SM transitioned exactly once).
// FAIL: Counter_Finish == 0 means FastEvent's Pass got clobbered during
//       the Reset cycle, so when SlowEvent eventually fired the transition
//       still saw a Fail condition (the previously-Pass FastEvent) and
//       never resolved to Pass.

namespace SmEventDrivenMultiCondition_Registry
{
    ACk_SmTest_EventDrivenMultiCondition_GymActor TryGet_Source(FCk_Handle InCallback)
    {
        FCk_Handle Context;
        if (!UCk_StateMachineGym_Runtime_UE::TryGet_SourceContext(
                InCallback, Context) ||
            !utils_owning_actor::Has(Context))
        { return nullptr; }
        auto Actor = Cast<ACk_SmTest_EventDrivenMultiCondition_GymActor>(
            utils_owning_actor::TryGet_EntityOwningActor(Context));
        if (ck::Is_NOT_Valid(Actor) || Actor.IsActorBeingDestroyed() ||
            Actor.Get_SourceClosed() || Context != Actor.Get_SourcePrimary())
        { return nullptr; }
        FCk_Handle Pass;
        if (!UCk_StateMachineGym_Runtime_UE::TryGet_DirectPassChild(
                InCallback, Context, Pass) || Actor.SmEntity != Pass)
        { return nullptr; }
        auto Roots = Actor.Get_SourceRoots();
        if (Roots.Num() != 1 || Roots[0] != Pass ||
            Actor.Get_SourceConstructedCount() != 1 ||
            !Actor.Get_HasCurrentSourceContext())
        { return nullptr; }
        // Initial state callbacks can run synchronously inside Add, before
        // OnEntityConstructed stores Add's returned typed handle.
        if (ck::IsValid(Actor.SmHandle) &&
            FCk_Handle(Actor.SmHandle) != Pass)
        { Actor.Request_CloseSource(); return nullptr; }
        return Actor;
    }

    void Increment_Finish(FCk_Handle InCallback)
    {
        auto Actor = TryGet_Source(InCallback);
        if (ck::IsValid(Actor)) { Actor.Counter_Finish += 1; }
    }

    bool HasExactTimer(FCk_Handle_Timer InTimer, FCk_Handle InCondition)
    {
        auto TimerEntity = FCk_Handle(InTimer);
        return ck::IsValid(InTimer) && ck::IsValid(InCondition) &&
            UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(TimerEntity) &&
            !utils_entity_lifetime::Get_IsPendingDestroy(
                TimerEntity, ECk_EntityLifetime_DestructionPhase::BeginDestroy) &&
            utils_entity_lifetime::Has_LifetimeOwner(TimerEntity) &&
            utils_entity_lifetime::Get_LifetimeOwner(TimerEntity) == InCondition;
    }
}

// ============================================================================
// CONDITIONS - both are event-driven timer gates that arm at EnterCondition
// and call MarkSatisfied when their timer fires. Different delays so they
// resolve at different points in the Reset cycle.
// ============================================================================

UCLASS()
class UCk_SmTest_EventDrivenMultiCondition_Condition_FastEvent : UCk_SmCondition_EventDriven
{
    private FCk_Handle _Condition;
    private FCk_Handle_Timer _Timer;

    UFUNCTION(BlueprintOverride)
    void DoEnterCondition(FCk_Handle_SmCondition InHandle, ECk_Sm_NetContext InNetContext)
    {
        auto _CkPerfScope = ck::ScopedStat();
        auto Actor = SmEventDrivenMultiCondition_Registry::TryGet_Source(FCk_Handle(InHandle));
        if (ck::Is_NOT_Valid(Actor)) { return; }
        auto Delay = Actor.FastDelaySeconds;
        if (!Math::IsFinite(Delay) || Delay <= 0.0f)
        { Actor.Request_CloseSource(); return; }
        auto TimerParams = FCk_Timer_Spec(FCk_Time(Delay));
        TimerParams
            .Set_StartingState(ECk_Timer_State::Running)
            .Set_Behavior(ECk_Timer_Behavior::PauseOnDone);

        _Condition = FCk_Handle(InHandle);
        _Timer = utils_timer::Add(InHandle, TimerParams);
        if (!SmEventDrivenMultiCondition_Registry::HasExactTimer(_Timer, _Condition))
        {
            auto Current = SmEventDrivenMultiCondition_Registry::TryGet_Source(_Condition);
            if (ck::IsValid(Current)) { Current.Request_CloseSource(); }
            return;
        }
        Actor = SmEventDrivenMultiCondition_Registry::TryGet_Source(_Condition);
        if (ck::Is_NOT_Valid(Actor)) { return; }
        _Timer.BindTo_OnDone(FCk_Delegate_Timer(this, n"OnDelayElapsed"));
    }

    UFUNCTION(BlueprintOverride)
    void DoExitCondition(FCk_Handle_SmCondition InHandle, ECk_Sm_NetContext InNetContext)
    { _Condition = FCk_Handle(); _Timer = FCk_Handle_Timer(); }

    UFUNCTION()
    private void OnDelayElapsed(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (_Timer != InTimer ||
            !SmEventDrivenMultiCondition_Registry::HasExactTimer(InTimer, _Condition) ||
            ck::Is_NOT_Valid(SmEventDrivenMultiCondition_Registry::TryGet_Source(_Condition)))
        { return; }
        MarkSatisfied();
    }
};

UCLASS()
class UCk_SmTest_EventDrivenMultiCondition_Condition_SlowEvent : UCk_SmCondition_EventDriven
{
    private FCk_Handle _Condition;
    private FCk_Handle_Timer _Timer;

    UFUNCTION(BlueprintOverride)
    void DoEnterCondition(FCk_Handle_SmCondition InHandle, ECk_Sm_NetContext InNetContext)
    {
        auto _CkPerfScope = ck::ScopedStat();
        auto Actor = SmEventDrivenMultiCondition_Registry::TryGet_Source(FCk_Handle(InHandle));
        if (ck::Is_NOT_Valid(Actor)) { return; }
        auto Delay = Actor.SlowDelaySeconds;
        if (!Math::IsFinite(Delay) || Delay <= 0.0f)
        { Actor.Request_CloseSource(); return; }
        auto TimerParams = FCk_Timer_Spec(FCk_Time(Delay));
        TimerParams
            .Set_StartingState(ECk_Timer_State::Running)
            .Set_Behavior(ECk_Timer_Behavior::PauseOnDone);

        _Condition = FCk_Handle(InHandle);
        _Timer = utils_timer::Add(InHandle, TimerParams);
        if (!SmEventDrivenMultiCondition_Registry::HasExactTimer(_Timer, _Condition))
        {
            auto Current = SmEventDrivenMultiCondition_Registry::TryGet_Source(_Condition);
            if (ck::IsValid(Current)) { Current.Request_CloseSource(); }
            return;
        }
        Actor = SmEventDrivenMultiCondition_Registry::TryGet_Source(_Condition);
        if (ck::Is_NOT_Valid(Actor)) { return; }
        _Timer.BindTo_OnDone(FCk_Delegate_Timer(this, n"OnDelayElapsed"));
    }

    UFUNCTION(BlueprintOverride)
    void DoExitCondition(FCk_Handle_SmCondition InHandle, ECk_Sm_NetContext InNetContext)
    { _Condition = FCk_Handle(); _Timer = FCk_Handle_Timer(); }

    UFUNCTION()
    private void OnDelayElapsed(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (_Timer != InTimer ||
            !SmEventDrivenMultiCondition_Registry::HasExactTimer(InTimer, _Condition) ||
            ck::Is_NOT_Valid(SmEventDrivenMultiCondition_Registry::TryGet_Source(_Condition)))
        { return; }
        MarkSatisfied();
    }
};

// ============================================================================
// COUNTER TASK - bumps the Finish counter on entry to the terminal state.
// ============================================================================

UCLASS()
class UCk_SmTest_EventDrivenMultiCondition_Task_Finish : UCk_SmTask_EntityScript
{
    default _TaskMode = ECk_SmTaskMode::EnterExitOnly;

    UFUNCTION(BlueprintOverride)
    void DoEnterTask(FCk_Handle_SmTask InHandle, ECk_Sm_NetContext InNetContext)
    { SmEventDrivenMultiCondition_Registry::Increment_Finish(FCk_Handle(InHandle)); }
};

// ============================================================================
// STATES
// ============================================================================

UCLASS()
class UCk_SmTest_EventDrivenMultiCondition_State_Finish : UCk_SmState_EntityScript
{
    UFUNCTION(BlueprintOverride)
    void DoDefineState(FCk_Handle_SmState_UnderConstruction& InHandle)
    { AddTask(InHandle, UCk_SmTest_EventDrivenMultiCondition_Task_Finish); }
};

UCLASS()
class UCk_SmTest_EventDrivenMultiCondition_State_Idle : UCk_SmState_EntityScript
{
    UFUNCTION(BlueprintOverride)
    void DoDefineState(FCk_Handle_SmState_UnderConstruction& InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        // Single transition with TWO event-driven conditions. The transition
        // must fire exactly once - when both conditions have resolved Pass.
        // The framework's contract is that event-driven conditions preserve
        // their last-known result across transition Reset (so FastEvent's
        // Pass survives the cycling between t=0.1s and t=0.4s).
        auto ToFinish = AddTransition(InHandle, UCk_SmTest_EventDrivenMultiCondition_State_Finish);
        AddCondition(ToFinish, UCk_SmTest_EventDrivenMultiCondition_Condition_FastEvent);
        AddCondition(ToFinish, UCk_SmTest_EventDrivenMultiCondition_Condition_SlowEvent);
    }
};

// ============================================================================
