// Language=angelscript

// Verifies the deferred-apply queue's LOUD timeout: a setting bound to a CVar that never
// registers must fire an ensure naming the key once the (shortened) timeout elapses, and the
// stored value must survive - timeout drops the queue entry, never the value.
class UCk_AutoTest_GameSettings_DeferredCVarTimesOutLoudly : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 10.0;

    private UCk_GameSettings_TimeoutSpecFixture_UE _Fixture;
    private UCk_GameSettings_Subsystem_UE _Settings;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        Set_CVarForTest(n"ck.GameSettings.DeferredApplyTimeoutSecs", "1");
        if (IsFinished()) { return; }

        _Fixture = Cast<UCk_GameSettings_TimeoutSpecFixture_UE>(
            NewObject(this, UCk_GameSettings_TimeoutSpecFixture_UE));
        if (_Fixture == nullptr)
        {
            FinishFailure("the isolated settings fixture could not be created");
            return;
        }
        _Settings = _Fixture.Request_Start();
        if (_Settings == nullptr)
        {
            FinishFailure("the isolated settings fixture could not start its registry and ticker");
            return;
        }

        auto Definition = FCk_GameSettings_SettingDefinition(n"astest.deferred.key", ECk_GameSettings_ValueType::Float, "0.25");
        Definition.Set_ApplyBindingType(ECk_GameSettings_ApplyBindingType::CVar);
        Definition.Set_CVar(utils_c_var::Make_CVarRef(n"ck.astest.nonexistent.cvar", ECk_CVarType::Float));

        Assert_True(_Settings.Request_RegisterSetting(Definition),
            "CVar-bound setting registers while its CVar is missing");

        Assert_True(_Settings.Request_SetSettingValue_Float(
            FCk_Request_GameSettings_SetValue_Float(n"astest.deferred.key", 0.75)),
            "set holds with the CVar missing (value goes to the deferred queue)");

        Assert_True(_Settings.Get_HasPendingCVarApply(n"astest.deferred.key"),
            "missing CVar leaves an apply pending before the timeout");
        WaitUntil(n"Check_TimeoutCompleted", n"OnTimeoutCompleted", 100000);
    }

    UFUNCTION()
    private void Check_TimeoutCompleted(FCk_Handle InHandle, FCk_SharedBool OutResult, FInstancedStruct InPayload)
    {
        if (IsFinished()) { return; }
        auto Result = OutResult;
        Result.Set(_Settings.Get_HasPendingCVarApply(n"astest.deferred.key") == false);
    }

    UFUNCTION()
    private void OnTimeoutCompleted(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        if (IsFinished()) { return; }
        Assert_True(_Settings.Get_HasPendingCVarApply(n"astest.deferred.key") == false,
            "the production ticker removed the timed-out apply");
        Assert_Equals_Float(_Settings.Get_SettingValue_Float(n"astest.deferred.key", -1.0), 0.75, 0.001,
            "the value survives the deferred-apply timeout");

        auto Provider = _Settings.Get_StorageProvider();
        auto StoredValues = TArray<FCk_GameSettings_StoredValue>();
        Assert_True(Provider.Get_StoredValues(ECk_GameSettings_Scope::Machine, 0, StoredValues)
            == ECk_GameSettings_StorageReadResult::Succeeded, "provider reads the retained machine values");
        auto FoundInStore = false;
        for (int32 Index = 0; Index < StoredValues.Num(); ++Index)
        {
            if (StoredValues[Index].Get_Key() == n"astest.deferred.key")
            {
                FoundInStore = true;
                break;
            }
        }
        Assert_True(FoundInStore, "the stored value is retained after the timeout, not deleted");

        FinishSuccess();
    }

    void FinishSuccess() override
    {
        if (IsFinished()) { return; }
        Assert_True(DoReleaseFixture(), "the isolated settings ticker and registry are released before CVar restoration");
        Super::FinishSuccess();
    }

    void FinishFailure(const FString& InMessage) override
    {
        if (IsFinished()) { return; }
        const auto Released = DoReleaseFixture();
        Super::FinishFailure(Released ? InMessage : InMessage + "; isolated settings cleanup failed");
    }

    UFUNCTION(BlueprintOverride)
    void DoEndPlay(FCk_Handle InHandle)
    {
        if (IsFinished() == false)
        {
            FinishFailure("the deferred settings timeout test ended before completion");
            return;
        }
        DoReleaseFixture();
    }

    private bool DoReleaseFixture()
    {
        _Settings = nullptr;
        if (_Fixture == nullptr) { return true; }
        const auto Released = _Fixture.Request_Release();
        if (Released) { _Fixture = nullptr; }
        return Released;
    }
}

class ACk_AutoTest_GameSettings_DeferredCVarTimesOutLoudly_Actor : ACk_AutoTestRunner
{
    default _TestEntityScriptClass = UCk_AutoTest_GameSettings_DeferredCVarTimesOutLoudly;

    // The editor's short mirror is a separate error from the original diagnostic.
    UFUNCTION(BlueprintOverride)
    TArray<FString> Get_ExpectedLogErrorsExact() const
    {
        TArray<FString> Out;
        Out.Add("[Server] ! TimedOut\nGameSettings deferred apply for key [astest.deferred.key] timed out, CVar [ck.astest.nonexistent.cvar] never registered within [1] seconds. The stored value is retained and will apply next boot.");
        return Out;
    }

    // A missing timeout is a failure even if the stored value remains intact.
    UFUNCTION(BlueprintOverride)
    TArray<FString> Get_RequiredLogErrors() const
    {
        TArray<FString> Out;
        // The editor mirrors the same ensure into CkEnsures without a callstack. Match only
        // the original diagnostic so one timeout has one required automation occurrence.
        Out.Add("GameSettings deferred apply for key [astest.deferred.key] timed out, CVar [ck.astest.nonexistent.cvar] never registered within [1] seconds. The stored value is retained and will apply next boot.\n\n == BP CallStack ==");
        return Out;
    }
}
