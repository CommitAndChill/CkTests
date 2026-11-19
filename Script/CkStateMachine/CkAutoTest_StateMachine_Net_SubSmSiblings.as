// Language=angelscript

//============================================================================
// CK STATE MACHINE - NET SIBLING SUB-SMs (topologies only)
//============================================================================
//
// State graphs for the sibling sub-SM address tests. Driven by the C++ specs in
// Source/CkTests/Private/Net/CkStateMachine_Net_SubSmSiblings.spec.cpp
// (Ck.StateMachine.Net.SubSmSiblings.*) and
// Source/CkTests/Private/StateMachine/CkStateMachine_SubSmAddress.spec.cpp
// (Ck.StateMachine.SubSmAddress.*).
//
// Every transition is gated on one byte "input" attribute (ByteAttribute.Gyms.Intent.R)
// reaching a threshold, so a test raises the input one step at a time and each
// step moves exactly one sub-SM. The input attribute only exists on the subject
// entity; on a bare entity no condition is ever satisfied and every state is a sink.
//
// N1 - two sibling sub-SMs, the first one hosting a nested sub-SM:
//   N1_Hold [ A, B ]
//     A: N1A_Host [ L ] --(5)--> N1A_One --(6)--> N1A_Two
//     L: N1L_Idle --(2)--> N1L_One --(4)--> N1L_Two
//     B: N1B_Idle --(1)--> N1B_One --(3)--> N1B_Two
//
// N2 - a root transition, then two sibling sub-SMs:
//   N2_Wait --(1)--> N2_Hold [ A, B ]
//     A: N2A_Idle --(2)--> N2A_One --(4)--> N2A_Two
//     B: N2B_Idle --(3)--> N2B_One --(5)--> N2B_Two
//
// N3 - a sibling pair at depth 2, beside a sibling at depth 1:
//   N3_Hold [ A, B ]
//     A: N3A_Host [ C, D ]                   (A itself never moves)
//       C: N3C_Idle --(2)--> N3C_One --(4)--> N3C_Two
//       D: N3D_Idle --(3)--> N3D_One --(5)--> N3D_Two
//     B: N3B_Idle --(1)--> N3B_One --(6)--> N3B_Two
//
// N4 - a hosting state left and re-entered (exact input values, "=n"):
//   N4_Hold [ A, B ] --(=3)--> N4_Away --(=4)--> N4_Hold
//     A: N4A_Idle --(=1 or =6)--> N4A_One --(=2)--> N4A_Two
//     B: N4B_Idle --(=5)--> N4B_One
//============================================================================

// ----------------------------------------------------------------------------
// Satisfied while the byte input is >= _Threshold. The input only rises, so a
// satisfied condition stays satisfied.
// ----------------------------------------------------------------------------
UCLASS()
class UCk_SmNetSibTest_InputAtLeast1 : UCk_SmCondition_EventDriven
{
    protected int32 _Threshold = 1;

    private FCk_Handle_ByteAttribute _CachedAttribute;

    UFUNCTION(BlueprintOverride)
    void DoEnterCondition(FCk_Handle_SmCondition InHandle, ECk_Sm_NetContext InNetContext)
    {
        auto _CkPerfScope = ck::ScopedStat();

        auto SourceEntity = ck::Ctx(InHandle);
        if (ck::Is_NOT_Valid(SourceEntity))
        { return; }

        const auto InputTag = GameplayTags::ResolveGameplayTag(n"ByteAttribute.Gyms.Intent.R");
        _CachedAttribute = utils_byte_attribute::TryGet(SourceEntity, InputTag);
        if (ck::Is_NOT_Valid(_CachedAttribute))
        { return; }

        utils_byte_attribute::BindTo_OnValueChanged(_CachedAttribute, ECk_MinMaxCurrent::Current,
            FCk_Delegate_ByteAttribute_OnValueChanged(this, n"OnInputChanged"));

        Evaluate(int32(_CachedAttribute.Get_FinalValue(ECk_MinMaxCurrent::Current)));
    }

    UFUNCTION(BlueprintOverride)
    void DoExitCondition(FCk_Handle_SmCondition InHandle, ECk_Sm_NetContext InNetContext)
    {
        auto _CkPerfScope = ck::ScopedStat();

        if (ck::IsValid(_CachedAttribute))
        {
            utils_byte_attribute::UnbindFrom_OnValueChanged(_CachedAttribute, ECk_MinMaxCurrent::Current,
                FCk_Delegate_ByteAttribute_OnValueChanged(this, n"OnInputChanged"));
        }
    }

    UFUNCTION()
    private void OnInputChanged(FCk_Handle InOwner, FCk_Payload_ByteAttribute_OnValueChanged InPayload)
    {
        auto _CkPerfScope = ck::ScopedStat();
        Evaluate(int32(InPayload.Get_FinalValue()));
    }

    protected bool Get_IsMet(int32 InValue) const
    {
        return InValue >= _Threshold;
    }

    private void Evaluate(int32 InValue)
    {
        if (Get_IsMet(InValue))
        { MarkSatisfied(); }
        else
        { MarkUnsatisfied(); }
    }
}

UCLASS()
class UCk_SmNetSibTest_InputAtLeast2 : UCk_SmNetSibTest_InputAtLeast1
{
    default _Threshold = 2;
}

UCLASS()
class UCk_SmNetSibTest_InputAtLeast3 : UCk_SmNetSibTest_InputAtLeast1
{
    default _Threshold = 3;
}

UCLASS()
class UCk_SmNetSibTest_InputAtLeast4 : UCk_SmNetSibTest_InputAtLeast1
{
    default _Threshold = 4;
}

UCLASS()
class UCk_SmNetSibTest_InputAtLeast5 : UCk_SmNetSibTest_InputAtLeast1
{
    default _Threshold = 5;
}

UCLASS()
class UCk_SmNetSibTest_InputAtLeast6 : UCk_SmNetSibTest_InputAtLeast1
{
    default _Threshold = 6;
}

// ----------------------------------------------------------------------------
// Satisfied while the byte input is exactly _Threshold or _OrValue, so a state
// re-entered at a later input does not leave again on its own.
// ----------------------------------------------------------------------------
UCLASS(Abstract)
class UCk_SmNetSibTest_InputIs : UCk_SmNetSibTest_InputAtLeast1
{
    protected int32 _OrValue = -1;

    protected bool Get_IsMet(int32 InValue) const override
    {
        return InValue == _Threshold || InValue == _OrValue;
    }
}

UCLASS()
class UCk_SmNetSibTest_InputIs1Or6 : UCk_SmNetSibTest_InputIs
{
    default _Threshold = 1;
    default _OrValue = 6;
}

UCLASS()
class UCk_SmNetSibTest_InputIs2 : UCk_SmNetSibTest_InputIs
{
    default _Threshold = 2;
}

UCLASS()
class UCk_SmNetSibTest_InputIs3 : UCk_SmNetSibTest_InputIs
{
    default _Threshold = 3;
}

UCLASS()
class UCk_SmNetSibTest_InputIs4 : UCk_SmNetSibTest_InputIs
{
    default _Threshold = 4;
}

UCLASS()
class UCk_SmNetSibTest_InputIs5 : UCk_SmNetSibTest_InputIs
{
    default _Threshold = 5;
}

// ----------------------------------------------------------------------------
// One state of a chain: hosts up to two tasks (in this order), then at most one
// transition to _NextState gated on _NextCondition.
// ----------------------------------------------------------------------------
UCLASS(Abstract)
class UCk_SmNetSibTest_Step : UCk_SmState_EntityScript
{
    protected TSubclassOf<UCk_SmTask_EntityScript> _FirstTask;
    protected TSubclassOf<UCk_SmTask_EntityScript> _SecondTask;
    protected TSubclassOf<UCk_SmState_EntityScript> _NextState;
    protected TSubclassOf<UCk_SmCondition_EntityScript> _NextCondition;

    UFUNCTION(BlueprintOverride)
    void DoDefineState(FCk_Handle_SmState_UnderConstruction& InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();

        if (_FirstTask != nullptr)
        { AddTask(InHandle, _FirstTask); }

        if (_SecondTask != nullptr)
        { AddTask(InHandle, _SecondTask); }

        if (_NextState == nullptr)
        { return; }

        auto Transition = AddTransition(InHandle, _NextState);
        AddCondition(Transition, _NextCondition);
    }
}

// ============================================================================
// N1
// ============================================================================

UCLASS()
class UCk_SmNetSibTest_N1L_Two : UCk_SmNetSibTest_Step
{
}

UCLASS()
class UCk_SmNetSibTest_N1L_One : UCk_SmNetSibTest_Step
{
    default _NextState = UCk_SmNetSibTest_N1L_Two;
    default _NextCondition = UCk_SmNetSibTest_InputAtLeast4;
}

UCLASS()
class UCk_SmNetSibTest_N1L_Idle : UCk_SmNetSibTest_Step
{
    default _NextState = UCk_SmNetSibTest_N1L_One;
    default _NextCondition = UCk_SmNetSibTest_InputAtLeast2;
}

UCLASS()
class UCk_SmNetSibTest_N1L_Task : UCk_SmTask_SubStateMachine
{
    default _InitialStateClass = UCk_SmNetSibTest_N1L_Idle;
    default _CompletionBehavior = ECk_SmTask_SubSm_CompletionBehavior::KeepRunning;
}

UCLASS()
class UCk_SmNetSibTest_N1A_Two : UCk_SmNetSibTest_Step
{
}

UCLASS()
class UCk_SmNetSibTest_N1A_One : UCk_SmNetSibTest_Step
{
    default _NextState = UCk_SmNetSibTest_N1A_Two;
    default _NextCondition = UCk_SmNetSibTest_InputAtLeast6;
}

UCLASS()
class UCk_SmNetSibTest_N1A_Host : UCk_SmNetSibTest_Step
{
    default _FirstTask = UCk_SmNetSibTest_N1L_Task;
    default _NextState = UCk_SmNetSibTest_N1A_One;
    default _NextCondition = UCk_SmNetSibTest_InputAtLeast5;
}

UCLASS()
class UCk_SmNetSibTest_N1A_Task : UCk_SmTask_SubStateMachine
{
    default _InitialStateClass = UCk_SmNetSibTest_N1A_Host;
    default _CompletionBehavior = ECk_SmTask_SubSm_CompletionBehavior::KeepRunning;
}

UCLASS()
class UCk_SmNetSibTest_N1B_Two : UCk_SmNetSibTest_Step
{
}

UCLASS()
class UCk_SmNetSibTest_N1B_One : UCk_SmNetSibTest_Step
{
    default _NextState = UCk_SmNetSibTest_N1B_Two;
    default _NextCondition = UCk_SmNetSibTest_InputAtLeast3;
}

UCLASS()
class UCk_SmNetSibTest_N1B_Idle : UCk_SmNetSibTest_Step
{
    default _NextState = UCk_SmNetSibTest_N1B_One;
    default _NextCondition = UCk_SmNetSibTest_InputAtLeast1;
}

UCLASS()
class UCk_SmNetSibTest_N1B_Task : UCk_SmTask_SubStateMachine
{
    default _InitialStateClass = UCk_SmNetSibTest_N1B_Idle;
    default _CompletionBehavior = ECk_SmTask_SubSm_CompletionBehavior::KeepRunning;
}

UCLASS()
class UCk_SmNetSibTest_N1_Hold : UCk_SmNetSibTest_Step
{
    default _FirstTask = UCk_SmNetSibTest_N1A_Task;
    default _SecondTask = UCk_SmNetSibTest_N1B_Task;
}

// ============================================================================
// N2
// ============================================================================

UCLASS()
class UCk_SmNetSibTest_N2A_Two : UCk_SmNetSibTest_Step
{
}

UCLASS()
class UCk_SmNetSibTest_N2A_One : UCk_SmNetSibTest_Step
{
    default _NextState = UCk_SmNetSibTest_N2A_Two;
    default _NextCondition = UCk_SmNetSibTest_InputAtLeast4;
}

UCLASS()
class UCk_SmNetSibTest_N2A_Idle : UCk_SmNetSibTest_Step
{
    default _NextState = UCk_SmNetSibTest_N2A_One;
    default _NextCondition = UCk_SmNetSibTest_InputAtLeast2;
}

UCLASS()
class UCk_SmNetSibTest_N2A_Task : UCk_SmTask_SubStateMachine
{
    default _InitialStateClass = UCk_SmNetSibTest_N2A_Idle;
    default _CompletionBehavior = ECk_SmTask_SubSm_CompletionBehavior::KeepRunning;
}

UCLASS()
class UCk_SmNetSibTest_N2B_Two : UCk_SmNetSibTest_Step
{
}

UCLASS()
class UCk_SmNetSibTest_N2B_One : UCk_SmNetSibTest_Step
{
    default _NextState = UCk_SmNetSibTest_N2B_Two;
    default _NextCondition = UCk_SmNetSibTest_InputAtLeast5;
}

UCLASS()
class UCk_SmNetSibTest_N2B_Idle : UCk_SmNetSibTest_Step
{
    default _NextState = UCk_SmNetSibTest_N2B_One;
    default _NextCondition = UCk_SmNetSibTest_InputAtLeast3;
}

UCLASS()
class UCk_SmNetSibTest_N2B_Task : UCk_SmTask_SubStateMachine
{
    default _InitialStateClass = UCk_SmNetSibTest_N2B_Idle;
    default _CompletionBehavior = ECk_SmTask_SubSm_CompletionBehavior::KeepRunning;
}

UCLASS()
class UCk_SmNetSibTest_N2_Hold : UCk_SmNetSibTest_Step
{
    default _FirstTask = UCk_SmNetSibTest_N2A_Task;
    default _SecondTask = UCk_SmNetSibTest_N2B_Task;
}

UCLASS()
class UCk_SmNetSibTest_N2_Wait : UCk_SmNetSibTest_Step
{
    default _NextState = UCk_SmNetSibTest_N2_Hold;
    default _NextCondition = UCk_SmNetSibTest_InputAtLeast1;
}

// ============================================================================
// N3
// ============================================================================

UCLASS()
class UCk_SmNetSibTest_N3C_Two : UCk_SmNetSibTest_Step
{
}

UCLASS()
class UCk_SmNetSibTest_N3C_One : UCk_SmNetSibTest_Step
{
    default _NextState = UCk_SmNetSibTest_N3C_Two;
    default _NextCondition = UCk_SmNetSibTest_InputAtLeast4;
}

UCLASS()
class UCk_SmNetSibTest_N3C_Idle : UCk_SmNetSibTest_Step
{
    default _NextState = UCk_SmNetSibTest_N3C_One;
    default _NextCondition = UCk_SmNetSibTest_InputAtLeast2;
}

UCLASS()
class UCk_SmNetSibTest_N3C_Task : UCk_SmTask_SubStateMachine
{
    default _InitialStateClass = UCk_SmNetSibTest_N3C_Idle;
    default _CompletionBehavior = ECk_SmTask_SubSm_CompletionBehavior::KeepRunning;
}

UCLASS()
class UCk_SmNetSibTest_N3D_Two : UCk_SmNetSibTest_Step
{
}

UCLASS()
class UCk_SmNetSibTest_N3D_One : UCk_SmNetSibTest_Step
{
    default _NextState = UCk_SmNetSibTest_N3D_Two;
    default _NextCondition = UCk_SmNetSibTest_InputAtLeast5;
}

UCLASS()
class UCk_SmNetSibTest_N3D_Idle : UCk_SmNetSibTest_Step
{
    default _NextState = UCk_SmNetSibTest_N3D_One;
    default _NextCondition = UCk_SmNetSibTest_InputAtLeast3;
}

UCLASS()
class UCk_SmNetSibTest_N3D_Task : UCk_SmTask_SubStateMachine
{
    default _InitialStateClass = UCk_SmNetSibTest_N3D_Idle;
    default _CompletionBehavior = ECk_SmTask_SubSm_CompletionBehavior::KeepRunning;
}

UCLASS()
class UCk_SmNetSibTest_N3A_Host : UCk_SmNetSibTest_Step
{
    default _FirstTask = UCk_SmNetSibTest_N3C_Task;
    default _SecondTask = UCk_SmNetSibTest_N3D_Task;
}

UCLASS()
class UCk_SmNetSibTest_N3A_Task : UCk_SmTask_SubStateMachine
{
    default _InitialStateClass = UCk_SmNetSibTest_N3A_Host;
    default _CompletionBehavior = ECk_SmTask_SubSm_CompletionBehavior::KeepRunning;
}

UCLASS()
class UCk_SmNetSibTest_N3B_Two : UCk_SmNetSibTest_Step
{
}

UCLASS()
class UCk_SmNetSibTest_N3B_One : UCk_SmNetSibTest_Step
{
    default _NextState = UCk_SmNetSibTest_N3B_Two;
    default _NextCondition = UCk_SmNetSibTest_InputAtLeast6;
}

UCLASS()
class UCk_SmNetSibTest_N3B_Idle : UCk_SmNetSibTest_Step
{
    default _NextState = UCk_SmNetSibTest_N3B_One;
    default _NextCondition = UCk_SmNetSibTest_InputAtLeast1;
}

UCLASS()
class UCk_SmNetSibTest_N3B_Task : UCk_SmTask_SubStateMachine
{
    default _InitialStateClass = UCk_SmNetSibTest_N3B_Idle;
    default _CompletionBehavior = ECk_SmTask_SubSm_CompletionBehavior::KeepRunning;
}

UCLASS()
class UCk_SmNetSibTest_N3_Hold : UCk_SmNetSibTest_Step
{
    default _FirstTask = UCk_SmNetSibTest_N3A_Task;
    default _SecondTask = UCk_SmNetSibTest_N3B_Task;
}

// ============================================================================
// N4
// ============================================================================

UCLASS()
class UCk_SmNetSibTest_N4A_Two : UCk_SmNetSibTest_Step
{
}

UCLASS()
class UCk_SmNetSibTest_N4A_One : UCk_SmNetSibTest_Step
{
    default _NextState = UCk_SmNetSibTest_N4A_Two;
    default _NextCondition = UCk_SmNetSibTest_InputIs2;
}

UCLASS()
class UCk_SmNetSibTest_N4A_Idle : UCk_SmNetSibTest_Step
{
    default _NextState = UCk_SmNetSibTest_N4A_One;
    default _NextCondition = UCk_SmNetSibTest_InputIs1Or6;
}

UCLASS()
class UCk_SmNetSibTest_N4A_Task : UCk_SmTask_SubStateMachine
{
    default _InitialStateClass = UCk_SmNetSibTest_N4A_Idle;
    default _CompletionBehavior = ECk_SmTask_SubSm_CompletionBehavior::KeepRunning;
}

UCLASS()
class UCk_SmNetSibTest_N4B_One : UCk_SmNetSibTest_Step
{
}

UCLASS()
class UCk_SmNetSibTest_N4B_Idle : UCk_SmNetSibTest_Step
{
    default _NextState = UCk_SmNetSibTest_N4B_One;
    default _NextCondition = UCk_SmNetSibTest_InputIs5;
}

UCLASS()
class UCk_SmNetSibTest_N4B_Task : UCk_SmTask_SubStateMachine
{
    default _InitialStateClass = UCk_SmNetSibTest_N4B_Idle;
    default _CompletionBehavior = ECk_SmTask_SubSm_CompletionBehavior::KeepRunning;
}

// Leads back to N4_Hold; the transition is added in DoDefineState rather than as a default so the cycle needs no
// forward class reference in a default.
UCLASS()
class UCk_SmNetSibTest_N4_Away : UCk_SmState_EntityScript
{
    UFUNCTION(BlueprintOverride)
    void DoDefineState(FCk_Handle_SmState_UnderConstruction& InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();

        auto Transition = AddTransition(InHandle, UCk_SmNetSibTest_N4_Hold);
        AddCondition(Transition, UCk_SmNetSibTest_InputIs4);
    }
}

UCLASS()
class UCk_SmNetSibTest_N4_Hold : UCk_SmNetSibTest_Step
{
    default _FirstTask = UCk_SmNetSibTest_N4A_Task;
    default _SecondTask = UCk_SmNetSibTest_N4B_Task;
    default _NextState = UCk_SmNetSibTest_N4_Away;
    default _NextCondition = UCk_SmNetSibTest_InputIs3;
}

// ============================================================================
// Replay check fixture (Ck.StateMachine.SubSmAddress.ReplayChecksWhereItStands)
// ============================================================================

// Stands for N3B_Idle when installed as an override; never installed by the tests. An event naming it as its previous
// state is what an authority sends after overriding N3B_Idle, since overrides never ride the wire.
UCLASS()
class UCk_SmNetSibTest_N3B_IdleOverride : UCk_SmNetSibTest_Step
{
    UFUNCTION(BlueprintOverride)
    TArray<FGameplayTag> DoGet_StatesToOverride() const
    {
        auto Tags = TArray<FGameplayTag>();
        Tags.Add(UCk_SmState_EntityScript::Get_StateTagForClass(UCk_SmNetSibTest_N3B_Idle));
        return Tags;
    }
}
