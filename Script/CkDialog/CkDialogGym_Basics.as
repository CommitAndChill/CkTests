// Language=angelscript
class UCk_EntityScript_DialogGym_Basics : UCk_EntityScript_DialogGym_Source
{
    private FCk_Handle_DialogEmitter _Emitter;
    private FGameplayTag _EventTag;
    private TArray<FCkDialogGym_LineResult> _Results;
    private int64 _QueryCount = 0;
    private bool _HasResult = false;
    protected bool PrepareStation() override
    {
        _EventTag = utils_gameplay_tag::ResolveGameplayTag(n"DialogGym.Basics.Greeting");
        if (!_EventTag.IsValid()) { return false; }
        _Emitter = UCk_Utils_DialogEmitter_UE::Add(_Root, FCk_DialogEmitter_Spec(FGameplayTagContainer()));
        if (!IsEmitterLive(_Emitter)) { return false; }
        _Emitter.BindTo_OnQueryCompleted(FCk_Delegate_DialogEmitter_OnQueryCompleted(this, n"OnQueryCompleted"));
        _Cadence = CreateCadence(2.0f, n"OnQueryTick");
        return IsTimerLive(_Cadence);
    }
    protected bool Get_StationIsHealthy() const override
    { return IsEmitterLive(_Emitter) && FCk_Handle(_Emitter) == _Root && _EventTag.IsValid(); }
    UFUNCTION()
    private void OnQueryTick(FCk_Handle_Timer Timer, FCk_Chrono Chrono, FCk_Time Dt)
    {
        if (!IsRunningCallback(Timer)) { if (_Started && !_Closed) { Request_Block(); } return; }
        if (_QueryCount >= 2147483647) { Request_Block(); return; }
        _QueryCount++;
        if (_Emitter.Request_Query(FCk_Request_DialogEmitter_Query(_EventTag)) != _Emitter)
        { Request_Block(); }
    }
    UFUNCTION()
    private void OnQueryCompleted(FCk_Handle_DialogEmitter Emitter, FCk_DialogEmitter_QueryResult Result)
    {
        if (!IsRunningResult(Emitter) || Emitter != _Emitter) { if (_Started && !_Closed) { Request_Block(); } return; }
        auto Entries = Result.Get_Entries();
        if (Entries.Num() != 3) { Request_Block(); return; }
        TArray<FCkDialogGym_LineResult> Next;
        for (auto Item : Entries)
        {
            auto Line = Item.Get_Line();
            if (!UCk_DialogGym_Runtime_UE::Get_IsLiveLine(FCk_Handle(Line), _World))
            { Request_Block(); return; }
            auto ID = Line.Get_LineID();
            if ((ID != n"DialogGym.Basics.Hello" &&
                 ID != n"DialogGym.Basics.Weather" &&
                 ID != n"DialogGym.Basics.Gated") ||
                _Bank.Get_Line(ID) != Line || Line.Get_EventTag() != _EventTag ||
                Item.Get_LineID() != ID)
            { Request_Block(); return; }
            for (auto Previous : Next)
            { if (Previous.LineID == ID) { Request_Block(); return; } }
            FCkDialogGym_LineResult Value;
            Value.LineID = ID;
            Value.Text = Item.Get_Text().ToString();
            Value.Result = int32(Item.Get_Result());
            Next.Add(Value);
        }
        _Results = Next;
        _HasResult = true;
    }
    protected bool FillStationFrame(FCkDialogGym_StationFrame&out Frame) const override
    {
        Frame.Title = "DIALOG BASICS";
        Frame.Footer = "Query auto-cycles every 2s";
        Frame.QueryCount = _QueryCount;
        Frame.Results = _Results;
        auto Body = f"Queries fired: {_QueryCount}\n\n";
        if (_HasResult)
        {
            Body += f"Last result ({_Results.Num()} lines):\n";
            for (auto Entry : _Results)
            {
                auto State = Entry.Result == int32(ECk_DialogLine_QueryResult::Fail_LineCondition)
                    ? "FAIL(line)" : Entry.Result == int32(ECk_DialogLine_QueryResult::Fail_EmitterCondition)
                    ? "FAIL(cooldown)" : "Passed";
                Body += f"  [{State}] {Entry.LineID.ToString()}\n";
            }
        }
        else { Body += "(waiting for first query...)"; }
        Frame.Detail = Body;
        return true;
    }
}
