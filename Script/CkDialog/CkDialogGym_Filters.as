// Language=angelscript
class UCk_EntityScript_DialogGym_Filters : UCk_EntityScript_DialogGym_Source
{
    private FCk_Handle_DialogEmitter _TownieEmitter;
    private FCk_Handle_DialogEmitter _NamedEmitter;
    private FCk_Handle _NamedChild;
    private FGameplayTag _EventTag;
    private TArray<FCkDialogGym_LineResult> _TownieResults;
    private TArray<FCkDialogGym_LineResult> _NamedResults;
    private int64 _QueryCount = 0;
    protected bool PrepareStation() override
    {
        _EventTag = utils_gameplay_tag::ResolveGameplayTag(n"DialogGym.Filters.Enter");
        auto TownieTag = utils_gameplay_tag::ResolveGameplayTag(n"DialogGym.Filters.Townie");
        auto NamedTag = utils_gameplay_tag::ResolveGameplayTag(n"DialogGym.Filters.NamedNpc");
        if (!_EventTag.IsValid() || !TownieTag.IsValid() || !NamedTag.IsValid()) { return false; }
        FGameplayTagContainer TownieTags;
        TownieTags.AddTag(TownieTag);
        _TownieEmitter = UCk_Utils_DialogEmitter_UE::Add(_Root, FCk_DialogEmitter_Spec(TownieTags));
        if (!IsEmitterLive(_TownieEmitter)) { return false; }
        _NamedChild = utils_entity_lifetime::Request_CreateEntity(_Root);
        if (!IsDirectChild(_NamedChild, _Root)) { return false; }
        FGameplayTagContainer NamedTags;
        NamedTags.AddTag(NamedTag);
        _NamedEmitter = UCk_Utils_DialogEmitter_UE::Add(_NamedChild, FCk_DialogEmitter_Spec(NamedTags));
        if (!IsEmitterLive(_NamedEmitter)) { return false; }
        _TownieEmitter.BindTo_OnQueryCompleted(FCk_Delegate_DialogEmitter_OnQueryCompleted(this, n"OnTownieResult"));
        _NamedEmitter.BindTo_OnQueryCompleted(FCk_Delegate_DialogEmitter_OnQueryCompleted(this, n"OnNamedResult"));
        _Cadence = CreateCadence(2.0f, n"OnQueryTick");
        return IsTimerLive(_Cadence);
    }
    protected bool Get_StationIsHealthy() const override
    {
        return _EventTag.IsValid() && IsEmitterLive(_TownieEmitter) &&
            FCk_Handle(_TownieEmitter) == _Root &&
            IsEmitterLive(_NamedEmitter) &&
            FCk_Handle(_NamedEmitter) == _NamedChild &&
            IsDirectChild(_NamedChild, _Root);
    }
    UFUNCTION()
    private void OnQueryTick(FCk_Handle_Timer Timer, FCk_Chrono Chrono, FCk_Time Dt)
    {
        if (!IsRunningCallback(Timer)) { if (_Started && !_Closed) { Request_Block(); } return; }
        if (_QueryCount >= 2147483647) { Request_Block(); return; }
        _QueryCount++;
        if (_TownieEmitter.Request_Query(FCk_Request_DialogEmitter_Query(_EventTag)) != _TownieEmitter ||
            _NamedEmitter.Request_Query(FCk_Request_DialogEmitter_Query(_EventTag)) != _NamedEmitter)
        { Request_Block(); }
    }
    private bool Capture(FCk_DialogEmitter_QueryResult Result, bool Townie,
        TArray<FCkDialogGym_LineResult>&out Out)
    {
        auto Entries = Result.Get_Entries();
        if (Entries.Num() != 2) { return false; }
        TArray<FCkDialogGym_LineResult> Next;
        for (auto Item : Entries)
        {
            auto Line = Item.Get_Line();
            if (!UCk_DialogGym_Runtime_UE::Get_IsLiveLine(FCk_Handle(Line), _World))
            { return false; }
            auto ID = Line.Get_LineID();
            if ((ID != n"DialogGym.Filters.GlobalLine" &&
                 ID != (Townie ? n"DialogGym.Filters.TownieLine" :
                     n"DialogGym.Filters.NamedLine")) ||
                _Bank.Get_Line(ID) != Line || Line.Get_EventTag() != _EventTag ||
                Item.Get_LineID() != ID)
            { return false; }
            for (auto Previous : Next)
            { if (Previous.LineID == ID) { return false; } }
            FCkDialogGym_LineResult Value;
            Value.LineID = ID;
            Value.Text = Item.Get_Text().ToString();
            Value.Result = int32(Item.Get_Result());
            Next.Add(Value);
        }
        Out = Next;
        return true;
    }
    UFUNCTION()
    private void OnTownieResult(FCk_Handle_DialogEmitter Emitter, FCk_DialogEmitter_QueryResult Result)
    {
        if (!IsRunningResult(Emitter) || Emitter != _TownieEmitter) { if (_Started && !_Closed) { Request_Block(); } return; }
        if (!Capture(Result, true, _TownieResults)) { Request_Block(); }
    }
    UFUNCTION()
    private void OnNamedResult(FCk_Handle_DialogEmitter Emitter, FCk_DialogEmitter_QueryResult Result)
    {
        if (!IsRunningResult(Emitter) || Emitter != _NamedEmitter) { if (_Started && !_Closed) { Request_Block(); } return; }
        if (!Capture(Result, false, _NamedResults)) { Request_Block(); }
    }
    protected bool FillStationFrame(FCkDialogGym_StationFrame&out Frame) const override
    {
        Frame.Title = "DIALOG TAG FILTERS";
        Frame.Footer = "Same event, different visible lines";
        Frame.QueryCount = _QueryCount;
        Frame.Results = _TownieResults;
        Frame.OtherResults = _NamedResults;
        Frame.Detail = f"Townie emitter sees: {_TownieResults.Num()} line(s)\n" +
            f"NamedNpc emitter sees: {_NamedResults.Num()} line(s)\n\n" +
            "(each sees its own tagged line + the global line = 2)";
        return true;
    }
}
