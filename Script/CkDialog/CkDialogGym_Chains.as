// Language=angelscript
class UCk_EntityScript_DialogGym_Chains : UCk_EntityScript_DialogGym_Source
{
    private FCk_Handle_DialogEmitter _Emitter;
    private FGameplayTag _EnterA;
    private FName _CurrentLineID;
    private FString _CurrentText;
    private TArray<FCkDialogGym_LineResult> _Results;
    private bool _HasCurrent = false;
    private int32 _Step = 0;
    private int64 _QueryCount = 0;
    protected bool PrepareStation() override
    {
        _EnterA = utils_gameplay_tag::ResolveGameplayTag(n"DialogGym.Chains.A");
        if (!_EnterA.IsValid()) { return false; }
        _Emitter = UCk_Utils_DialogEmitter_UE::Add(_Root, FCk_DialogEmitter_Spec(FGameplayTagContainer()));
        if (!IsEmitterLive(_Emitter)) { return false; }
        _Emitter.BindTo_OnQueryCompleted(FCk_Delegate_DialogEmitter_OnQueryCompleted(this, n"OnResult"));
        _Cadence = CreateCadence(4.0f, n"OnRestartTick");
        return IsTimerLive(_Cadence);
    }
    protected bool Get_StationIsHealthy() const override
    { return _EnterA.IsValid() && IsEmitterLive(_Emitter) && FCk_Handle(_Emitter) == _Root; }
    UFUNCTION()
    private void OnRestartTick(FCk_Handle_Timer Timer, FCk_Chrono Chrono, FCk_Time Dt)
    {
        if (!IsRunningCallback(Timer)) { if (_Started && !_Closed) { Request_Block(); } return; }
        if (_QueryCount >= 2147483647) { Request_Block(); return; }
        _Step = 0;
        _QueryCount++;
        if (_Emitter.Request_Query(FCk_Request_DialogEmitter_Query(_EnterA)) != _Emitter)
        { Request_Block(); }
    }
    UFUNCTION()
    private void OnResult(FCk_Handle_DialogEmitter Emitter, FCk_DialogEmitter_QueryResult Result)
    {
        if (!IsRunningResult(Emitter) || Emitter != _Emitter) { if (_Started && !_Closed) { Request_Block(); } return; }
        auto Entries = Result.Get_Entries();
        if (Entries.Num() != 1 || _Step >= 3) { Request_Block(); return; }
        auto Line = Entries[0].Get_Line();
        auto ExpectedID = _Step == 0 ? n"DialogGym.Chains.A" :
            _Step == 1 ? n"DialogGym.Chains.B" : n"DialogGym.Chains.C";
        if (!UCk_DialogGym_Runtime_UE::Get_IsLiveLine(FCk_Handle(Line), _World) ||
            Line != _Bank.Get_Line(ExpectedID) ||
            Line.Get_LineID() != ExpectedID ||
            Entries[0].Get_LineID() != ExpectedID ||
            Entries[0].Get_Result() != ECk_DialogLine_QueryResult::Passed)
        { Request_Block(); return; }
        FCkDialogGym_LineResult Value;
        Value.LineID = ExpectedID;
        Value.Text = Entries[0].Get_Text().ToString();
        Value.Result = int32(Entries[0].Get_Result());
        _Results.Reset();
        _Results.Add(Value);
        _CurrentLineID = Value.LineID;
        _CurrentText = Value.Text;
        _HasCurrent = true;
        _Step++;
        if (_Step < 3 && _Emitter.Request_QueryFollowUp(Line) != _Emitter)
        { Request_Block(); }
    }
    protected bool FillStationFrame(FCkDialogGym_StationFrame&out Frame) const override
    {
        Frame.Title = "DIALOG EXIT CHAINS";
        Frame.Footer = "A -> B -> C, restarts every 4s";
        Frame.QueryCount = _QueryCount;
        Frame.ChainStep = _Step;
        Frame.ActiveLineID = _CurrentLineID;
        Frame.Results = _Results;
        Frame.Detail = f"Step {_Step} of 3\n\n";
        if (_HasCurrent)
        { Frame.Detail += f"[{_CurrentLineID.ToString()}]\n\"{_CurrentText}\""; }
        else { Frame.Detail += "(chain starting...)"; }
        return _Step >= 0 && _Step <= 3;
    }
}
