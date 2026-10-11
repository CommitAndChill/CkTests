// Language=angelscript
// Source-only dialog walk. The four node boxes and dashed edges are drawn from
// this value frame by each viewer's passive presentation.
class UCk_EntityScript_DialogGym_Graph : UCk_EntityScript_DialogGym_Source
{
    private FCk_Handle_DialogEmitter _Emitter;
    private FGameplayTag _TagStart;
    private FGameplayTag _TagChoice;
    private FGameplayTag _TagEnd;
    private FGameplayTag _CurrentEnter;
    private FName _ActiveLineID;
    private TArray<int32> _NodeStates;
    private TArray<FCkDialogGym_LineResult> _Results;
    private int64 _QueryCount = 0;
    protected bool PrepareStation() override
    {
        _TagStart = utils_gameplay_tag::ResolveGameplayTag(n"DialogGym.Graph.Start");
        _TagChoice = utils_gameplay_tag::ResolveGameplayTag(n"DialogGym.Graph.Choice");
        _TagEnd = utils_gameplay_tag::ResolveGameplayTag(n"DialogGym.Graph.End");
        if (!_TagStart.IsValid() || !_TagChoice.IsValid() || !_TagEnd.IsValid())
        { return false; }
        _CurrentEnter = _TagStart;
        _NodeStates.Add(0);
        _NodeStates.Add(0);
        _NodeStates.Add(0);
        _NodeStates.Add(0);
        _Emitter = UCk_Utils_DialogEmitter_UE::Add(_Root, FCk_DialogEmitter_Spec(FGameplayTagContainer()));
        if (!IsEmitterLive(_Emitter)) { return false; }
        _Emitter.BindTo_OnQueryCompleted(
            FCk_Delegate_DialogEmitter_OnQueryCompleted(this, n"OnQueryCompleted"));
        _Cadence = CreateCadence(2.5f, n"OnWalkTick");
        return IsTimerLive(_Cadence);
    }
    protected bool Get_StationIsHealthy() const override
    {
        return _TagStart.IsValid() && _TagChoice.IsValid() && _TagEnd.IsValid() &&
            _CurrentEnter.IsValid() && _NodeStates.Num() == 4 &&
            IsEmitterLive(_Emitter) && FCk_Handle(_Emitter) == _Root;
    }
    UFUNCTION()
    private void OnWalkTick(FCk_Handle_Timer Timer, FCk_Chrono Chrono, FCk_Time Dt)
    {
        if (!IsRunningCallback(Timer)) { if (_Started && !_Closed) { Request_Block(); } return; }
        if (_QueryCount >= 2147483647) { Request_Block(); return; }
        _QueryCount++;
        if (_Emitter.Request_Query(FCk_Request_DialogEmitter_Query(_CurrentEnter)) != _Emitter)
        { Request_Block(); }
    }
    private int32 NodeIndex(FName ID) const
    {
        if (ID == n"Graph.Greeting") { return 0; }
        if (ID == n"Graph.Friendly") { return 1; }
        if (ID == n"Graph.Hostile") { return 2; }
        if (ID == n"Graph.Farewell") { return 3; }
        return -1;
    }
    UFUNCTION()
    private void OnQueryCompleted(FCk_Handle_DialogEmitter Emitter,
        FCk_DialogEmitter_QueryResult Result)
    {
        if (!IsRunningResult(Emitter) || Emitter != _Emitter) { if (_Started && !_Closed) { Request_Block(); } return; }
        auto Entries = Result.Get_Entries();
        if (Entries.Num() < 1 || Entries.Num() > 2) { Request_Block(); return; }
        TArray<FCkDialogGym_LineResult> Next;
        TArray<int32> States = _NodeStates;
        FName Best;
        bool Found = false;
        for (auto Item : Entries)
        {
            auto Line = Item.Get_Line();
            if (!UCk_DialogGym_Runtime_UE::Get_IsLiveLine(FCk_Handle(Line), _World))
            { Request_Block(); return; }
            auto ID = Line.Get_LineID();
            auto Index = NodeIndex(ID);
            auto ResultCode = Item.Get_Result();
            if (Index < 0 ||
                _Bank.Get_Line(ID) != Line ||
                Line.Get_EventTag() != _CurrentEnter ||
                Item.Get_LineID() != ID ||
                (ResultCode != ECk_DialogLine_QueryResult::Passed &&
                 ResultCode != ECk_DialogLine_QueryResult::Fail_LineCondition &&
                 ResultCode != ECk_DialogLine_QueryResult::Fail_EmitterCondition))
            { Request_Block(); return; }
            for (auto Previous : Next)
            { if (Previous.LineID == ID) { Request_Block(); return; } }
            States[Index] = ResultCode == ECk_DialogLine_QueryResult::Passed ? 1 : 2;
            FCkDialogGym_LineResult Value;
            Value.LineID = ID;
            Value.Text = Item.Get_Text().ToString();
            Value.Result = int32(ResultCode);
            Next.Add(Value);
            if (!Found && ResultCode == ECk_DialogLine_QueryResult::Passed)
            { Best = ID; Found = true; }
        }
        if (!Found) { Request_Block(); return; }
        _NodeStates = States;
        _Results = Next;
        _ActiveLineID = Best;
        if (Best == n"Graph.Greeting") { _CurrentEnter = _TagChoice; }
        else if (Best == n"Graph.Friendly") { _CurrentEnter = _TagEnd; }
        else { _CurrentEnter = _TagStart; }
    }
    protected bool FillStationFrame(FCkDialogGym_StationFrame&out Frame) const override
    {
        if (_NodeStates.Num() != 4) { return false; }
        Frame.Title = "DIALOG GRAPH (LIVE)";
        Frame.Footer = "Green=passable, red=blocked, yellow=active";
        Frame.Detail = _ActiveLineID.IsNone()
            ? "Walk starting..." : f"Active: {_ActiveLineID.ToString()}";
        Frame.QueryCount = _QueryCount;
        Frame.ActiveLineID = _ActiveLineID;
        Frame.Results = _Results;
        Frame.GraphNodeStates = _NodeStates;
        return true;
    }
}
