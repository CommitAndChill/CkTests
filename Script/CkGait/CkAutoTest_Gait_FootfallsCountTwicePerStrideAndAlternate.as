// Language=angelscript
// Flying at the reference speed (the fixture's "grounded, moving"): the gait counts a footfall at every dip bottom, so
// over ~2 s at 1.6 strides/s the count climbs by about 3.2 per second, one per frame-step at most, and the side flips
// Left, Right, Left, ... on every increment.
class UCk_AutoTest_Gait_FootfallsCountTwicePerStrideAndAlternate : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 10.0f;
    private FCk_GaitAutoTestFixture _F;
    private FCk_Handle _Owner;

    private int32 _SeenCount = 0;
    private TArray<ECk_Gait_Side> _Sides;
    private bool _SawMultiStep = false;
    private float32 _MovingSeconds = 0.0f;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        Add_Step("spawn the test character", n"Step_Spawn");
        Add_Step_WaitUntil("character entity constructed", n"Check_EntityReady");
        Add_Step("add a gait and fly at the reference speed", n"Step_Fly");
        Add_Step_WaitUntil("six footfalls observed", n"Check_SixFootfalls");
        Add_Step("assert the sides alternate and the rate", n"Step_Verify");
        Run_Steps(InHandle);
    }

    UFUNCTION()
    private void Step_Spawn(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _Owner = InHandle;
        _F.Spawn(this, FVector(6000.0, 30000.0, -40000.0));
        if (ck::Is_NOT_Valid(_F.Character))
        {
            return;
        }

        utils_pending_entity_script::Promise_OnConstructed(_F.Character.PendingEntity,
            FCk_Delegate_EntityScript_Constructed(this, n"OnEntityReady"));
    }

    UFUNCTION()
    private void OnEntityReady(FCk_Handle_EntityScript InEntityScript)
    {
        if (IsFinished())
        {
            return;
        }

        _F.Ready(_Owner, InEntityScript);
    }

    UFUNCTION()
    private void Check_EntityReady(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        Res.Set(ck::IsValid(_F.Entity) && ck::IsValid(_F.Root));
    }

    UFUNCTION()
    private void Step_Fly(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        _F.AddGait(FCk_Gait_Spec());
        Assert_Valid(_F.Gait, "gait added to the character's entity");
        Assert_Equals_Int(utils_gait::Get_FootfallCount(_F.Gait), 0, "no footfall before moving");
        _F.Character.Fly(FVector(420.0, 0.0, 0.0));
        utils_timer::Create_Tick(_Owner, FCk_Delegate_Timer(this, n"OnTick"));
    }

    UFUNCTION()
    private void OnTick(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished() || ck::Is_NOT_Valid(_F.Gait))
        {
            return;
        }

        _MovingSeconds += float32(InDeltaT.Get_Seconds());

        const auto Count = utils_gait::Get_FootfallCount(_F.Gait);
        if (Count == _SeenCount)
        {
            return;
        }

        if (Count - _SeenCount > 1)
        {
            _SawMultiStep = true;
        }

        _SeenCount = Count;
        _Sides.Add(utils_gait::Get_LastFootfallSide(_F.Gait));
    }

    UFUNCTION()
    private void Check_SixFootfalls(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        auto Res = OutResult;
        Res.Set(_Sides.Num() >= 6);
    }

    UFUNCTION()
    private void Step_Verify(FCk_Handle InHandle, FInstancedStruct InPayload)
    {
        Assert_False(_SawMultiStep, "at 60 fps a frame advances the phase by far less than pi: one footfall per increment");

        for (int32 Index = 1; Index < _Sides.Num(); ++Index)
        {
            Assert_True(_Sides[Index] != _Sides[Index - 1], f"footfall {Index} switches feet");
        }

        // 1.6 strides/s at the reference speed = 3.2 footfalls/s; six of them need ~1.9 s. Bounds are loose on purpose:
        // the first footfall lands anywhere in the first half cycle.
        Assert_True(_MovingSeconds > 1.2f && _MovingSeconds < 3.5f,
            f"six footfalls took {_MovingSeconds} s, expected about 1.9 s at 3.2 footfalls/s");
        FinishSuccess();
    }
}

class ACk_AutoTest_Gait_FootfallsCountTwicePerStrideAndAlternate_Actor : ACk_AutoTestRunner
{
    default _TestEntityScriptClass = UCk_AutoTest_Gait_FootfallsCountTwicePerStrideAndAlternate;
    default _TimeoutSeconds = 10.0f;
}
