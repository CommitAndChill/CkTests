// ============================================================================
// SM RACING EVENT-DRIVEN TRANSITIONS - REGRESSION TEST STATES
// ============================================================================
//
// What this test guards against:
//
// FProcessor_SmState_Evaluate walks a state's outgoing transitions in
// declaration order and Break's the moment it sees an Undetermined transition,
// activating that transition's evaluation but never inspecting any later
// sibling. When two event-driven transitions race on the same state, a later
// transition that has already resolved Pass is silently ignored if any
// earlier transition is still Undetermined - and worse, with the bug present
// the second transition's condition never gets activated at all (its timer
// never starts), so the slow-first transition wins by default at its delay.
//
// Topology:
//
//     Idle -+-> DestA   (event-driven timer, slow - added FIRST)
//           `-> DestB   (event-driven timer, fast - added SECOND, should win)
//
// Both transitions are pure event-driven timer conditions. With the fix in
// place, every Undetermined sibling is activated each evaluation pass; the
// fast timer fires first and ToDestB Pass'es first -> DestB wins. Without
// the fix, only ToDestA's evaluation ever begins; ToDestA Pass'es at its
// (slower) delay -> DestA wins.
//
// PASS criterion: SM lands on DestB exactly once.
// FAIL: SM lands on DestA (the first-declared, slower transition that
//        blocked the evaluator).

namespace SmRacing_Registry
{
    ACk_SmTest_RacingEventDriven_GymActor TryGet_Source(FCk_Handle InCallback)
    {
        FCk_Handle Context;
        if (!UCk_StateMachineGym_Runtime_UE::TryGet_SourceContext(
                InCallback, Context) ||
            !utils_owning_actor::Has(Context))
        { return nullptr; }
        auto Actor = Cast<ACk_SmTest_RacingEventDriven_GymActor>(
            utils_owning_actor::TryGet_EntityOwningActor(Context));
        if (ck::Is_NOT_Valid(Actor) || Actor.IsActorBeingDestroyed() ||
            Actor.Get_SourceClosed() || Actor.SmEntity != Context)
        { return nullptr; }
        auto Roots = Actor.Get_SourceRoots();
        if (Roots.Num() != 1 || Roots[0] != Context ||
            Actor.Get_SourceConstructedCount() != 1 ||
            !Actor.Get_HasCurrentSourceContext())
        { return nullptr; }
        // Add may synchronously enter Idle before OnEntityConstructed assigns
        // the returned typed SM handle. Once assigned it must be this root.
        if (ck::IsValid(Actor.SmHandle) &&
            FCk_Handle(Actor.SmHandle) != Context)
        { Actor.Request_CloseSource(); return nullptr; }
        return Actor;
    }

    void Increment_DestA(FCk_Handle InCallback)
    {
        auto Actor = TryGet_Source(InCallback);
        if (ck::IsValid(Actor)) { Actor.Counter_DestA += 1; }
    }

    void Increment_DestB(FCk_Handle InCallback)
    {
        auto Actor = TryGet_Source(InCallback);
        if (ck::IsValid(Actor)) { Actor.Counter_DestB += 1; }
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
// CONDITIONS - event-driven timer Pass after a delay sourced from the gym
// actor (so a single set of state classes can be reused by both the visual
// gym station with watchable timing and the headless autotest with fast
// timing).
// ============================================================================

UCLASS()
class UCk_SmTest_Racing_Condition_SlowTimer : UCk_SmCondition_EventDriven
{
    private FCk_Handle _Condition;
    private FCk_Handle_Timer _Timer;

    UFUNCTION(BlueprintOverride)
    void DoEnterCondition(FCk_Handle_SmCondition InHandle, ECk_Sm_NetContext InNetContext)
    {
        auto _CkPerfScope = ck::ScopedStat();
        auto Actor = SmRacing_Registry::TryGet_Source(FCk_Handle(InHandle));
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
        if (!SmRacing_Registry::HasExactTimer(_Timer, _Condition))
        {
            auto Current = SmRacing_Registry::TryGet_Source(_Condition);
            if (ck::IsValid(Current)) { Current.Request_CloseSource(); }
            return;
        }
        Actor = SmRacing_Registry::TryGet_Source(_Condition);
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
            !SmRacing_Registry::HasExactTimer(InTimer, _Condition) ||
            ck::Is_NOT_Valid(SmRacing_Registry::TryGet_Source(_Condition)))
        { return; }
        MarkSatisfied();
    }
};

UCLASS()
class UCk_SmTest_Racing_Condition_FastTimer : UCk_SmCondition_EventDriven
{
    private FCk_Handle _Condition;
    private FCk_Handle_Timer _Timer;

    UFUNCTION(BlueprintOverride)
    void DoEnterCondition(FCk_Handle_SmCondition InHandle, ECk_Sm_NetContext InNetContext)
    {
        auto _CkPerfScope = ck::ScopedStat();
        auto Actor = SmRacing_Registry::TryGet_Source(FCk_Handle(InHandle));
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
        if (!SmRacing_Registry::HasExactTimer(_Timer, _Condition))
        {
            auto Current = SmRacing_Registry::TryGet_Source(_Condition);
            if (ck::IsValid(Current)) { Current.Request_CloseSource(); }
            return;
        }
        Actor = SmRacing_Registry::TryGet_Source(_Condition);
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
            !SmRacing_Registry::HasExactTimer(InTimer, _Condition) ||
            ck::Is_NOT_Valid(SmRacing_Registry::TryGet_Source(_Condition)))
        { return; }
        MarkSatisfied();
    }
};

// ============================================================================
// COUNTER TASKS
// ============================================================================

UCLASS()
class UCk_SmTest_Racing_Task_DestA : UCk_SmTask_EntityScript
{
    default _TaskMode = ECk_SmTaskMode::EnterExitOnly;

    UFUNCTION(BlueprintOverride)
    void DoEnterTask(FCk_Handle_SmTask InHandle, ECk_Sm_NetContext InNetContext)
    { SmRacing_Registry::Increment_DestA(FCk_Handle(InHandle)); }
};

UCLASS()
class UCk_SmTest_Racing_Task_DestB : UCk_SmTask_EntityScript
{
    default _TaskMode = ECk_SmTaskMode::EnterExitOnly;

    UFUNCTION(BlueprintOverride)
    void DoEnterTask(FCk_Handle_SmTask InHandle, ECk_Sm_NetContext InNetContext)
    { SmRacing_Registry::Increment_DestB(FCk_Handle(InHandle)); }
};

// ============================================================================
// STATES
// ============================================================================

UCLASS()
class UCk_SmTest_Racing_State_DestA : UCk_SmState_EntityScript
{
    UFUNCTION(BlueprintOverride)
    void DoDefineState(FCk_Handle_SmState_UnderConstruction& InHandle)
    { AddTask(InHandle, UCk_SmTest_Racing_Task_DestA); }
};

UCLASS()
class UCk_SmTest_Racing_State_DestB : UCk_SmState_EntityScript
{
    UFUNCTION(BlueprintOverride)
    void DoDefineState(FCk_Handle_SmState_UnderConstruction& InHandle)
    { AddTask(InHandle, UCk_SmTest_Racing_Task_DestB); }
};

UCLASS()
class UCk_SmTest_Racing_State_Idle : UCk_SmState_EntityScript
{
    UFUNCTION(BlueprintOverride)
    void DoDefineState(FCk_Handle_SmState_UnderConstruction& InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        // ORDER MATTERS - ToDestA is declared first. With the bug, the state
        // evaluator hits ToDestA's Undetermined transition first, Break's,
        // and never inspects ToDestB. After the fix, every Undetermined
        // sibling is activated each pass and the first Pass wins; ToDestB
        // Pass'es first (faster timer) so it should be the chosen branch.
        auto ToDestA = AddTransition(InHandle, UCk_SmTest_Racing_State_DestA);
        AddCondition(ToDestA, UCk_SmTest_Racing_Condition_SlowTimer);

        auto ToDestB = AddTransition(InHandle, UCk_SmTest_Racing_State_DestB);
        AddCondition(ToDestB, UCk_SmTest_Racing_Condition_FastTimer);
    }
};

// ============================================================================
