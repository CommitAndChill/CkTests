// Language=angelscript

// The 14 gym lines belong to the world session, not to any rerunnable emitter.
class UCk_DialogGym_Bank : UObject
{
    private UObject _Context;
    private UCk_DialogRegistry_Subsystem_UE _Registry;
    private TArray<FCk_Handle_DialogLine> _Lines;
    private TArray<FName> _IDs;
    private bool _Attempted = false;
    private bool _Closed = false;

    private bool AddLine(FName ID, FName EnterName, FString Text,
        FName ExitName = NAME_None, FName FilterName = NAME_None,
        bool AlwaysFail = false)
    {
        if (_Closed || ck::Is_NOT_Valid(_Registry) || ck::Is_NOT_Valid(_Context))
        { return false; }
        auto Enter = utils_gameplay_tag::ResolveGameplayTag(EnterName);
        if (!Enter.IsValid()) { return false; }
        FGameplayTagContainer Filter;
        if (!FilterName.IsNone())
        {
            auto Tag = utils_gameplay_tag::ResolveGameplayTag(FilterName);
            if (!Tag.IsValid()) { return false; }
            Filter.AddTag(Tag);
        }
        auto Data = FCk_DialogBank_LineData(ID, Enter);
        Data.Set_Text(FText::FromString(Text));
        if (!ExitName.IsNone())
        {
            auto Exit = utils_gameplay_tag::ResolveGameplayTag(ExitName);
            if (!Exit.IsValid()) { return false; }
            Data.Set_LinkedEventTag(Exit);
        }
        FCk_Handle_DialogLine Line;
        if (AlwaysFail)
        {
            auto Condition = NewObject(this, UCk_DialogTestCond_AlwaysFail);
            if (ck::Is_NOT_Valid(Condition)) { return false; }
            Line = _Registry.Request_RegisterLine_WithCondition(Data, Filter, Condition);
        }
        else { Line = _Registry.Request_RegisterLine(Data, Filter); }
        // Retain the returned receipt before inspecting it or continuing.
        if (ck::IsValid(Line)) { _Lines.Add(Line); }
        return ck::IsValid(Line);
    }

    bool Create(UObject WorldContext)
    {
        if (_Attempted || _Closed || ck::Is_NOT_Valid(WorldContext) ||
            ck::Is_NOT_Valid(WorldContext.GetWorld()))
        { return false; }
        auto State = Cast<ACk_DialogGym_GameState>(WorldContext.GetWorld().GetGameState());
        if (ck::Is_NOT_Valid(State) || !State.HasAuthority() ||
            State.GetWorld() != WorldContext.GetWorld())
        { return false; }
        _Attempted = true;
        _Context = WorldContext;
        _Registry = UCk_DialogRegistry_Subsystem_UE::Get_DialogRegistry();
        if (ck::Is_NOT_Valid(_Registry) ||
            _Registry.GetWorld() != WorldContext.GetWorld())
        { _Closed = true; return false; }
        _IDs.Add(n"DialogGym.Basics.Hello");
        _IDs.Add(n"DialogGym.Basics.Weather");
        _IDs.Add(n"DialogGym.Basics.Gated");
        _IDs.Add(n"DialogGym.Filters.TownieLine");
        _IDs.Add(n"DialogGym.Filters.NamedLine");
        _IDs.Add(n"DialogGym.Filters.GlobalLine");
        _IDs.Add(n"DialogGym.Cooldowns.Line");
        _IDs.Add(n"DialogGym.Chains.A");
        _IDs.Add(n"DialogGym.Chains.B");
        _IDs.Add(n"DialogGym.Chains.C");
        _IDs.Add(n"Graph.Greeting");
        _IDs.Add(n"Graph.Friendly");
        _IDs.Add(n"Graph.Hostile");
        _IDs.Add(n"Graph.Farewell");
        // Registry rejects duplicate IDs with an ensure. Preflight before any
        // registration, including when a previous session has not retired yet.
        auto Existing = _Registry.Get_AllLines();
        for (auto Line : Existing)
        {
            if (!UCk_DialogGym_Runtime_UE::Get_IsLiveLine(FCk_Handle(Line), WorldContext.GetWorld()))
            { _Closed = true; return false; }
            auto ID = Line.Get_LineID();
            for (auto Wanted : _IDs)
            {
                if (ID == Wanted) { _Closed = true; return false; }
            }
        }
        bool OK =
            AddLine(n"DialogGym.Basics.Hello", n"DialogGym.Basics.Greeting", "Hello, traveler!") &&
            AddLine(n"DialogGym.Basics.Weather", n"DialogGym.Basics.Greeting", "Lovely weather today.") &&
            AddLine(n"DialogGym.Basics.Gated", n"DialogGym.Basics.Greeting", "(gated off)",
                NAME_None, NAME_None, true) &&
            AddLine(n"DialogGym.Filters.TownieLine", n"DialogGym.Filters.Enter", "Nice day for a walk.",
                NAME_None, n"DialogGym.Filters.Townie") &&
            AddLine(n"DialogGym.Filters.NamedLine", n"DialogGym.Filters.Enter", "Ah, it is you again, hero.",
                NAME_None, n"DialogGym.Filters.NamedNpc") &&
            AddLine(n"DialogGym.Filters.GlobalLine", n"DialogGym.Filters.Enter", "(everyone can say this)") &&
            AddLine(n"DialogGym.Cooldowns.Line", n"DialogGym.Cooldowns.Enter", "Watch me go on cooldown.") &&
            AddLine(n"DialogGym.Chains.A", n"DialogGym.Chains.A", "So, you've come a long way.",
                n"DialogGym.Chains.B") &&
            AddLine(n"DialogGym.Chains.B", n"DialogGym.Chains.B", "The road ahead is dangerous.",
                n"DialogGym.Chains.C") &&
            AddLine(n"DialogGym.Chains.C", n"DialogGym.Chains.C", "Good luck, traveler.") &&
            AddLine(n"Graph.Greeting", n"DialogGym.Graph.Start", "Well met, traveler.",
                n"DialogGym.Graph.Choice") &&
            AddLine(n"Graph.Friendly", n"DialogGym.Graph.Choice", "Safe travels!",
                n"DialogGym.Graph.End") &&
            AddLine(n"Graph.Hostile", n"DialogGym.Graph.Choice", "Get lost!",
                NAME_None, NAME_None, true) &&
            AddLine(n"Graph.Farewell", n"DialogGym.Graph.End", "Farewell.");
        if (!OK) { Request_Close(); }
        return OK;
    }

    bool Get_IsHealthy()
    {
        if (!_Attempted || _Closed || ck::Is_NOT_Valid(_Context) ||
            ck::Is_NOT_Valid(_Registry) || _Lines.Num() != 14 ||
            _IDs.Num() != 14 || ck::Is_NOT_Valid(_Context.GetWorld()))
        { return false; }
        auto World = _Context.GetWorld();
        for (int32 I = 0; I < 14; I++)
        {
            auto Line = _Lines[I];
            if (!UCk_DialogGym_Runtime_UE::Get_IsLiveLine(FCk_Handle(Line), World) ||
                Line.Get_LineID() != _IDs[I])
            { return false; }
        }
        return true;
    }

    bool Get_IsReady()
    {
        if (!Get_IsHealthy() || !_Registry.Get_IsReady()) { return false; }
        // Registration is deferred. Every retained line must be query-visible
        // under its exact enter tag, including both condition-bearing lines.
        for (auto Line : _Lines)
        {
            auto Visible = _Registry.Get_Lines_ByEventTag(Line.Get_EventTag());
            bool Found = false;
            for (auto Entry : Visible)
            {
                if (Entry == Line) { Found = true; break; }
            }
            if (!Found) { return false; }
        }
        return true;
    }

    FCk_Handle_DialogLine Get_Line(FName ID)
    {
        if (!Get_IsHealthy() || ID.IsNone()) { return FCk_Handle_DialogLine(); }
        for (int32 I = 0; I < 14; I++)
        {
            if (_IDs[I] == ID) { return _Lines[I]; }
        }
        return FCk_Handle_DialogLine();
    }

    TArray<FCk_Handle_DialogLine> Get_Lines()
    { return Get_IsHealthy() ? _Lines : TArray<FCk_Handle_DialogLine>(); }

    void Request_Close()
    {
        if (_Closed && _Lines.Num() == 0) { return; }
        _Closed = true;
        if (ck::IsValid(_Registry))
        {
            for (auto Line : _Lines)
            {
                if (ck::IsValid(Line) && ck::IsValid(_Context) &&
                    ck::IsValid(_Context.GetWorld()) &&
                    UCk_DialogGym_Runtime_UE::Get_IsLiveLine(FCk_Handle(Line), _Context.GetWorld()))
                { _Registry.Request_UnregisterLine(Line); }
            }
        }
        _Lines.Reset();
    }
}
