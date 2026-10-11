// Language=angelscript
class UCk_EntityScript_DialogGym_Cooldowns : UCk_EntityScript_DialogGym_Source
{
    private FCk_Handle_DialogEmitter _Emitter;
    private FCk_Handle_DialogEmitter _OtherEmitter;
    private FCk_Handle _OtherChild;
    private FCk_Handle_DialogLine _Line;
    private FGameplayTag _EventTag;
    private TArray<FCkDialogGym_LineResult> _Results;
    private TArray<FCkDialogGym_LineResult> _OtherResults;
    private int64 _QueryCount = 0;
    private bool _HasState = false;
    protected bool PrepareStation() override
    {
        _EventTag = utils_gameplay_tag::ResolveGameplayTag(n"DialogGym.Cooldowns.Enter");
        _Line = _Bank.Get_Line(n"DialogGym.Cooldowns.Line");
        if (!_EventTag.IsValid() || !UCk_DialogGym_Runtime_UE::Get_IsLiveLine(FCk_Handle(_Line), _World))
        { return false; }
        _Emitter = UCk_Utils_DialogEmitter_UE::Add(_Root, FCk_DialogEmitter_Spec(FGameplayTagContainer()));
        if (!IsEmitterLive(_Emitter)) { return false; }
        _OtherChild = utils_entity_lifetime::Request_CreateEntity(_Root);
        if (!IsDirectChild(_OtherChild, _Root)) { return false; }
        _OtherEmitter = UCk_Utils_DialogEmitter_UE::Add(_OtherChild,
            FCk_DialogEmitter_Spec(FGameplayTagContainer()));
        if (!IsEmitterLive(_OtherEmitter)) { return false; }
        _Emitter.BindTo_OnQueryCompleted(FCk_Delegate_DialogEmitter_OnQueryCompleted(this, n"OnResult"));
        _OtherEmitter.BindTo_OnQueryCompleted(FCk_Delegate_DialogEmitter_OnQueryCompleted(this, n"OnOtherResult"));
        _Cadence = CreateCadence(1.0f, n"OnQueryTick");
        return IsTimerLive(_Cadence);
    }
    protected bool Get_StationIsHealthy() const override
    {
        return _EventTag.IsValid() &&
            UCk_DialogGym_Runtime_UE::Get_IsLiveLine(FCk_Handle(_Line), _World) &&
            IsEmitterLive(_Emitter) && FCk_Handle(_Emitter) == _Root &&
            IsEmitterLive(_OtherEmitter) &&
            FCk_Handle(_OtherEmitter) == _OtherChild &&
            IsDirectChild(_OtherChild, _Root);
    }
    UFUNCTION()
    private void OnQueryTick(FCk_Handle_Timer Timer, FCk_Chrono Chrono, FCk_Time Dt)
    {
        if (!IsRunningCallback(Timer)) { if (_Started && !_Closed) { Request_Block(); } return; }
        if (_QueryCount >= 2147483647) { Request_Block(); return; }
        _QueryCount++;
        if (_Emitter.Request_Query(FCk_Request_DialogEmitter_Query(_EventTag)) != _Emitter ||
            _OtherEmitter.Request_Query(FCk_Request_DialogEmitter_Query(_EventTag)) != _OtherEmitter)
        { Request_Block(); }
    }
    private bool Capture(FCk_DialogEmitter_QueryResult Result, TArray<FCkDialogGym_LineResult>&out Out)
    {
        auto Entries = Result.Get_Entries();
        if (Entries.Num() != 1) { return false; }
        auto Line = Entries[0].Get_Line();
        if (!UCk_DialogGym_Runtime_UE::Get_IsLiveLine(FCk_Handle(Line), _World) ||
            Line != _Line || Line.Get_LineID() != n"DialogGym.Cooldowns.Line" ||
            _Bank.Get_Line(n"DialogGym.Cooldowns.Line") != Line ||
            Line.Get_EventTag() != _EventTag ||
            Entries[0].Get_LineID() != n"DialogGym.Cooldowns.Line")
        { return false; }
        FCkDialogGym_LineResult Value;
        Value.LineID = n"DialogGym.Cooldowns.Line";
        Value.Text = Entries[0].Get_Text().ToString();
        Value.Result = int32(Entries[0].Get_Result());
        Out.Reset();
        Out.Add(Value);
        return true;
    }
    UFUNCTION()
    private void OnResult(FCk_Handle_DialogEmitter Emitter, FCk_DialogEmitter_QueryResult Result)
    {
        if (!IsRunningResult(Emitter) || Emitter != _Emitter) { if (_Started && !_Closed) { Request_Block(); } return; }
        if (!Capture(Result, _Results)) { Request_Block(); return; }
        _HasState = true;
        if (_Results[0].Result == int32(ECk_DialogLine_QueryResult::Passed) &&
            _Emitter.Request_StartCooldown(
                FCk_Request_DialogEmitter_StartCooldown(_Line, FCk_Time(3.0))) != _Emitter)
        { Request_Block(); }
    }
    UFUNCTION()
    private void OnOtherResult(FCk_Handle_DialogEmitter Emitter, FCk_DialogEmitter_QueryResult Result)
    {
        if (!IsRunningResult(Emitter) || Emitter != _OtherEmitter) { if (_Started && !_Closed) { Request_Block(); } return; }
        if (!Capture(Result, _OtherResults)) { Request_Block(); }
    }
    protected bool FillStationFrame(FCkDialogGym_StationFrame&out Frame) const override
    {
        auto Remaining = _Emitter.Get_CooldownRemaining(_Line).Get_Seconds();
        if (!Math::IsFinite(Remaining) || Remaining < 0.0 || Remaining > 3.1)
        { return false; }
        Frame.Title = "DIALOG COOLDOWNS";
        Frame.Footer = "Query 1s / cooldown 3s";
        Frame.QueryCount = _QueryCount;
        Frame.Results = _Results;
        Frame.OtherResults = _OtherResults;
        Frame.CooldownRemaining = float32(Remaining);
        if (_HasState)
        {
            auto Mine = _Results[0].Result == int32(ECk_DialogLine_QueryResult::Fail_EmitterCondition)
                ? "FAIL(cooldown)" : _Results[0].Result == int32(ECk_DialogLine_QueryResult::Fail_LineCondition)
                ? "FAIL(line)" : "Passed";
            auto Other = _OtherResults.Num() == 1 &&
                _OtherResults[0].Result == int32(ECk_DialogLine_QueryResult::Fail_EmitterCondition)
                ? "FAIL(cooldown)" : "Passed";
            Frame.Detail = f"Emitter A: {Mine}   (cooldown {Remaining}s)\n" +
                f"Emitter B: {Other}   (never cooled)\n";
        }
        else { Frame.Detail = "(waiting for first query...)"; }
        return true;
    }
}
